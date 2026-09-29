/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Audit mode.
 *
 * Automatic checks of the horn arrangements (and the structure of every part):
 *   any-keys   the versions of one "Any Horns" chair in different keys should hold the same music
 *   reference  each horn line against its closest line in the reference horn section
 *   melody     horn lines that follow the lead sheet melody and break from it for a bar
 *   structure  bars with the wrong number of beats, key signatures that don't match, hidden or silent notes
 *   range      notes outside the instrument's range
 *   crossing / doubling   voice order inside a section
 *   dynamics   entrances after a long rest with no dynamic
 * plus the listen-through (each horn section at each rehearsal mark) and the library scan.
 */
#include "starscoreservice.h"

#include <algorithm>
#include <climits>
#include <cmath>
#include <tuple>
#include <map>
#include <set>

#include <QCryptographicHash>
#include <QDate>
#include <QDir>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QCoreApplication>
#include <QUuid>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/note.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/articulation.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/key.h"
#include "engraving/types/constants.h"
#include "engraving/types/symnames.h"

#include "notation/inotationinteraction.h"
#include "notation/inotationelements.h"
#include "notation/inotationparts.h"

#include "translation.h"
#include "log.h"

using namespace mu::project;
using namespace mu::engraving;
using namespace mu::notation;
using namespace muse;

namespace {
//! One onset in a bar: the notes of every voice and staff of a part that start at the same time
struct AuditChord {
    int rtick = 0;
    int ticks = 0;
    std::vector<int> pitches;   // concert, lowest first
    std::vector<int> tpcs;      // concert spelling, same order
    bool tie = false;
    std::vector<int> artics;    // SymIds, sorted
    bool slurStart = false;
    bool hidden = false;
    bool silent = false;
};

struct AuditBar {
    std::vector<AuditChord> chords;
    std::vector<std::pair<int, QString> > dynamics;   // rtick, text
    bool empty() const { return chords.empty(); }
};

using AuditLine = std::vector<AuditBar>;

struct AuditContext {
    const MasterScore* ms = nullptr;
    std::vector<const Measure*> measures;
    std::map<QString, AuditLine> lines;           // part id -> bars
    std::map<QString, QStringList> sectionsOfPart;
    std::map<QString, QString> sectionName;       // section id -> name
    std::map<QString, QString> labelOverride;     // part id -> e.g. "3-Horn Arr: Horn 2 in Eb (Alto Saxophone)"
};
}

// ---------------------------------------------------------------------------
//  Reading the music
// ---------------------------------------------------------------------------

static bool auditIsDrums(const Part* part)
{
    return !part || part->instrument()->useDrumset();
}

static AuditLine auditLineOf(const Part* part, const std::vector<const Measure*>& measures, const std::set<int>& slurTicks,
                             bool topOnly)
{
    AuditLine line;
    line.reserve(measures.size());
    std::vector<const Staff*> staves;
    for (const Staff* st : part->staves()) {
        staves.push_back(st);
        if (topOnly) {
            break;
        }
    }
    const track_idx_t firstTrack = part->staves().front()->idx() * VOICES;
    const track_idx_t endTrack = (part->staves().back()->idx() + 1) * VOICES;

    for (const Measure* m : measures) {
        AuditBar bar;
        std::map<int, AuditChord> byTick;
        for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            const int rtick = s->rtick().ticks();
            for (const Staff* st : staves) {
                const track_idx_t t0 = st->idx() * VOICES;
                for (track_idx_t track = t0; track < t0 + VOICES; ++track) {
                    const EngravingItem* e = s->element(track);
                    if (!e || !e->isChord()) {
                        continue;
                    }
                    const Chord* c = toChord(e);
                    AuditChord& ac = byTick[rtick];
                    const int dur = c->actualTicks().ticks();
                    ac.rtick = rtick;
                    ac.ticks = ac.ticks == 0 ? dur : std::min(ac.ticks, dur);
                    for (const Note* n : c->notes()) {
                        ac.pitches.push_back(n->pitch());
                        ac.tpcs.push_back(n->tpc1());
                        ac.tie = ac.tie || n->tieFor();
                        ac.hidden = ac.hidden || !n->visible();
                        ac.silent = ac.silent || !n->play();
                    }
                    ac.hidden = ac.hidden || !c->visible();
                    for (const Articulation* a : c->articulations()) {
                        ac.artics.push_back(int(a->symId()));
                    }
                    if (slurTicks.count(s->tick().ticks())) {
                        ac.slurStart = true;
                    }
                }
            }
            for (const EngravingItem* ann : s->annotations()) {
                if (ann->isDynamic() && ann->track() >= firstTrack && ann->track() < endTrack) {
                    bar.dynamics.push_back({ rtick, toDynamic(ann)->plainText().toQString() });
                }
            }
        }
        for (auto& [rtick, ac] : byTick) {
            // keep pitch and spelling paired while sorting
            std::vector<std::pair<int, int> > pt;
            for (size_t i = 0; i < ac.pitches.size(); ++i) {
                pt.push_back({ ac.pitches[i], ac.tpcs[i] });
            }
            std::sort(pt.begin(), pt.end());
            pt.erase(std::unique(pt.begin(), pt.end()), pt.end());
            if (topOnly && !pt.empty()) {
                pt = { pt.back() };
            }
            ac.pitches.clear();
            ac.tpcs.clear();
            for (const auto& [p, t] : pt) {
                ac.pitches.push_back(p);
                ac.tpcs.push_back(t);
            }
            std::sort(ac.artics.begin(), ac.artics.end());
            ac.artics.erase(std::unique(ac.artics.begin(), ac.artics.end()), ac.artics.end());
            bar.chords.push_back(std::move(ac));
        }
        std::sort(bar.dynamics.begin(), bar.dynamics.end());
        line.push_back(std::move(bar));
    }
    return line;
}

static std::set<int> auditSlurTicks(const MasterScore* ms, const Part* part)
{
    std::set<int> ticks;
    const track_idx_t firstTrack = part->staves().front()->idx() * VOICES;
    const track_idx_t endTrack = (part->staves().back()->idx() + 1) * VOICES;
    for (const auto& [tick, sp] : ms->spanner()) {
        if (sp && sp->isSlur() && sp->track() >= firstTrack && sp->track() < endTrack) {
            ticks.insert(sp->tick().ticks());
        }
    }
    return ticks;
}

//! Everything in the bar, for fingerprints and keys
static QString auditBarSig(const AuditBar& bar)
{
    QString sig;
    for (const AuditChord& c : bar.chords) {
        sig += QString("|%1d%2").arg(c.rtick).arg(c.ticks);
        for (int p : c.pitches) {
            sig += "n" + QString::number(p);
        }
        for (int a : c.artics) {
            sig += "a" + QString::number(a);
        }
        sig += c.tie ? "t" : "";
        sig += c.slurStart ? "s" : "";
    }
    for (const auto& [rt, text] : bar.dynamics) {
        sig += QString("D%1%2").arg(rt).arg(text);
    }
    return sig;
}

static QString auditHash(const QString& s)
{
    return QString::fromLatin1(QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Md5).toHex().left(12));
}

static QString auditPitchName(int tpc)
{
    static const char STEPS[] = "FCGDAEB";
    const int t = tpc + 1;
    const int step = ((t % 7) + 7) % 7;
    const int acc = (t - step) / 7;
    QString name = QString(QChar(STEPS[step]));
    switch (acc) {
    case 0: name += QString::fromUtf8("𝄫");
        break;
    case 1: name += QString::fromUtf8("♭");
        break;
    case 3: name += QString::fromUtf8("♯");
        break;
    case 4: name += QString::fromUtf8("𝄪");
        break;
    default: break;
    }
    return name;
}

static QString auditPitchNameWithOctave(int pitch, int tpc)
{
    const int t = tpc + 1;
    const int step = ((t % 7) + 7) % 7;
    const int alter = (t - step) / 7 - 2;
    const int octave = (pitch - alter) / 12 - 1;
    return auditPitchName(tpc) + QString::number(octave);
}

static QString auditChordNames(const AuditChord& c)
{
    QStringList names;
    for (int tpc : c.tpcs) {
        names << auditPitchName(tpc);
    }
    return names.join("/");
}

static QString auditBeat(const Measure* m, int rtick)
{
    const Fraction ts = m->timesig();
    const int beatTicks = std::max(1, Constants::DIVISION * 4 / std::max(1, ts.denominator()));
    const double beat = 1.0 + double(rtick) / beatTicks;
    return muse::qtrc("starscore", "beat %1").arg(QString::number(beat, 'g', 4));
}

static QString auditArticNames(const std::vector<int>& artics)
{
    if (artics.empty()) {
        return muse::qtrc("starscore", "no articulation");
    }
    QStringList names;
    for (int a : artics) {
        names << SymNames::translatedUserNameForSymId(SymId(a)).toQString().toLower();
    }
    return names.join(", ");
}

static std::vector<int> auditPitchClasses(const AuditChord& c)
{
    std::vector<int> pcs;
    for (int p : c.pitches) {
        pcs.push_back(((p % 12) + 12) % 12);
    }
    std::sort(pcs.begin(), pcs.end());
    pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
    return pcs;
}

//! Kinds of difference, worst first
enum class AuditDiff {
    None = 0,
    Marks,     // articulation, dynamic or slur only
    Notes      // rhythm, pitch, ties, or one part rests
};

//! First difference between two bars, described from a's side ("here") against b ("in <other>").
static AuditDiff auditCompareBars(const AuditBar& a, const AuditBar& b, bool pitchClass, bool marks, const Measure* m,
                                  const QString& other, QString* message)
{
    auto say = [&](const QString& text) {
        if (message) {
            *message = text;
        }
    };

    if (a.empty() && b.empty()) {
        return AuditDiff::None;
    }
    if (a.empty()) {
        say(muse::qtrc("starscore", "Rests here; %1 plays").arg(other));
        return AuditDiff::Notes;
    }
    if (b.empty()) {
        say(muse::qtrc("starscore", "Plays here; %1 rests").arg(other));
        return AuditDiff::Notes;
    }

    // Rhythm: onsets
    size_t i = 0;
    while (i < a.chords.size() && i < b.chords.size() && a.chords[i].rtick == b.chords[i].rtick) {
        ++i;
    }
    if (i < a.chords.size() || i < b.chords.size()) {
        int rt = INT_MAX;
        if (i < a.chords.size()) {
            rt = std::min(rt, a.chords[i].rtick);
        }
        if (i < b.chords.size()) {
            rt = std::min(rt, b.chords[i].rtick);
        }
        say(muse::qtrc("starscore", "Rhythm differs from %1 at %2").arg(other, auditBeat(m, rt)));
        return AuditDiff::Notes;
    }

    for (size_t k = 0; k < a.chords.size(); ++k) {
        const AuditChord& ca = a.chords[k];
        const AuditChord& cb = b.chords[k];
        if (ca.ticks != cb.ticks) {
            say(muse::qtrc("starscore", "Note length differs from %1 at %2").arg(other, auditBeat(m, ca.rtick)));
            return AuditDiff::Notes;
        }
        const bool samePitch = pitchClass ? auditPitchClasses(ca) == auditPitchClasses(cb) : ca.pitches == cb.pitches;
        if (!samePitch) {
            say(muse::qtrc("starscore", "%1: %2 here, %3 in %4 (concert)")
                .arg(auditBeat(m, ca.rtick), auditChordNames(ca), auditChordNames(cb), other));
            return AuditDiff::Notes;
        }
        if (ca.tie != cb.tie) {
            say(muse::qtrc("starscore", "Tie differs from %1 at %2").arg(other, auditBeat(m, ca.rtick)));
            return AuditDiff::Notes;
        }
    }

    if (!marks) {
        return AuditDiff::None;
    }

    for (size_t k = 0; k < a.chords.size(); ++k) {
        const AuditChord& ca = a.chords[k];
        const AuditChord& cb = b.chords[k];
        if (ca.artics != cb.artics) {
            say(muse::qtrc("starscore", "%1: %2 here, %3 in %4")
                .arg(auditBeat(m, ca.rtick), auditArticNames(ca.artics), auditArticNames(cb.artics), other));
            return AuditDiff::Marks;
        }
        if (ca.slurStart != cb.slurStart) {
            say(ca.slurStart
                ? muse::qtrc("starscore", "A slur starts at %1 here but not in %2").arg(auditBeat(m, ca.rtick), other)
                : muse::qtrc("starscore", "A slur starts at %1 in %2 but not here").arg(auditBeat(m, ca.rtick), other));
            return AuditDiff::Marks;
        }
    }
    if (a.dynamics != b.dynamics) {
        QStringList da;
        QStringList db;
        for (const auto& d : a.dynamics) {
            da << d.second;
        }
        for (const auto& d : b.dynamics) {
            db << d.second;
        }
        say(muse::qtrc("starscore", "Dynamics: %1 here, %2 in %3")
            .arg(da.isEmpty() ? muse::qtrc("starscore", "none") : da.join(" "),
                 db.isEmpty() ? muse::qtrc("starscore", "none") : db.join(" "), other));
        return AuditDiff::Marks;
    }
    return AuditDiff::None;
}

// ---------------------------------------------------------------------------
//  Sections
// ---------------------------------------------------------------------------

static bool auditIsLeadOrRhythm(const StarScoreSection& s)
{
    return s.templateKey == "lead-sheet" || s.templateKey == "rhythm" || s.templateKey.endsWith("-rhythm")
           || s.name.contains("Lead Sheet", Qt::CaseInsensitive) || s.name.contains("Rhythm", Qt::CaseInsensitive);
}

//! "3-Horn Section" -> 3; 0 when the name has no horn count
static int auditHornCount(const StarScoreSection& s)
{
    static const QRegularExpression re("(\\d+)\\s*-?\\s*Horn", QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(s.name);
    if (m.hasMatch()) {
        return m.captured(1).toInt();
    }
    static const QRegularExpression tre("^(\\d+)-horn");
    const QRegularExpressionMatch tm = tre.match(s.templateKey);
    return tm.hasMatch() ? tm.captured(1).toInt() : 0;
}

static bool auditIsAny(const StarScoreSection& s)
{
    return s.name.contains("Any", Qt::CaseSensitive) || s.name.contains("Flexible", Qt::CaseSensitive)
           || s.templateKey.contains("any");
}

//! Horn sections for the reference comparison and the listen-through: biggest first, the standard
//! section of a size before its "Any" section
static std::vector<const StarScoreSection*> auditHornSections(const std::vector<StarScoreSection>& sections)
{
    std::vector<const StarScoreSection*> out;
    for (const StarScoreSection& s : sections) {
        if (auditHornCount(s) > 0 && !auditIsLeadOrRhythm(s)) {
            out.push_back(&s);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const StarScoreSection* a, const StarScoreSection* b) {
        const int na = auditHornCount(*a);
        const int nb = auditHornCount(*b);
        if (na != nb) {
            return na > nb;
        }
        return !auditIsAny(*a) && auditIsAny(*b);
    });
    return out;
}

static std::vector<const Part*> auditMasterPartsOf(const MasterScore* ms, const Excerpt* excerpt)
{
    std::vector<const Part*> result;
    std::vector<Part*> candidates = excerpt->excerptScore() ? excerpt->excerptScore()->parts() : excerpt->parts();
    for (const Part* p : candidates) {
        const Part* masterPart = nullptr;
        if (p->score() == ms) {
            masterPart = p;
        } else {
            for (Staff* staff : p->staves()) {
                if (Staff* linked = staff->findLinkedInScore(ms)) {
                    masterPart = linked->part();
                    break;
                }
            }
        }
        if (masterPart && std::find(result.begin(), result.end(), masterPart) == result.end()) {
            result.push_back(masterPart);
        }
    }
    return result;
}

//! Bars where both play and match (pitch classes, rhythm), and bars where either plays
static std::pair<int, int> auditAgreement(const AuditLine& a, const AuditLine& b, const std::vector<const Measure*>& measures)
{
    int same = 0;
    int either = 0;
    for (size_t i = 0; i < measures.size() && i < a.size() && i < b.size(); ++i) {
        if (a[i].empty() && b[i].empty()) {
            continue;
        }
        ++either;
        if (!a[i].empty() && !b[i].empty()
            && auditCompareBars(a[i], b[i], true, false, measures[i], QString(), nullptr) == AuditDiff::None) {
            ++same;
        }
    }
    return { same, either };
}

//! "Any Horns" sections holding several keys of each chair: the parts of each chair, the chair's
//! most typical version first. Chairs come from the part books' names ("3-Horn Arr: Horn 2 in Eb"),
//! otherwise from the music.
static std::vector<std::vector<const Part*> > auditChairGroups(const AuditContext& ctx, const StarScoreSection& section)
{
    std::vector<const Part*> parts;
    for (const QString& pid : section.partIds) {
        const Part* p = ctx.ms->partById(ID(pid));
        if (p && !auditIsDrums(p) && ctx.lines.count(pid)) {
            parts.push_back(p);
        }
    }

    const int hornCount = auditHornCount(section);
    std::map<int, std::vector<const Part*> > byChair;
    std::set<const Part*> placed;
    static const QRegularExpression bookRe("(\\d+)\\s*-?\\s*Horn.*\\bHorn\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression chairRe("^\\s*Horn\\s*(\\d+)\\s*$", QRegularExpression::CaseInsensitiveOption);
    for (const Excerpt* ex : ctx.ms->excerpts()) {
        const QRegularExpressionMatch m = bookRe.match(ex->name().toQString());
        if (!m.hasMatch() || (hornCount > 0 && m.captured(1).toInt() != hornCount)) {
            continue;
        }
        const int chair = m.captured(2).toInt();
        for (const Part* p : auditMasterPartsOf(ctx.ms, ex)) {
            if (std::find(parts.begin(), parts.end(), p) != parts.end() && !placed.count(p)) {
                byChair[chair].push_back(p);
                placed.insert(p);
            }
        }
    }
    for (const Part* p : parts) {
        if (placed.count(p)) {
            continue;
        }
        const QRegularExpressionMatch m = chairRe.match(p->partName().toQString());
        if (m.hasMatch()) {
            byChair[m.captured(1).toInt()].push_back(p);
            placed.insert(p);
        }
    }

    std::vector<std::vector<const Part*> > groups;
    for (auto& [chair, list] : byChair) {
        groups.push_back(list);
    }

    // The rest: join the chair whose music it matches, or stand alone
    for (const Part* p : parts) {
        if (placed.count(p)) {
            continue;
        }
        const AuditLine& lp = ctx.lines.at(StarScoreService::idTextOf(p));
        int best = -1;
        double bestScore = 0.5;
        for (size_t g = 0; g < groups.size(); ++g) {
            const auto [same, either] = auditAgreement(lp, ctx.lines.at(StarScoreService::idTextOf(groups[g].front())), ctx.measures);
            const double score = either > 0 ? double(same) / either : 0.0;
            if (score >= bestScore) {
                bestScore = score;
                best = int(g);
            }
        }
        if (best >= 0) {
            groups[best].push_back(p);
        } else {
            groups.push_back({ p });
        }
        placed.insert(p);
    }

    // Most typical version first (agrees with the others the most)
    for (auto& group : groups) {
        if (group.size() < 3) {
            continue;
        }
        size_t bestIdx = 0;
        int bestTotal = -1;
        for (size_t i = 0; i < group.size(); ++i) {
            int total = 0;
            for (size_t j = 0; j < group.size(); ++j) {
                if (i != j) {
                    total += auditAgreement(ctx.lines.at(StarScoreService::idTextOf(group[i])),
                                            ctx.lines.at(StarScoreService::idTextOf(group[j])), ctx.measures).first;
                }
            }
            if (total > bestTotal) {
                bestTotal = total;
                bestIdx = i;
            }
        }
        std::rotate(group.begin(), group.begin() + bestIdx, group.begin() + bestIdx + 1);
    }
    return groups;
}

//! Does this "Any" section hold several versions (keys) of a chair?
static bool auditHasVersions(const std::vector<std::vector<const Part*> >& groups)
{
    return std::any_of(groups.begin(), groups.end(), [](const std::vector<const Part*>& g) { return g.size() > 1; });
}

// ---------------------------------------------------------------------------
//  The checks
// ---------------------------------------------------------------------------

namespace {
struct AuditIssueBuilder {
    AuditContext& ctx;
    std::vector<StarScoreAuditIssue>& out;

    QString label(const QString& partId) const
    {
        auto it = ctx.labelOverride.find(partId);
        if (it != ctx.labelOverride.end()) {
            return it->second;
        }
        const Part* p = ctx.ms->partById(ID(partId));
        QStringList names;
        auto sit = ctx.sectionsOfPart.find(partId);
        if (sit != ctx.sectionsOfPart.end()) {
            for (const QString& sid : sit->second) {
                names << ctx.sectionName[sid];
            }
        }
        const QString name = p ? p->partName().toQString() : partId;
        return names.isEmpty() ? name : QString("%1 — %2").arg(name, names.join(", "));
    }

    //! "Trumpet A (6-Horn Section)", for messages
    QString shortLabel(const QString& partId) const
    {
        auto it = ctx.labelOverride.find(partId);
        if (it != ctx.labelOverride.end()) {
            return it->second;
        }
        const Part* p = ctx.ms->partById(ID(partId));
        QStringList names;
        auto sit = ctx.sectionsOfPart.find(partId);
        if (sit != ctx.sectionsOfPart.end()) {
            for (const QString& sid : sit->second) {
                names << ctx.sectionName[sid];
            }
        }
        const QString name = p ? p->partName().toQString() : partId;
        return names.isEmpty() ? name : QString("%1 (%2)").arg(name, names.first());
    }

    QString rangeSig(const QString& partId, int from, int to) const
    {
        QString sig;
        auto it = ctx.lines.find(partId);
        if (it == ctx.lines.end()) {
            return sig;
        }
        for (int b = from; b <= to && b < int(it->second.size()); ++b) {
            sig += auditBarSig(it->second[b]) + "#";
        }
        return sig;
    }

    //! from/to are 0-based bar indices
    void add(const QString& check, int severity, const QString& partId, const QString& otherPartId, int from, int to,
             const QString& title, const QString& message)
    {
        StarScoreAuditIssue issue;
        issue.check = check;
        issue.severity = severity;
        issue.bar = from + 1;
        issue.endBar = to + 1;
        issue.partId = partId;
        issue.otherPartId = otherPartId;
        auto sit = ctx.sectionsOfPart.find(partId);
        if (sit != ctx.sectionsOfPart.end()) {
            issue.sectionIds = sit->second;
        }
        issue.title = title;
        issue.partLabel = label(partId);
        issue.message = message;
        issue.key = QString("%1|%2|%3|%4-%5|%6").arg(check, partId, otherPartId).arg(issue.bar).arg(issue.endBar)
                    .arg(auditHash(rangeSig(partId, from, to) + "~" + rangeSig(otherPartId, from, to) + "~" + message));
        out.push_back(std::move(issue));
    }
};

//! Consecutive flagged bars: [from, to] with the first bar's message and the worst kind
struct AuditRun {
    int from = 0;
    int to = 0;
    AuditDiff worst = AuditDiff::None;
    QString message;
};
}

static std::vector<AuditRun> auditRuns(const std::vector<std::pair<AuditDiff, QString> >& perBar)
{
    std::vector<AuditRun> runs;
    for (int b = 0; b < int(perBar.size()); ++b) {
        if (perBar[b].first == AuditDiff::None) {
            continue;
        }
        if (!runs.empty() && runs.back().to == b - 1) {
            runs.back().to = b;
            runs.back().worst = std::max(runs.back().worst, perBar[b].first);
        } else {
            runs.push_back({ b, b, perBar[b].first, perBar[b].second });
        }
    }
    return runs;
}

static QString auditWithMore(const AuditRun& run)
{
    const int more = run.to - run.from;
    if (more <= 0) {
        return run.message;
    }
    return run.message + " " + muse::qtrc("starscore", "(and %n more bar(s))", nullptr, more);
}

//! Check 1: every version of a chair in an "Any Horns" section against the chair's main version
static void auditAnyKeys(AuditContext& ctx, AuditIssueBuilder& add, const std::vector<std::vector<const Part*> >& groups)
{
    for (const std::vector<const Part*>& group : groups) {
        if (group.size() < 2) {
            continue;
        }
        const QString refId = StarScoreService::idTextOf(group.front());
        const AuditLine& ref = ctx.lines.at(refId);
        for (size_t i = 1; i < group.size(); ++i) {
            const QString pid = StarScoreService::idTextOf(group[i]);
            const AuditLine& line = ctx.lines.at(pid);
            const QString other = add.shortLabel(refId);

            std::vector<std::pair<AuditDiff, QString> > perBar(ctx.measures.size());
            std::map<int, int> octaveVotes;   // octave offset -> bars
            std::vector<int> barOctave(ctx.measures.size(), INT_MIN);
            for (size_t b = 0; b < ctx.measures.size(); ++b) {
                QString msg;
                const AuditDiff d = auditCompareBars(line[b], ref[b], true, true, ctx.measures[b], other, &msg);
                perBar[b] = { d, msg };
                if (d == AuditDiff::None && !line[b].empty()) {
                    // same notes: which octave apart?
                    std::map<int, int> votes;
                    for (size_t k = 0; k < line[b].chords.size(); ++k) {
                        const int diff = line[b].chords[k].pitches.front() - ref[b].chords[k].pitches.front();
                        ++votes[int(std::lround(diff / 12.0))];
                    }
                    int best = 0;
                    int bestCount = -1;
                    for (const auto& [oct, count] : votes) {
                        if (count > bestCount) {
                            best = oct;
                            bestCount = count;
                        }
                    }
                    barOctave[b] = votes.size() > 1 ? INT_MAX : best;   // INT_MAX: the octave changes within the bar
                    ++octaveVotes[barOctave[b]];
                }
            }

            for (const AuditRun& run : auditRuns(perBar)) {
                add.add("any-keys", run.worst == AuditDiff::Notes ? 2 : 1, pid, refId, run.from, run.to,
                        muse::qtrc("starscore", "Doesn't match this chair in another key"), auditWithMore(run));
            }

            // Octave: the usual distance between the two versions, and bars that break from it
            int usual = INT_MIN;
            int usualCount = 0;
            for (const auto& [oct, count] : octaveVotes) {
                if (oct != INT_MAX && count > usualCount) {
                    usual = oct;
                    usualCount = count;
                }
            }
            if (usual == INT_MIN) {
                continue;
            }
            std::vector<std::pair<AuditDiff, QString> > octBars(ctx.measures.size());
            for (size_t b = 0; b < ctx.measures.size(); ++b) {
                if (barOctave[b] != INT_MIN && barOctave[b] != usual) {
                    octBars[b] = { AuditDiff::Marks,
                                   muse::qtrc("starscore", "Octave differs from the rest of this version compared with %1").arg(other) };
                }
            }
            for (const AuditRun& run : auditRuns(octBars)) {
                add.add("any-keys", 0, pid, refId, run.from, run.to,
                        muse::qtrc("starscore", "Octave change against the other key"), run.message);
            }
        }
    }
}

//! Neighbouring bars (the nearest bar before and after where either part plays) both match
static bool auditIsIsland(const AuditLine& a, const AuditLine& b, const std::vector<const Measure*>& measures, int bar)
{
    auto matchesAt = [&](int i) {
        return !a[i].empty() && !b[i].empty()
               && auditCompareBars(a[i], b[i], true, false, measures[i], QString(), nullptr) == AuditDiff::None;
    };
    int before = bar - 1;
    while (before >= 0 && a[before].empty() && b[before].empty()) {
        --before;
    }
    int after = bar + 1;
    while (after < int(measures.size()) && a[after].empty() && b[after].empty()) {
        ++after;
    }
    return before >= 0 && after < int(measures.size()) && matchesAt(before) && matchesAt(after);
}

//! Check 2: each horn line against its closest line in the reference section
static void auditReference(AuditContext& ctx, AuditIssueBuilder& add, const StarScoreSection& refSection,
                           const std::vector<std::pair<const StarScoreSection*, std::vector<const Part*> > >& linesBySection)
{
    std::vector<QString> refParts;
    for (const QString& pid : refSection.partIds) {
        const Part* p = ctx.ms->partById(ID(pid));
        if (p && !auditIsDrums(p) && ctx.lines.count(pid)) {
            refParts.push_back(pid);
        }
    }
    if (refParts.empty()) {
        return;
    }

    for (const auto& [section, parts] : linesBySection) {
        if (section->id == refSection.id) {
            continue;
        }
        for (const Part* part : parts) {
            const QString pid = StarScoreService::idTextOf(part);
            if (std::find(refParts.begin(), refParts.end(), pid) != refParts.end()) {
                continue;   // the same instrument is in the reference section
            }
            const AuditLine& line = ctx.lines.at(pid);
            int plays = 0;
            for (const AuditBar& bar : line) {
                plays += bar.empty() ? 0 : 1;
            }
            if (plays == 0) {
                continue;
            }

            QString bestId;
            int bestSame = 0;
            int bestEither = 1;
            for (const QString& rid : refParts) {
                const auto [same, either] = auditAgreement(line, ctx.lines.at(rid), ctx.measures);
                if (same > bestSame) {
                    bestSame = same;
                    bestEither = either;
                    bestId = rid;
                }
            }
            if (bestId.isEmpty() || bestSame < 2 || bestSame < plays / 4) {
                continue;   // an independent line: nothing to compare it with
            }
            const double similarity = double(bestSame) / std::max(1, bestEither);
            const AuditLine& ref = ctx.lines.at(bestId);
            const QString other = add.shortLabel(bestId);

            std::vector<std::pair<AuditDiff, QString> > perBar(ctx.measures.size());
            std::vector<bool> island(ctx.measures.size(), false);
            for (size_t b = 0; b < ctx.measures.size(); ++b) {
                QString msg;
                const AuditDiff d = auditCompareBars(line[b], ref[b], true, true, ctx.measures[b], other, &msg);
                if (d == AuditDiff::None) {
                    continue;
                }
                island[b] = auditIsIsland(line, ref, ctx.measures, int(b));
                if (similarity >= 0.6 || island[b]) {
                    perBar[b] = { d, msg };
                }
            }
            for (const AuditRun& run : auditRuns(perBar)) {
                int severity = run.worst == AuditDiff::Notes ? 1 : 0;
                if (run.worst == AuditDiff::Notes && run.to - run.from <= 1 && island[run.from]) {
                    severity = 2;   // a bar or two that break from an otherwise identical line
                }
                add.add("reference", severity, pid, bestId, run.from, run.to,
                        muse::qtrc("starscore", "Differs from the reference section"), auditWithMore(run));
            }
        }
    }
}

//! Horn lines that follow the lead sheet melody and break from it for a bar
static void auditMelody(AuditContext& ctx, AuditIssueBuilder& add, const Part* lead,
                        const std::vector<std::pair<const StarScoreSection*, std::vector<const Part*> > >& linesBySection)
{
    const QString leadId = StarScoreService::idTextOf(lead);
    const AuditLine melody = auditLineOf(lead, ctx.measures, {}, true);
    const QString other = muse::qtrc("starscore", "the lead sheet");
    std::set<QString> done;
    for (const auto& [section, parts] : linesBySection) {
        for (const Part* part : parts) {
            const QString pid = StarScoreService::idTextOf(part);
            if (pid == leadId || done.count(pid)) {
                continue;
            }
            done.insert(pid);
            const AuditLine top = auditLineOf(part, ctx.measures, {}, true);
            for (size_t b = 0; b < ctx.measures.size(); ++b) {
                if (top[b].empty() || melody[b].empty()) {
                    continue;
                }
                QString msg;
                if (auditCompareBars(top[b], melody[b], true, false, ctx.measures[b], other, &msg) == AuditDiff::None) {
                    continue;
                }
                if (auditIsIsland(top, melody, ctx.measures, int(b))) {
                    add.add("melody", 1, pid, leadId, int(b), int(b),
                            muse::qtrc("starscore", "Breaks from the melody for one bar"), msg);
                }
            }
        }
    }
}

static QString auditKeyName(int fifths)
{
    static const char* NAMES[] = { "C♭", "G♭", "D♭", "A♭", "E♭", "B♭", "F", "C", "G", "D", "A", "E", "B", "F♯", "C♯" };
    if (fifths < -7 || fifths > 7) {
        return QString::number(fifths);
    }
    return QString::fromUtf8(NAMES[fifths + 7]);
}

//! Check 3: beats per bar, key signatures, hidden and silent notes
static void auditStructure(AuditContext& ctx, AuditIssueBuilder& add, const std::vector<const Part*>& parts,
                           const std::set<QString>& hornParts, const Part* keyReference)
{
    const Staff* keyStaff = keyReference ? keyReference->staves().front() : nullptr;
    const QString keyRefName = keyReference ? add.shortLabel(StarScoreService::idTextOf(keyReference)) : QString();

    for (const Part* part : parts) {
        const QString pid = StarScoreService::idTextOf(part);
        const bool drums = auditIsDrums(part);

        // Beats in each voice
        std::vector<std::pair<AuditDiff, QString> > beats(ctx.measures.size());
        for (size_t b = 0; b < ctx.measures.size(); ++b) {
            const Measure* m = ctx.measures[b];
            for (const Staff* staff : part->staves()) {
                const Fraction expected = m->stretchedLen(staff);
                for (voice_idx_t v = 0; v < VOICES && beats[b].first == AuditDiff::None; ++v) {
                    const track_idx_t track = staff->idx() * VOICES + v;
                    Fraction sum(0, 1);
                    bool any = false;
                    for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
                        const EngravingItem* e = s->element(track);
                        if (e && e->isChordRest()) {
                            sum += toChordRest(e)->actualTicks();
                            any = true;
                        }
                    }
                    if (v == 0 && !any) {
                        beats[b] = { AuditDiff::Notes, muse::qtrc("starscore", "The bar is empty (no notes or rests)") };
                    } else if (any && sum > expected) {
                        beats[b] = { AuditDiff::Notes, v == 0 ? muse::qtrc("starscore", "The bar has too many beats")
                                     : muse::qtrc("starscore", "Voice %1 has too many beats").arg(v + 1) };
                    } else if (v == 0 && any && sum < expected) {
                        beats[b] = { AuditDiff::Notes, muse::qtrc("starscore", "The bar has too few beats") };
                    }
                }
            }
        }
        for (const AuditRun& run : auditRuns(beats)) {
            add.add("structure", 2, pid, QString(), run.from, run.to, muse::qtrc("starscore", "Wrong number of beats"), run.message);
        }

        if (drums) {
            continue;
        }

        // Key signature (concert) against the lead sheet
        if (keyStaff && part != keyReference) {
            std::vector<std::pair<AuditDiff, QString> > keys(ctx.measures.size());
            for (size_t b = 0; b < ctx.measures.size(); ++b) {
                const Fraction tick = ctx.measures[b]->tick();
                const KeySigEvent ref = keyStaff->keySigEvent(tick);
                if (ref.isAtonal() || ref.custom()) {
                    continue;
                }
                for (const Staff* staff : part->staves()) {
                    if (staff->isDrumStaff(tick) || !staff->isPitchedStaff(tick)) {
                        continue;
                    }
                    const KeySigEvent ev = staff->keySigEvent(tick);
                    if (ev.isAtonal() || ev.custom()) {
                        continue;
                    }
                    if (ev.concertKey() != ref.concertKey()) {
                        keys[b] = { AuditDiff::Notes, muse::qtrc("starscore", "Key signature: concert %1 here, %2 in %3")
                                    .arg(auditKeyName(int(ev.concertKey())), auditKeyName(int(ref.concertKey())), keyRefName) };
                        break;
                    }
                }
            }
            for (const AuditRun& run : auditRuns(keys)) {
                add.add("structure", 2, pid, StarScoreService::idTextOf(keyReference), run.from, run.to,
                        muse::qtrc("starscore", "Key signature doesn't match"), run.message);
            }
        }

        // Local time signature
        std::vector<std::pair<AuditDiff, QString> > stretch(ctx.measures.size());
        for (size_t b = 0; b < ctx.measures.size(); ++b) {
            for (const Staff* staff : part->staves()) {
                if (staff->timeStretch(ctx.measures[b]->tick()) != Fraction(1, 1)) {
                    stretch[b] = { AuditDiff::Notes, muse::qtrc("starscore", "This staff has its own (local) time signature") };
                }
            }
        }
        for (const AuditRun& run : auditRuns(stretch)) {
            add.add("structure", 1, pid, QString(), run.from, run.to, muse::qtrc("starscore", "Local time signature"), run.message);
        }

        // Hidden or silent notes in horn parts
        if (!hornParts.count(pid)) {
            continue;
        }
        const AuditLine& line = ctx.lines.at(pid);
        std::vector<std::pair<AuditDiff, QString> > hidden(ctx.measures.size());
        std::vector<std::pair<AuditDiff, QString> > silent(ctx.measures.size());
        for (size_t b = 0; b < line.size(); ++b) {
            for (const AuditChord& c : line[b].chords) {
                if (c.hidden && hidden[b].first == AuditDiff::None) {
                    hidden[b] = { AuditDiff::Notes, muse::qtrc("starscore", "Invisible note at %1").arg(auditBeat(ctx.measures[b], c.rtick)) };
                }
                if (c.silent && silent[b].first == AuditDiff::None) {
                    silent[b] = { AuditDiff::Notes, muse::qtrc("starscore", "Note set not to play at %1").arg(auditBeat(ctx.measures[b],
                                                                                                                   c.rtick)) };
                }
            }
        }
        for (const AuditRun& run : auditRuns(hidden)) {
            add.add("structure", 1, pid, QString(), run.from, run.to, muse::qtrc("starscore", "Invisible notes"), auditWithMore(run));
        }
        for (const AuditRun& run : auditRuns(silent)) {
            add.add("structure", 1, pid, QString(), run.from, run.to, muse::qtrc("starscore", "Notes that don't play back"),
                    auditWithMore(run));
        }
    }
}

//! Check 5a: notes outside the instrument's range (red = professional range, yellow = comfortable range)
static void auditRange(AuditContext& ctx, AuditIssueBuilder& add, const std::vector<const Part*>& hornParts)
{
    for (const Part* part : hornParts) {
        const QString pid = StarScoreService::idTextOf(part);
        const AuditLine& line = ctx.lines.at(pid);
        std::vector<std::pair<AuditDiff, QString> > pro(ctx.measures.size());
        std::vector<std::pair<AuditDiff, QString> > comfy(ctx.measures.size());
        for (size_t b = 0; b < line.size(); ++b) {
            const Instrument* instr = part->instrument(ctx.measures[b]->tick());
            if (!instr) {
                continue;
            }
            for (const AuditChord& c : line[b].chords) {
                for (size_t k = 0; k < c.pitches.size(); ++k) {
                    const int p = c.pitches[k];
                    const QString name = auditPitchNameWithOctave(p, c.tpcs[k]);
                    if (p > instr->maxPitchP() || p < instr->minPitchP()) {
                        if (pro[b].first == AuditDiff::None) {
                            pro[b] = { AuditDiff::Notes, p > instr->maxPitchP()
                                       ? muse::qtrc("starscore", "%1 (concert) is above the professional range").arg(name)
                                       : muse::qtrc("starscore", "%1 (concert) is below the professional range").arg(name) };
                        }
                    } else if ((p > instr->maxPitchA() || p < instr->minPitchA()) && comfy[b].first == AuditDiff::None) {
                        comfy[b] = { AuditDiff::Marks, p > instr->maxPitchA()
                                     ? muse::qtrc("starscore", "%1 (concert) is above the comfortable range").arg(name)
                                     : muse::qtrc("starscore", "%1 (concert) is below the comfortable range").arg(name) };
                    }
                }
            }
        }
        for (const AuditRun& run : auditRuns(pro)) {
            add.add("range", 2, pid, QString(), run.from, run.to, muse::qtrc("starscore", "Out of range"), auditWithMore(run));
        }
        for (const AuditRun& run : auditRuns(comfy)) {
            add.add("range", 0, pid, QString(), run.from, run.to, muse::qtrc("starscore", "Outside the comfortable range"),
                    auditWithMore(run));
        }
    }
}

//! Check 5c: an entrance after four or more bars' rest (or the first entrance) with no dynamic
static void auditDynamics(AuditContext& ctx, AuditIssueBuilder& add, const std::vector<const Part*>& hornParts)
{
    for (const Part* part : hornParts) {
        const QString pid = StarScoreService::idTextOf(part);
        const AuditLine& line = ctx.lines.at(pid);
        int lastPlaying = -1;
        for (int b = 0; b < int(line.size()); ++b) {
            if (line[b].empty()) {
                continue;
            }
            const int restBars = lastPlaying < 0 ? b : b - lastPlaying - 1;
            if (lastPlaying < 0 || restBars >= 4) {
                // any dynamic from the start of the rest up to the first note?
                const int firstNote = line[b].chords.front().rtick;
                bool found = false;
                for (int k = lastPlaying + 1; k <= b && !found; ++k) {
                    for (const auto& d : line[k].dynamics) {
                        if (k < b || d.first <= firstNote) {
                            found = true;
                            break;
                        }
                    }
                }
                if (!found) {
                    add.add("dynamics", 1, pid, QString(), b, b, muse::qtrc("starscore", "No dynamic at the entrance"),
                            lastPlaying < 0 ? muse::qtrc("starscore", "First entrance, %1").arg(auditBeat(ctx.measures[b], firstNote))
                            : muse::qtrc("starscore", "Comes in at %1 after %n bar(s) of rest", nullptr, restBars)
                            .arg(auditBeat(ctx.measures[b], firstNote)));
                }
            }
            lastPlaying = b;
        }
    }
}

//! Fingerprint of the music of these parts (changes whenever a note, rhythm, articulation, slur or dynamic changes)
static QString auditFingerprint(const std::set<QString>& partIds, const std::map<QString, AuditLine>& lines)
{
    QString fp;
    for (const QString& pid : partIds) {
        auto it = lines.find(pid);
        if (it == lines.end()) {
            continue;
        }
        fp += pid + ":";
        for (const AuditBar& bar : it->second) {
            fp += auditBarSig(bar) + "#";
        }
    }
    return auditHash(fp);
}

static std::set<QString> auditArrangementParts(const StarScoreArrangement& arr, const std::vector<StarScoreSection>& sections)
{
    std::set<QString> partIds;
    for (const StarScoreSection& s : sections) {
        if (arr.sectionIds.contains(s.id)) {
            partIds.insert(s.partIds.begin(), s.partIds.end());
        }
    }
    return partIds;
}

QString StarScoreService::idTextOf(const Part* part)
{
    return idText(part);
}

// ---------------------------------------------------------------------------
//  The whole audit of one score
// ---------------------------------------------------------------------------

StarScoreAuditReport StarScoreService::auditScore(const MasterScore* ms, const Data& data) const
{
    StarScoreAuditReport report;
    if (!ms) {
        return report;
    }

    AuditContext ctx;
    ctx.ms = ms;
    for (const Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        ctx.measures.push_back(m);
    }
    for (const StarScoreSection& s : data.sections) {
        ctx.sectionName[s.id] = s.name;
        for (const QString& pid : s.partIds) {
            ctx.sectionsOfPart[pid] << s.id;
        }
    }

    // Parts to check: those in a section (every part when the score has no sections)
    std::vector<const Part*> parts;
    for (const Part* p : ms->parts()) {
        if (data.sections.empty() || ctx.sectionsOfPart.count(idText(p))) {
            parts.push_back(p);
        }
    }
    for (const Part* p : parts) {
        if (!auditIsDrums(p)) {
            ctx.lines[idText(p)] = auditLineOf(p, ctx.measures, auditSlurTicks(ms, p), false);
        }
    }

    // Horn parts: in a section that isn't the lead sheet or the rhythm section
    std::set<QString> hornPartIds;
    std::vector<const Part*> hornParts;
    std::vector<std::pair<const StarScoreSection*, std::vector<const Part*> > > linesBySection;   // one line per chair
    for (const StarScoreSection& s : data.sections) {
        if (auditIsLeadOrRhythm(s)) {
            continue;
        }
        std::vector<const Part*> sectionLines;
        if (auditIsAny(s)) {
            const auto groups = auditChairGroups(ctx, s);
            if (auditHasVersions(groups)) {
                // label the versions by their part books ("3-Horn Arr: Horn 2 in Eb")
                static const QRegularExpression bookRe("Horn\\s*\\d+", QRegularExpression::CaseInsensitiveOption);
                for (const Excerpt* ex : ms->excerpts()) {
                    const QString exName = ex->name().toQString();
                    if (!bookRe.match(exName).hasMatch()) {
                        continue;
                    }
                    for (const Part* p : auditMasterPartsOf(ms, ex)) {
                        const QString pid = idText(p);
                        if (s.partIds.contains(pid) && !ctx.labelOverride.count(pid)) {
                            ctx.labelOverride[pid] = QString("%1 (%2)").arg(exName, p->partName().toQString());
                        }
                    }
                }
            }
            for (const auto& group : groups) {
                sectionLines.push_back(group.front());
            }
        }
        for (const QString& pid : s.partIds) {
            const Part* p = ms->partById(ID(pid));
            if (!p || auditIsDrums(p) || !ctx.lines.count(pid)) {
                continue;
            }
            if (!hornPartIds.count(pid)) {
                hornPartIds.insert(pid);
                hornParts.push_back(p);
            }
            if (!auditIsAny(s) && !s.alternates.count(pid)) {   // a stand-in version isn't a line of its own
                sectionLines.push_back(p);
            }
        }
        linesBySection.push_back({ &s, sectionLines });
    }

    std::vector<StarScoreAuditIssue> issues;
    AuditIssueBuilder add { ctx, issues };

    // 1: "Any Horns" versions in different keys
    for (const StarScoreSection& s : data.sections) {
        if (!auditIsLeadOrRhythm(s) && auditIsAny(s)) {
            const auto groups = auditChairGroups(ctx, s);
            if (auditHasVersions(groups)) {
                auditAnyKeys(ctx, add, groups);
            }
        }
        // Stand-in versions (7-Horn Baritone and Bass Saxophone) against the part they stand in for
        if (!s.alternates.empty()) {
            std::map<QString, std::vector<const Part*> > byMain;
            for (const auto& [alt, main] : s.alternates) {
                const Part* a = ms->partById(ID(alt));
                const Part* m = ms->partById(ID(main));
                if (!a || !m || !ctx.lines.count(alt) || !ctx.lines.count(main)) {
                    continue;
                }
                auto& group = byMain[main];
                if (group.empty()) {
                    group.push_back(m);
                }
                group.push_back(a);
            }
            std::vector<std::vector<const Part*> > groups;
            for (auto& [main, group] : byMain) {
                groups.push_back(group);
            }
            auditAnyKeys(ctx, add, groups);
        }
    }

    // 2: against the reference section, and against the lead sheet melody
    const std::vector<const StarScoreSection*> hornSections = auditHornSections(data.sections);
    const StarScoreSection* refSection = nullptr;
    for (const StarScoreSection* s : hornSections) {
        report.referenceChoiceIds << s->id;
        report.referenceChoiceNames << s->name;
        if (s->id == data.auditReferenceSectionId) {
            refSection = s;
        }
    }
    if (!refSection) {
        for (const StarScoreSection* s : hornSections) {
            if (!auditIsAny(*s)) {
                refSection = s;   // the biggest standard horn section
                break;
            }
        }
    }
    if (refSection) {
        report.referenceSectionId = refSection->id;
        auditReference(ctx, add, *refSection, linesBySection);
    }

    const Part* lead = nullptr;
    for (const StarScoreSection& s : data.sections) {
        if (s.templateKey == "lead-sheet" && !s.partIds.isEmpty()) {
            lead = ms->partById(ID(s.partIds.front()));
            break;
        }
    }
    if (lead && !auditIsDrums(lead)) {
        auditMelody(ctx, add, lead, linesBySection);
    }

    // 3: structure (every part); keys are compared with the lead sheet, or the first pitched part
    const Part* keyReference = lead && !auditIsDrums(lead) ? lead : nullptr;
    for (size_t i = 0; !keyReference && i < parts.size(); ++i) {
        if (!auditIsDrums(parts[i])) {
            keyReference = parts[i];
        }
    }
    auditStructure(ctx, add, parts, hornPartIds, keyReference);

    // 5: range, voice order, dynamics
    auditRange(ctx, add, hornParts);
    for (const StarScoreVoiceSection& vs : checkVoiceOrderIn(ms, data)) {
        QString sectionId;
        for (const StarScoreSection& s : data.sections) {
            if (s.name == vs.section) {
                sectionId = s.id;
            }
        }
        if (sectionId.isEmpty() || vs.section.contains("all keys", Qt::CaseInsensitive)) {
            continue;
        }
        for (const StarScoreVoiceRule& rule : vs.rules) {
            if (!rule.strict || !ctx.lines.count(rule.lowerPartId) || !ctx.lines.count(rule.upperPartId)) {
                continue;
            }
            const QString upper = add.shortLabel(rule.upperPartId);
            std::vector<std::pair<AuditDiff, QString> > crossed(ctx.measures.size());
            std::vector<std::pair<AuditDiff, QString> > doubled(ctx.measures.size());
            for (int b = 0; b < int(rule.bars.size()) && b < int(ctx.measures.size()); ++b) {
                if (rule.bars[b] == 1) {
                    crossed[b] = { AuditDiff::Notes, muse::qtrc("starscore", "Goes above %1").arg(upper) };
                } else if (rule.bars[b] == 2) {
                    const bool prev = b > 0 && rule.bars[b - 1] == 2;
                    const bool next = b + 1 < int(rule.bars.size()) && rule.bars[b + 1] == 2;
                    if (!prev && !next) {
                        doubled[b] = { AuditDiff::Marks, muse::qtrc("starscore", "Plays the same note as %1").arg(upper) };
                    }
                }
            }
            for (const AuditRun& run : auditRuns(crossed)) {
                add.add("crossing", 1, rule.lowerPartId, rule.upperPartId, run.from, run.to,
                        muse::qtrc("starscore", "Voices cross"), run.message);
            }
            for (const AuditRun& run : auditRuns(doubled)) {
                add.add("doubling", 0, rule.lowerPartId, rule.upperPartId, run.from, run.to,
                        muse::qtrc("starscore", "Unison for one bar"), run.message);
            }
        }
    }
    auditDynamics(ctx, add, hornParts);

    // Intentional marks
    const std::set<QString> intentional(data.auditIntentional.begin(), data.auditIntentional.end());
    for (StarScoreAuditIssue& issue : issues) {
        issue.intentional = intentional.count(issue.key) > 0;
    }
    std::stable_sort(issues.begin(), issues.end(), [](const StarScoreAuditIssue& a, const StarScoreAuditIssue& b) {
        if (a.bar != b.bar) {
            return a.bar < b.bar;
        }
        return a.severity > b.severity;
    });
    report.issues = std::move(issues);

    // Arrangements: open issues and "audited" (with a fingerprint of their music)
    for (const StarScoreArrangement& arr : data.arrangements) {
        StarScoreAuditArrangementState st;
        st.id = arr.id;
        st.name = arr.name;
        for (const StarScoreAuditIssue& issue : report.issues) {
            if (issue.intentional) {
                continue;
            }
            const bool mine = std::any_of(issue.sectionIds.begin(), issue.sectionIds.end(),
                                          [&](const QString& sid) { return arr.sectionIds.contains(sid); });
            if (mine && issue.severity >= 1) {
                ++st.openIssues;
                st.likelyErrors += issue.severity == 2 ? 1 : 0;
            }
        }
        const QString fingerprint = auditFingerprint(auditArrangementParts(arr, data.sections), ctx.lines);
        auto ait = data.auditAudited.find(arr.id);
        if (ait != data.auditAudited.end()) {
            st.audited = true;
            st.auditedDate = ait->second.first;
            st.changedSinceAudit = ait->second.second != fingerprint;
        }
        report.arrangements.push_back(st);
    }

    // Listen-through: each horn section at each rehearsal mark where it plays
    std::vector<std::pair<int, QString> > marks;   // bar index, text
    for (int b = 0; b < int(ctx.measures.size()); ++b) {
        const Measure* m = ctx.measures[b];
        bool found = false;
        for (const Segment* s = m->first(); s && !found; s = s->next()) {
            for (const EngravingItem* ann : s->annotations()) {
                if (ann->isRehearsalMark()) {
                    marks.push_back({ b, toRehearsalMark(ann)->plainText().toQString().trimmed() });
                    found = true;
                    break;
                }
            }
        }
    }
    std::vector<std::tuple<int, int, QString> > ranges;
    if (marks.empty() || marks.front().first > 0) {
        ranges.push_back({ 0, (marks.empty() ? int(ctx.measures.size()) : marks.front().first) - 1,
                           marks.empty() ? muse::qtrc("starscore", "Whole song") : muse::qtrc("starscore", "Start") });
    }
    for (size_t i = 0; i < marks.size(); ++i) {
        const int end = i + 1 < marks.size() ? marks[i + 1].first - 1 : int(ctx.measures.size()) - 1;
        ranges.push_back({ marks[i].first, end, marks[i].second });
    }
    const std::set<QString> listened(data.auditListened.begin(), data.auditListened.end());
    for (const auto& [from, to, label] : ranges) {
        if (to < from) {
            continue;
        }
        for (const StarScoreSection* s : hornSections) {
            bool plays = false;
            QString sig;
            for (const QString& pid : s->partIds) {
                auto it = ctx.lines.find(pid);
                if (it == ctx.lines.end()) {
                    continue;
                }
                sig += pid + ":";
                for (int b = from; b <= to; ++b) {
                    plays = plays || !it->second[b].empty();
                    sig += auditBarSig(it->second[b]) + "#";
                }
            }
            if (!plays) {
                continue;
            }
            StarScoreListenStep step;
            step.rehearsal = label;
            step.startBar = from + 1;
            step.endBar = to + 1;
            step.sectionId = s->id;
            step.sectionName = s->name;
            step.key = QString("listen|%1|%2-%3|%4").arg(s->id).arg(step.startBar).arg(step.endBar).arg(auditHash(sig));
            step.approved = listened.count(step.key) > 0;
            report.listen.push_back(step);
        }
    }

    return report;
}

// ---------------------------------------------------------------------------
//  The current score
// ---------------------------------------------------------------------------

//! The main score (not a solo transcription that happens to be showing)
static MasterScore* auditMainScore(const std::shared_ptr<INotationProject>& mainProject, MasterScore* current)
{
    if (mainProject && mainProject->masterNotation()) {
        return mainProject->masterNotation()->masterScore();
    }
    return current;
}

StarScoreAuditReport StarScoreService::audit() const
{
    MasterScore* ms = auditMainScore(m_mainProject, masterScore());
    return auditScore(ms, loadFrom(ms));
}

void StarScoreService::setAuditReferenceSection(const QString& sectionId)
{
    MasterScore* ms = auditMainScore(m_mainProject, masterScore());
    Data data = loadFrom(ms);
    data.auditReferenceSectionId = sectionId;
    storeTo(ms, data, m_mainProject ? m_mainProject : globalContext()->currentProject());
}

void StarScoreService::setAuditIntentional(const QString& issueKey, bool intentional)
{
    MasterScore* ms = auditMainScore(m_mainProject, masterScore());
    Data data = loadFrom(ms);
    data.auditIntentional.removeAll(issueKey);
    if (intentional) {
        data.auditIntentional << issueKey;
    }
    storeTo(ms, data, m_mainProject ? m_mainProject : globalContext()->currentProject());
}

void StarScoreService::setArrangementAudited(const QString& arrangementId, bool audited)
{
    MasterScore* ms = auditMainScore(m_mainProject, masterScore());
    Data data = loadFrom(ms);
    if (!audited) {
        data.auditAudited.erase(arrangementId);
    } else {
        std::vector<const Measure*> measures;
        for (const Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
            measures.push_back(m);
        }
        std::set<QString> partIds;
        for (const StarScoreArrangement& arr : data.arrangements) {
            if (arr.id == arrangementId) {
                partIds = auditArrangementParts(arr, data.sections);
            }
        }
        std::map<QString, AuditLine> lines;
        for (const QString& pid : partIds) {
            const Part* p = ms->partById(ID(pid));
            if (p && !auditIsDrums(p)) {
                lines[pid] = auditLineOf(p, measures, auditSlurTicks(ms, p), false);
            }
        }
        const QString fingerprint = auditFingerprint(partIds, lines);
        data.auditAudited[arrangementId] = { QDate::currentDate().toString(Qt::ISODate), fingerprint };
    }
    storeTo(ms, data, m_mainProject ? m_mainProject : globalContext()->currentProject());
}

void StarScoreService::setListenApproved(const QString& stepKey, bool approved)
{
    MasterScore* ms = auditMainScore(m_mainProject, masterScore());
    Data data = loadFrom(ms);
    data.auditListened.removeAll(stepKey);
    if (approved) {
        data.auditListened << stepKey;
    }
    storeTo(ms, data, m_mainProject ? m_mainProject : globalContext()->currentProject());
}

//! Main score: select bars from..to (1-based) on the staves of the given parts, showing them if hidden
static void auditSelectBars(const IMasterNotationPtr& master, MasterScore* ms, const std::vector<const Part*>& parts, int fromBar,
                            int toBar, bool showHidden)
{
    Measure* first = nullptr;
    Measure* last = nullptr;
    int i = 1;
    for (Measure* m = ms->firstMeasure(); m; m = m->nextMeasure(), ++i) {
        if (i == fromBar) {
            first = m;
        }
        if (i == toBar) {
            last = m;
        }
    }
    if (!first || parts.empty()) {
        return;
    }
    if (!last) {
        last = first;
    }

    if (showHidden) {
        std::vector<std::pair<ID, bool> > show;
        for (const Part* p : parts) {
            if (!p->show()) {
                show.push_back({ p->id(), true });
            }
        }
        if (!show.empty()) {
            master->parts()->setPartsVisible(show, TranslatableString::untranslatable("Show instrument"));
        }
    }

    staff_idx_t top = muse::nidx;
    staff_idx_t bottom = 0;
    for (const Part* p : parts) {
        for (const Staff* st : p->staves()) {
            top = std::min(top, st->idx());
            bottom = std::max(bottom, st->idx());
        }
    }
    INotationInteractionPtr interaction = master->notation()->interaction();
    interaction->clearSelection();
    interaction->select({ first }, SelectType::RANGE, top);
    interaction->select({ last }, SelectType::RANGE, bottom);
    interaction->showItem(first, int(top));
}

void StarScoreService::showAuditIssue(const StarScoreAuditIssue& issue)
{
    if (isSoloProject(globalContext()->currentProject().get())) {
        showMainScore();
    }
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    MasterScore* ms = masterScore();
    if (!master || !ms) {
        return;
    }
    std::vector<const Part*> parts;
    for (const QString& pid : { issue.partId, issue.otherPartId }) {
        if (const Part* p = pid.isEmpty() ? nullptr : ms->partById(ID(pid))) {
            parts.push_back(p);
        }
    }
    globalContext()->setCurrentNotation(master->notation());
    auditSelectBars(master, ms, parts, issue.bar, issue.endBar, !m_listening);
}

// ---------------------------------------------------------------------------
//  Listen-through
// ---------------------------------------------------------------------------

void StarScoreService::playListenStep(const StarScoreListenStep& step, bool withRhythmSection)
{
    if (isSoloProject(globalContext()->currentProject().get())) {
        showMainScore();
    }
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    MasterScore* ms = masterScore();
    if (!master || !ms) {
        return;
    }
    globalContext()->setCurrentNotation(master->notation());
    if (playbackController()->isPlaying()) {
        dispatcher()->dispatch("stop");
    }

    const Data data = loadFrom(ms);
    if (!m_listening) {
        m_listenVisibility.clear();
        for (const Part* p : ms->parts()) {
            m_listenVisibility[idText(p)] = p->show();
        }
        m_listening = true;
    }

    // What plays: the section (one version of each chair when an "Any Horns" section holds every key)
    std::set<QString> play;
    for (const StarScoreSection& s : data.sections) {
        if (s.id != step.sectionId) {
            continue;
        }
        if (auditIsAny(s)) {
            AuditContext ctx;
            ctx.ms = ms;
            for (const Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
                ctx.measures.push_back(m);
            }
            for (const QString& pid : s.partIds) {
                const Part* p = ms->partById(ID(pid));
                if (p && !auditIsDrums(p)) {
                    ctx.lines[pid] = auditLineOf(p, ctx.measures, {}, false);
                }
            }
            for (const auto& group : auditChairGroups(ctx, s)) {
                play.insert(idText(group.front()));
            }
        } else {
            for (const QString& pid : s.partIds) {
                if (!s.alternates.count(pid)) {   // stand-in versions don't play along
                    play.insert(pid);
                }
            }
        }
    }
    if (withRhythmSection) {
        for (const StarScoreSection& s : data.sections) {
            if (!(s.templateKey == "rhythm" || s.templateKey.endsWith("-rhythm"))) {
                continue;
            }
            QStringList wanted;
            for (const QString& pid : s.partIds) {
                if (m_listenVisibility[pid]) {
                    wanted << pid;
                }
            }
            if (wanted.isEmpty()) {
                wanted = s.shownPartIds.isEmpty() ? s.partIds : s.shownPartIds;
            }
            play.insert(wanted.begin(), wanted.end());
        }
    }

    std::vector<std::pair<ID, bool> > changes;
    std::vector<const Part*> playParts;
    for (const Part* p : ms->parts()) {
        const bool want = play.count(idText(p)) > 0;
        if (want) {
            playParts.push_back(p);
        }
        if (p->show() != want) {
            changes.push_back({ p->id(), want });
        }
    }
    if (!changes.empty()) {
        master->parts()->setPartsVisible(changes, TranslatableString::untranslatable("Listen"));
    }

    // Select the section's bars across the playing instruments (range playback plays just those) and play
    auditSelectBars(master, ms, playParts, step.startBar, step.endBar, false);
    int bar = 1;
    m_listenStartTick = -1;
    m_listenEndTick = -1;
    for (const Measure* m = ms->firstMeasure(); m; m = m->nextMeasure(), ++bar) {
        if (bar == step.startBar) {
            m_listenStartTick = m->tick().ticks();
        }
        if (bar == step.endBar) {
            m_listenEndTick = m->endTick().ticks();
        }
    }
    m_listeningChanged.notify();

    QTimer::singleShot(150, [this]() {
        if (m_listening && !playbackController()->isPlaying()) {
            dispatcher()->dispatch("play");
        }
    });
}

void StarScoreService::onPlaybackPosition(int tick)
{
    if (!m_listening || m_listenEndTick < 0 || !playbackController()->isPlaying()) {
        return;
    }
    if (tick >= m_listenEndTick || tick < m_listenStartTick - Constants::DIVISION) {
        m_listenEndTick = -1;
        dispatcher()->dispatch("stop");
    }
}

void StarScoreService::stopListening()
{
    if (!m_listening) {
        return;
    }
    if (playbackController()->isPlaying()) {
        dispatcher()->dispatch("stop");
    }
    IMasterNotationPtr master = m_mainProject ? m_mainProject->masterNotation() : globalContext()->currentMasterNotation();
    MasterScore* ms = master ? master->masterScore() : nullptr;
    if (ms) {
        std::vector<std::pair<ID, bool> > changes;
        for (const Part* p : ms->parts()) {
            auto it = m_listenVisibility.find(idText(p));
            if (it != m_listenVisibility.end() && p->show() != it->second) {
                changes.push_back({ p->id(), it->second });
            }
        }
        if (!changes.empty()) {
            master->parts()->setPartsVisible(changes, TranslatableString::untranslatable("Stop listening"));
        }
    }
    m_listening = false;
    m_listenVisibility.clear();
    m_listenStartTick = -1;
    m_listenEndTick = -1;
    m_listeningChanged.notify();
}

bool StarScoreService::isListening() const
{
    return m_listening;
}

async::Notification StarScoreService::listeningChanged() const
{
    return m_listeningChanged;
}

// ---------------------------------------------------------------------------
//  Library audit
// ---------------------------------------------------------------------------

static QString auditCachePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + "/starscore-audit-library.json";
}

static QJsonObject auditReadCache()
{
    QFile f(auditCachePath());
    if (!f.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(f.readAll()).object();
}

static void auditWriteCache(const QJsonObject& cache)
{
    QFile f(auditCachePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(cache).toJson(QJsonDocument::Compact));
    }
}

// Cache entries from before the dashboard (no arrangement list) are read again
static const int AUDIT_CACHE_VERSION = 2;

static QJsonObject auditSummaryToJson(const StarScoreAuditFileSummary& s)
{
    QJsonArray arrs;
    for (const StarScoreFileArrangement& a : s.arrangementList) {
        arrs.append(QJsonObject {
            { "col", a.column }, { "name", a.name }, { "status", a.status },
            { "unfinished", QJsonArray::fromStringList(a.unfinished) }, { "audited", a.audited },
            { "changed", a.changedSinceAudit }, { "date", a.auditedDate }, { "open", a.openIssues }
        });
    }
    return QJsonObject {
        { "title", s.title }, { "open", s.openIssues }, { "likely", s.likelyErrors }, { "arr", s.arrangements },
        { "audited", s.arrangementsAudited }, { "changed", s.arrangementsChanged }, { "steps", s.listenSteps },
        { "approved", s.listenApproved }, { "error", s.error }, { "arrs", arrs }, { "v", AUDIT_CACHE_VERSION }
    };
}

static StarScoreAuditFileSummary auditSummaryFromJson(const QString& path, const QJsonObject& o)
{
    StarScoreAuditFileSummary s;
    s.path = path;
    s.title = o.value("title").toString();
    s.openIssues = o.value("open").toInt();
    s.likelyErrors = o.value("likely").toInt();
    s.arrangements = o.value("arr").toInt();
    s.arrangementsAudited = o.value("audited").toInt();
    s.arrangementsChanged = o.value("changed").toInt();
    s.listenSteps = o.value("steps").toInt();
    s.listenApproved = o.value("approved").toInt();
    s.error = o.value("error").toString();
    for (const QJsonValue& v : o.value("arrs").toArray()) {
        const QJsonObject a = v.toObject();
        StarScoreFileArrangement fa;
        fa.column = a.value("col").toString();
        fa.name = a.value("name").toString();
        fa.status = a.value("status").toInt();
        for (const QJsonValue& u : a.value("unfinished").toArray()) {
            fa.unfinished << u.toString();
        }
        fa.audited = a.value("audited").toBool();
        fa.changedSinceAudit = a.value("changed").toBool();
        fa.auditedDate = a.value("date").toString();
        fa.openIssues = a.value("open").toInt();
        s.arrangementList.push_back(fa);
    }
    return s;
}

static bool auditCacheCurrent(const QJsonObject& entry, const QString& stamp)
{
    return entry.value("stamp").toString() == stamp && entry.value("v").toInt() == AUDIT_CACHE_VERSION;
}

//! Which dashboard column an arrangement fills: "3H" (3-Horn Standard), "3F" (3-Horn Flexible), …
static QString auditArrangementColumn(const StarScoreArrangement& a)
{
    static const QRegularExpression keyRe("^(\\d)-horn-(standard|any)$");
    const QRegularExpressionMatch km = keyRe.match(a.templateKey);
    if (km.hasMatch()) {
        return km.captured(1) + (km.captured(2) == "any" ? "F" : "H");
    }
    static const QRegularExpression nameRe("^\\s*(\\d)-Horn\\s+(Standard|Arrangement|Section|Flexible|Any)\\b",
                                           QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch nm = nameRe.match(a.name);
    if (nm.hasMatch()) {
        const QString kind = nm.captured(2).toLower();
        return nm.captured(1) + ((kind == "flexible" || kind == "any") ? "F" : "H");
    }
    return QString();
}

static QString auditStatusName(int status)
{
    switch (status) {
    case 0: return muse::qtrc("starscore", "Empty");
    case 1: return muse::qtrc("starscore", "Sketch");
    case 2: return muse::qtrc("starscore", "In progress");
    case 3: return muse::qtrc("starscore", "Needs review");
    default: return muse::qtrc("starscore", "Finished");
    }
}

std::vector<StarScoreFileArrangement> StarScoreService::summarizeArrangements(const Data& data,
                                                                              const StarScoreAuditReport& report) const
{
    std::vector<StarScoreFileArrangement> out;
    for (const StarScoreArrangement& a : data.arrangements) {
        StarScoreFileArrangement fa;
        fa.column = auditArrangementColumn(a);
        fa.name = a.name;
        int least = int(StarScoreStatus::Finished);
        for (const QString& sid : a.sectionIds) {
            for (const StarScoreSection& sec : data.sections) {
                if (sec.id != sid) {
                    continue;
                }
                const int st = int(sec.status);
                least = std::min(least, st);
                if (st < int(StarScoreStatus::Finished)) {
                    fa.unfinished << QString("%1: %2").arg(sec.name, auditStatusName(st));
                }
            }
        }
        fa.status = a.sectionIds.isEmpty() ? 0 : least;
        for (const StarScoreAuditArrangementState& st : report.arrangements) {
            if (st.id == a.id) {
                fa.audited = st.audited && !st.changedSinceAudit;
                fa.changedSinceAudit = st.audited && st.changedSinceAudit;
                fa.auditedDate = st.auditedDate;
                fa.openIssues = st.openIssues;
            }
        }
        out.push_back(fa);
    }
    return out;
}

static StarScoreAuditFileSummary auditSummarize(const QString& path, const StarScoreAuditReport& report)
{
    StarScoreAuditFileSummary s;
    s.path = path;
    s.title = QFileInfo(path).completeBaseName();
    for (const StarScoreAuditIssue& issue : report.issues) {
        if (!issue.intentional && issue.severity >= 1) {
            ++s.openIssues;
            s.likelyErrors += issue.severity == 2 ? 1 : 0;
        }
    }
    s.arrangements = int(report.arrangements.size());
    for (const StarScoreAuditArrangementState& a : report.arrangements) {
        if (a.audited && !a.changedSinceAudit) {
            ++s.arrangementsAudited;
        } else if (a.audited) {
            ++s.arrangementsChanged;
        }
    }
    s.listenSteps = int(report.listen.size());
    for (const StarScoreListenStep& step : report.listen) {
        s.listenApproved += step.approved ? 1 : 0;
    }
    return s;
}

static QString auditFileStamp(const QFileInfo& fi)
{
    return QString("%1:%2").arg(fi.lastModified().toMSecsSinceEpoch()).arg(fi.size());
}

QString StarScoreService::auditLibraryFolder() const
{
    const QString saved = QSettings().value("StarScore/auditLibraryFolder").toString();
    if (!saved.isEmpty() && QDir(saved).exists()) {
        return saved;
    }
    const QString band = bandFolder();
    if (!band.isEmpty()) {
        const QString sibling = QFileInfo(band).absolutePath() + "/Projects and Sheets";
        if (QDir(sibling).exists()) {
            return sibling;
        }
    }
    return QString();
}

void StarScoreService::setAuditLibraryFolder(const QString& path)
{
    QSettings().setValue("StarScore/auditLibraryFolder", path);
}

QStringList StarScoreService::auditLibraryFiles(const QString& folder) const
{
    static const QStringList SKIP { "Version History", "MuseScore Files", "Quarantine", "Templates", "Z Empty Folders", "Superseded" };
    QStringList files;
    if (folder.isEmpty()) {
        return files;
    }
    // The band's songs: only "1 Starsign Originals" and "2 Starsign Covers" when the folder has them
    QStringList roots;
    for (const QString& sub : { QString("1 Starsign Originals"), QString("2 Starsign Covers") }) {
        if (QDir(folder + "/" + sub).exists()) {
            roots << folder + "/" + sub;
        }
    }
    if (roots.isEmpty()) {
        roots << folder;
    }
    for (const QString& root : roots) {
    QDirIterator it(root, { "*.starscore" }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString rel = QDir(folder).relativeFilePath(path);
        if (std::any_of(SKIP.begin(), SKIP.end(), [&](const QString& s) { return rel.contains(s); })
            || QFileInfo(path).fileName().startsWith(".")) {
            continue;
        }
        files << path;
    }
    }
    std::sort(files.begin(), files.end(), [](const QString& a, const QString& b) {
        return QString::localeAwareCompare(a, b) < 0;
    });
    return files;
}

StarScoreAuditFileSummary StarScoreService::auditFile(const QString& path, bool force)
{
    const QFileInfo fi(path);

    // The open score: its live state
    if (m_mainProject && QFileInfo(m_mainProject->path().toQString()).absoluteFilePath() == fi.absoluteFilePath()) {
        const StarScoreAuditReport report = audit();
        StarScoreAuditFileSummary live = auditSummarize(path, report);
        live.arrangementList = summarizeArrangements(loadFrom(m_mainProject->masterNotation()->masterScore()), report);
        return live;
    }

    QJsonObject cache = auditReadCache();
    const QString stamp = auditFileStamp(fi);
    if (!force) {
        const QJsonObject entry = cache.value(path).toObject();
        if (auditCacheCurrent(entry, stamp)) {
            return auditSummaryFromJson(path, entry);
        }
    }

    StarScoreAuditFileSummary summary;
    summary.path = path;
    summary.title = fi.completeBaseName();

    // Read a copy (a .starscore is a MuseScore file) so the original is never touched
    const QString tmp = QDir::tempPath() + "/StarScoreAudit-" + QUuid::createUuid().toString(QUuid::Id128) + ".mscz";
    if (!QFile::copy(path, tmp)) {
        summary.error = muse::qtrc("starscore", "Couldn't read the file");
    } else {
        INotationProjectPtr project = projectCreator()->newProject(iocContext());
        const Ret ret = project->load(io::path_t(tmp));
        if (!ret || !project->masterNotation() || !project->masterNotation()->masterScore()) {
            summary.error = muse::qtrc("starscore", "Couldn't open the file: %1").arg(QString::fromStdString(ret.toString()));
        } else {
            MasterScore* ms = project->masterNotation()->masterScore();
            const Data fileData = loadFrom(ms);
            const StarScoreAuditReport report = auditScore(ms, fileData);
            summary = auditSummarize(path, report);
            summary.arrangementList = summarizeArrangements(fileData, report);
        }
        QFile::remove(tmp);
    }

    QJsonObject entry = auditSummaryToJson(summary);
    entry["stamp"] = stamp;
    cache[path] = entry;
    auditWriteCache(cache);
    return summary;
}

std::vector<StarScoreAuditFileSummary> StarScoreService::cachedLibraryAudit(const QString& folder) const
{
    std::vector<StarScoreAuditFileSummary> out;
    const QJsonObject cache = auditReadCache();
    for (const QString& path : auditLibraryFiles(folder)) {
        const QJsonObject entry = cache.value(path).toObject();
        if (!entry.isEmpty() && auditCacheCurrent(entry, auditFileStamp(QFileInfo(path)))) {
            out.push_back(auditSummaryFromJson(path, entry));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
//  Audit all songs: one song after another, each opened with the Audit panel
// ---------------------------------------------------------------------------

static const char* AUDIT_WALK_PATHS = "StarScore/auditWalk/paths";
static const char* AUDIT_WALK_INDEX = "StarScore/auditWalk/index";

void StarScoreService::startAuditWalk(const QStringList& paths)
{
    if (paths.isEmpty()) {
        return;
    }
    QSettings settings;
    settings.setValue(AUDIT_WALK_PATHS, paths);
    settings.setValue(AUDIT_WALK_INDEX, 0);
    m_changed.notify();
    openAuditWalkSong();
}

bool StarScoreService::auditWalkActive() const
{
    return !auditWalkPaths().isEmpty();
}

int StarScoreService::auditWalkIndex() const
{
    // The song that's open, if it's one of the walk's songs (opening can be cancelled at the save prompt)
    const QStringList paths = auditWalkPaths();
    auto current = globalContext() ? globalContext()->currentProject() : nullptr;
    if (current) {
        const QString open = QFileInfo(current->path().toQString()).absoluteFilePath();
        for (int i = 0; i < int(paths.size()); ++i) {
            if (QFileInfo(paths.at(i)).absoluteFilePath() == open) {
                return i;
            }
        }
    }
    return std::clamp(QSettings().value(AUDIT_WALK_INDEX, 0).toInt(), 0, std::max(0, int(auditWalkPaths().size()) - 1));
}

QStringList StarScoreService::auditWalkPaths() const
{
    return QSettings().value(AUDIT_WALK_PATHS).toStringList();
}

void StarScoreService::auditWalkStep(int delta)
{
    const QStringList paths = auditWalkPaths();
    if (paths.isEmpty()) {
        return;
    }
    const int next = auditWalkIndex() + delta;
    if (next < 0) {
        return;
    }
    if (next >= int(paths.size())) {
        stopAuditWalk();
        return;
    }
    QSettings().setValue(AUDIT_WALK_INDEX, next);
    m_changed.notify();
    openAuditWalkSong();
}

void StarScoreService::stopAuditWalk()
{
    QSettings settings;
    settings.remove(AUDIT_WALK_PATHS);
    settings.remove(AUDIT_WALK_INDEX);
    m_changed.notify();
}

void StarScoreService::openAuditWalkSong()
{
    // The stored place, not the open song: when the walk starts or moves, the open song is the one being left
    const QStringList paths = auditWalkPaths();
    const int index = QSettings().value(AUDIT_WALK_INDEX, 0).toInt();
    if (index < 0 || index >= int(paths.size())) {
        return;
    }
    const QString path = paths.at(index);
    auto current = globalContext() ? globalContext()->currentProject() : nullptr;
    if (current && QFileInfo(current->path().toQString()).absoluteFilePath() == QFileInfo(path).absoluteFilePath()) {
        // Already open: just make sure the Audit panel (with the walk strip) is showing
        auto d = dispatcher();
        auto docks = dockWindowProvider();
        QTimer::singleShot(0, qApp, [d, docks]() {
            if (docks && docks->window() && !docks->window()->isDockOpen("starscoreAuditPanel")) {
                d->dispatch("toggle-starscore-audit");
            }
        });
        return;
    }
    // After the current action has finished: opening a file closes the current one (asking to save if needed)
    auto d = dispatcher();
    auto docks = dockWindowProvider();
    QTimer::singleShot(0, qApp, [d, path]() {
        d->dispatch("starscore-audit-open", muse::actions::ActionData::make_arg1<QUrl>(QUrl::fromLocalFile(path)));
    });
    QTimer::singleShot(2000, qApp, [d, docks]() {
        if (docks && docks->window() && !docks->window()->isDockOpen("starscoreAuditPanel")) {
            d->dispatch("toggle-starscore-audit");
        }
    });
}
