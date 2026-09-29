/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — "Compare parts": for each instrument that appears in more than one section,
 * which bars differ between its parts.
 */
#include "starscoreservice.h"

#include <algorithm>
#include <map>
#include <set>

#include <QRegularExpression>
#include <QStringList>

#include "engraving/dom/masterscore.h"
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
#include "engraving/dom/textbase.h"

#include "notation/inotationinteraction.h"
#include "notation/inotationelements.h"

#include "translation.h"
#include "log.h"

using namespace mu::project;
using namespace mu::engraving;
using namespace mu::notation;
using namespace muse;

//! What a part plays in one bar, as text: rhythm, pitches (concert), ties, articulations and dynamics.
//! Empty when the bar is only rests.
static std::string starscoreBarSignature(const Part* part, const Measure* m, bool ignoreOctave = false)
{
    std::string sig;
    bool hasNotes = false;
    for (const Staff* staff : part->staves()) {
        const track_idx_t startTrack = staff->idx() * VOICES;
        for (track_idx_t track = startTrack; track < startTrack + VOICES; ++track) {
            for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
                const EngravingItem* e = s->element(track);
                if (!e) {
                    continue;
                }
                sig += "|" + std::to_string(track - startTrack) + "@" + std::to_string(s->rtick().ticks());
                const ChordRest* cr = toChordRest(e);
                sig += "d" + std::to_string(cr->actualTicks().ticks());
                if (e->isChord()) {
                    hasNotes = true;
                    const Chord* c = toChord(e);
                    std::vector<int> pitches;
                    for (const Note* n : c->notes()) {
                        const int pitch = ignoreOctave ? n->pitch() % 12 : n->pitch();
                        pitches.push_back(pitch * 2 + (n->tieFor() ? 1 : 0));
                    }
                    std::sort(pitches.begin(), pitches.end());
                    for (int p : pitches) {
                        sig += "n" + std::to_string(p);
                    }
                    for (const Articulation* a : c->articulations()) {
                        sig += "a" + std::to_string(int(a->symId()));
                    }
                    sig += "g" + std::to_string(c->graceNotes().size());
                } else {
                    sig += "r";
                }
                for (const EngravingItem* ann : s->annotations()) {
                    if (ann->track() == track && ann->isDynamic()) {
                        sig += "D" + toDynamic(ann)->plainText().toStdString();
                    }
                }
            }
        }
    }
    return hasNotes ? sig : std::string();
}

//! Instruments count as the same whatever their key: bb-trumpet and c-trumpet are both "trumpet"
static QString starscoreInstrumentFamily(const Part* part)
{
    QString id = part->instrumentId().toQString();
    id.remove(QRegularExpression("^(bb|eb|ab|db|gb|c|f|a|d|g|e|b)-"));
    return id;
}

static QString starscoreBarRanges(const std::vector<int>& bars)
{
    QStringList out;
    size_t i = 0;
    while (i < bars.size()) {
        size_t j = i;
        while (j + 1 < bars.size() && bars[j + 1] == bars[j] + 1) {
            ++j;
        }
        out << (j > i ? QString("%1–%2").arg(bars[i]).arg(bars[j]) : QString::number(bars[i]));
        i = j + 1;
    }
    return out.join(", ");
}

std::vector<StarScoreComparison> StarScoreService::compareParts(const std::map<QString, QString>& referenceByInstrument) const
{
    std::vector<StarScoreComparison> result;
    const MasterScore* ms = masterScore();
    if (!ms) {
        return result;
    }
    const Data data = load();

    // Section names per part, in section order; parts in no section are left out
    std::map<QString, QStringList> sectionsOfPart;
    std::vector<QString> order;
    for (const StarScoreSection& s : data.sections) {
        for (const QString& pid : s.partIds) {
            if (!sectionsOfPart.count(pid)) {
                order.push_back(pid);
            }
            sectionsOfPart[pid] << s.name;
        }
    }

    std::vector<const Measure*> measures;
    for (const Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        measures.push_back(m);
    }

    // Group by instrument family, keeping section order
    std::vector<QString> families;
    std::map<QString, std::vector<const Part*> > byFamily;
    for (const QString& pid : order) {
        const Part* p = ms->partById(ID(pid));
        if (!p || p->instrument()->useDrumset()) {
            continue;
        }
        const QString fam = starscoreInstrumentFamily(p);
        if (!byFamily.count(fam)) {
            families.push_back(fam);
        }
        byFamily[fam].push_back(p);
    }

    for (const QString& fam : families) {
        const std::vector<const Part*>& parts = byFamily[fam];
        if (parts.size() < 2) {
            continue;
        }

        std::vector<std::vector<std::string> > sigs;
        std::vector<std::vector<std::string> > classSigs;   // pitch classes only, to spot octave-only differences
        for (const Part* p : parts) {
            std::vector<std::string> row;
            std::vector<std::string> classRow;
            row.reserve(measures.size());
            for (const Measure* m : measures) {
                row.push_back(starscoreBarSignature(p, m));
                classRow.push_back(starscoreBarSignature(p, m, true));
            }
            sigs.push_back(std::move(row));
            classSigs.push_back(std::move(classRow));
        }

        const QString instrumentName = parts.front()->instrument()->trackName().toQString();
        size_t ref = 0;
        auto refIt = referenceByInstrument.find(instrumentName);
        for (size_t i = 0; refIt != referenceByInstrument.end() && i < parts.size(); ++i) {
            if (idText(parts[i]) == refIt->second) {
                ref = i;
            }
        }

        auto labelOf = [&](size_t i) {
            return QString("%1 — %2").arg(parts[i]->partName().toQString(), sectionsOfPart[idText(parts[i])].join(", "));
        };
        auto shortOf = [&](size_t i) {
            return sectionsOfPart[idText(parts[i])].join(", ");
        };

        StarScoreComparison cmp;
        cmp.instrument = instrumentName;
        cmp.barCount = int(measures.size());

        for (size_t i = 0; i < parts.size(); ++i) {
            StarScoreComparedPart row;
            row.partId = idText(parts[i]);
            row.label = labelOf(i);
            row.isReference = i == ref;

            std::vector<int> differing;
            std::vector<int> octaveOnly;
            for (size_t b = 0; b < measures.size(); ++b) {
                const bool same = sigs[i][b] == sigs[ref][b];
                if (same) {
                    row.bars.push_back(sigs[i][b].empty() ? 2 : 0);
                } else if (!sigs[i][b].empty() && classSigs[i][b] == classSigs[ref][b]) {
                    row.bars.push_back(3);   // same notes and rhythm, different octave
                    octaveOnly.push_back(int(b) + 1);
                } else {
                    row.bars.push_back(1);
                    differing.push_back(int(b) + 1);
                }
            }

            if (i == ref) {
                row.summary = muse::qtrc("starscore", "Reference");
            } else if (differing.empty() && octaveOnly.empty()) {
                row.summary = muse::qtrc("starscore", "Same as %1").arg(shortOf(ref));
            } else if (differing.empty()) {
                row.summary = muse::qtrc("starscore", "Same as %1 apart from the octave in bars %2")
                              .arg(shortOf(ref), starscoreBarRanges(octaveOnly));
            } else {
                // Identical to another non-reference part?
                QString twin;
                for (size_t j = 0; j < i && twin.isEmpty(); ++j) {
                    if (j != ref && sigs[j] == sigs[i]) {
                        twin = shortOf(j);
                    }
                }
                row.summary = muse::qtrc("starscore", "%n bar(s) differ from %1: %2", nullptr, int(differing.size()))
                              .arg(shortOf(ref), starscoreBarRanges(differing));
                if (!octaveOnly.empty()) {
                    row.summary += "; " + muse::qtrc("starscore", "octave only: %1").arg(starscoreBarRanges(octaveOnly));
                }
                if (!twin.isEmpty()) {
                    row.summary += "  " + muse::qtrc("starscore", "(identical to %1)").arg(twin);
                }
            }
            cmp.parts.push_back(std::move(row));
        }
        result.push_back(std::move(cmp));
    }

    return result;
}

void StarScoreService::selectBar(const QString& partId, int bar)
{
    if (isSoloProject(globalContext()->currentProject().get())) {
        showMainScore();
    }
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    MasterScore* ms = masterScore();
    if (!master || !ms) {
        return;
    }
    const Part* part = ms->partById(ID(partId));
    Measure* m = ms->firstMeasure();
    for (int i = 1; m && i < bar; ++i) {
        m = m->nextMeasure();
    }
    if (!part || !m || part->staves().empty()) {
        return;
    }

    // Work in the main score so every section's staff is reachable; show the part if it is hidden
    globalContext()->setCurrentNotation(master->notation());
    if (!part->show()) {
        master->parts()->setPartsVisible({ { part->id(), true } }, TranslatableString::untranslatable("Show instrument"));
    }
    const staff_idx_t staffIdx = part->staves().front()->idx();
    INotationInteractionPtr interaction = master->notation()->interaction();
    interaction->select({ m }, SelectType::RANGE, staffIdx);
    interaction->showItem(m, int(staffIdx));
}

// ---------------------------------------------------------------------------
//  Check voice order
// ---------------------------------------------------------------------------

namespace {
struct VoiceNote {
    int start;
    int end;
    int top;   // highest sounding (concert) pitch
};

struct VoicePart {
    const Part* part = nullptr;
    QString family;   // e.g. "trumpet", "alto-saxophone", "chair"
    int number = 0;   // "Trumpet 2" -> 2; 0 when unnumbered
    QString name;
    std::vector<VoiceNote> notes;
};
}

static std::vector<VoiceNote> starscoreVoiceNotes(const Part* part, const MasterScore* ms)
{
    std::vector<VoiceNote> out;
    for (const Staff* staff : part->staves()) {
        const track_idx_t startTrack = staff->idx() * VOICES;
        for (track_idx_t track = startTrack; track < startTrack + VOICES; ++track) {
            for (const Segment* s = ms->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
                const EngravingItem* e = s->element(track);
                if (!e || !e->isChord()) {
                    continue;
                }
                const Chord* c = toChord(e);
                int top = -1;
                for (const Note* n : c->notes()) {
                    top = std::max(top, n->pitch());
                }
                const int start = s->tick().ticks();
                out.push_back({ start, start + c->actualTicks().ticks(), top });
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const VoiceNote& a, const VoiceNote& b) { return a.start < b.start; });
    return out;
}

//! One staff's notes; "top" is the highest note of each chord, or the lowest when lowest = true
static std::vector<VoiceNote> starscoreStaffNotes(const Staff* staff, const MasterScore* ms, bool lowest)
{
    std::vector<VoiceNote> out;
    const track_idx_t startTrack = staff->idx() * VOICES;
    for (track_idx_t track = startTrack; track < startTrack + VOICES; ++track) {
        for (const Segment* s = ms->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            const EngravingItem* e = s->element(track);
            if (!e || !e->isChord()) {
                continue;
            }
            const Chord* c = toChord(e);
            int pitch = lowest ? 1000 : -1;
            for (const Note* n : c->notes()) {
                pitch = lowest ? std::min(pitch, n->pitch()) : std::max(pitch, n->pitch());
            }
            const int start = s->tick().ticks();
            out.push_back({ start, start + c->actualTicks().ticks(), pitch });
        }
    }
    std::sort(out.begin(), out.end(), [](const VoiceNote& a, const VoiceNote& b) { return a.start < b.start; });
    return out;
}

//! Order of register within a family group; higher value = higher voice
static int starscoreSaxRank(const QString& fam)
{
    if (fam == "soprano-saxophone") {
        return 5;
    } else if (fam == "alto-saxophone") {
        return 4;
    } else if (fam == "tenor-saxophone") {
        return 3;
    } else if (fam == "baritone-saxophone") {
        return 2;
    } else if (fam == "bass-saxophone") {
        return 1;
    }
    return 0;
}

std::vector<StarScoreVoiceSection> StarScoreService::checkVoiceOrder() const
{
    return checkVoiceOrderIn(masterScore(), load());
}

std::vector<StarScoreVoiceSection> StarScoreService::checkVoiceOrderIn(const MasterScore* ms, const Data& data) const
{
    std::vector<StarScoreVoiceSection> result;
    if (!ms) {
        return result;
    }

    int barCount = 0;
    std::vector<int> barStarts;
    for (const Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        barStarts.push_back(m->tick().ticks());
        ++barCount;
    }
    auto barOf = [&](int tick) {
        auto it = std::upper_bound(barStarts.begin(), barStarts.end(), tick);
        return int(it - barStarts.begin()) - 1;   // 0-based
    };

    static const QRegularExpression numberRe("(\\d+)\\s*$");
    static const QRegularExpression chairRe("^\\s*Horn\\s*(\\d+)\\s*$", QRegularExpression::CaseInsensitiveOption);

    for (const StarScoreSection& sec : data.sections) {
        // (1-Horn: one melody sheet per instrument, not voices of one chord)
        if (sec.templateKey == "lead-sheet" || sec.templateKey == "rhythm" || sec.templateKey.endsWith("-rhythm")
            || sec.templateKey == "1-horn" || sec.partIds.size() < 2) {
            continue;
        }

        std::vector<VoicePart> parts;
        for (const QString& pid : sec.partIds) {
            const Part* p = ms->partById(ID(pid));
            if (!p || p->instrument()->useDrumset()) {
                continue;
            }
            VoicePart vp;
            vp.part = p;
            vp.name = p->partName().toQString();
            if (const QRegularExpressionMatch cm = chairRe.match(vp.name); cm.hasMatch()) {
                vp.family = "chair";   // "Any Horns" chairs: Horn 1 above Horn 2 above Horn 3
                vp.number = cm.captured(1).toInt();
            } else {
                vp.family = starscoreInstrumentFamily(p);
                if (vp.family == "clarinet" || vp.family.endsWith("-clarinet")) {
                    vp.family = vp.family == "bass-clarinet" ? QString("bass-clarinet") : QString("clarinet");
                }
                const QRegularExpressionMatch nm = numberRe.match(vp.name);
                vp.number = nm.hasMatch() ? nm.captured(1).toInt() : 0;
            }
            vp.notes = starscoreVoiceNotes(p, ms);
            parts.push_back(std::move(vp));
        }
        if (parts.size() < 2) {
            continue;
        }

        auto isTrumpet = [](const VoicePart& v) { return v.family == "trumpet" || v.family == "flugelhorn" || v.family == "cornet"; };
        auto isLeadTrumpet = [&](const VoicePart& v) { return isTrumpet(v) && v.number <= 1; };
        auto isStrings = [](const QString& f) {
            return f == "violin" || f == "viola" || f == "violoncello" || f == "contrabass";
        };

        // rule for the ordered pair (a above b): 0 = none, 1 = strict, 2 = preferred only
        auto ruleFor = [&](const VoicePart& a, const VoicePart& b) -> int {
            // Numbered parts of one instrument: 1 above 2 (horns in pairs: 1 above 2, 3 above 4)
            if (a.family == b.family && a.number > 0 && b.number > 0 && a.number < b.number) {
                if (a.family == "horn") {
                    return (a.number % 2 == 1 && b.number == a.number + 1) ? 1 : 0;
                }
                return isStrings(a.family) ? 2 : 1;
            }
            // Saxophones: soprano, alto, tenor, baritone, bass
            const int ra = starscoreSaxRank(a.family);
            const int rb = starscoreSaxRank(b.family);
            if (ra && rb && ra > rb) {
                return 1;
            }
            // Trumpet 1 above alto; trumpet or soprano on top (trumpet preferred)
            if (isLeadTrumpet(a) && b.family == "alto-saxophone") {
                return 1;
            }
            if (isLeadTrumpet(a) && b.family == "soprano-saxophone") {
                return 2;
            }
            // Trombone below trumpets, soprano and alto; bass trombone below trombone
            if (b.family == "trombone" && (isTrumpet(a) || a.family == "soprano-saxophone" || a.family == "alto-saxophone")) {
                return 1;
            }
            if (a.family == "trombone" && b.family == "bass-trombone") {
                return 1;
            }
            // Orchestra (preferred only: crossings are common there)
            static const std::vector<QString> STRINGS { "violin", "viola", "violoncello", "contrabass" };
            static const std::vector<QString> WINDS { "piccolo", "flute", "oboe", "clarinet", "bassoon" };
            for (const std::vector<QString>* chain : { &STRINGS, &WINDS }) {
                const auto ia = std::find(chain->begin(), chain->end(), a.family);
                const auto ib = std::find(chain->begin(), chain->end(), b.family);
                if (ia != chain->end() && ib != chain->end() && ib == ia + 1) {
                    return 2;
                }
            }
            if ((a.family == "oboe" && b.family == "english-horn") || (a.family == "clarinet" && b.family == "bass-clarinet")
                || (a.family == "bassoon" && b.family == "contrabassoon")
                || ((a.family == "trombone" || a.family == "bass-trombone") && b.family == "tuba")) {
                return 2;
            }
            return 0;
        };

        StarScoreVoiceSection vs;
        vs.section = sec.name;
        vs.barCount = barCount;

        for (size_t i = 0; i < parts.size(); ++i) {
            for (size_t j = 0; j < parts.size(); ++j) {
                if (i == j) {
                    continue;
                }
                const int kind = ruleFor(parts[i], parts[j]);
                if (!kind) {
                    continue;
                }
                const VoicePart& up = parts[i];
                const VoicePart& lo = parts[j];

                StarScoreVoiceRule rule;
                rule.upperPartId = idText(up.part);
                rule.lowerPartId = idText(lo.part);
                rule.strict = kind == 1;
                rule.label = muse::qtrc("starscore", "%1 above %2").arg(up.name, lo.name)
                             + (rule.strict ? QString() : "  " + muse::qtrc("starscore", "(preferred)"));
                rule.bars.assign(barCount, 4);

                // Sweep the two note lists and compare wherever both sound
                std::vector<int> crossed;
                std::vector<int> doubled;
                size_t a = 0;
                size_t b = 0;
                while (a < up.notes.size() && b < lo.notes.size()) {
                    const VoiceNote& na = up.notes[a];
                    const VoiceNote& nb = lo.notes[b];
                    const int from = std::max(na.start, nb.start);
                    const int to = std::min(na.end, nb.end);
                    if (from < to) {
                        const int bar = barOf(from);
                        if (bar >= 0 && bar < barCount) {
                            int& cell = rule.bars[bar];
                            if (nb.top > na.top) {
                                cell = rule.strict ? 1 : 3;
                            } else if (nb.top == na.top) {
                                if (cell != 1 && cell != 3) {
                                    cell = 2;
                                }
                            } else if (cell == 4) {
                                cell = 0;
                            }
                        }
                    }
                    if (na.end < nb.end) {
                        ++a;
                    } else {
                        ++b;
                    }
                }
                for (int bar = 0; bar < barCount; ++bar) {
                    if (rule.bars[bar] == 1 || rule.bars[bar] == 3) {
                        crossed.push_back(bar + 1);
                    } else if (rule.bars[bar] == 2) {
                        doubled.push_back(bar + 1);
                    }
                }

                QStringList bits;
                if (!crossed.empty()) {
                    bits << muse::qtrc("starscore", "%1 above in bars %2").arg(lo.name, starscoreBarRanges(crossed));
                }
                if (!doubled.empty()) {
                    bits << muse::qtrc("starscore", "doubled in bars %1").arg(starscoreBarRanges(doubled));
                }
                rule.summary = bits.isEmpty() ? muse::qtrc("starscore", "Always in order") : bits.join("; ");
                vs.rules.push_back(std::move(rule));
            }
        }

        if (!vs.rules.empty()) {
            result.push_back(std::move(vs));
        }
    }

    // Two-staff keyboards (piano, electric piano, organ, …): the left hand stays below the right hand.
    // Compares the left hand's highest note with the right hand's lowest wherever both hands play.
    StarScoreVoiceSection keys;
    keys.section = muse::qtrc("starscore", "Keyboards");
    keys.barCount = barCount;
    std::set<QString> inSections;
    for (const StarScoreSection& sec : data.sections) {
        for (const QString& pid : sec.partIds) {
            inSections.insert(pid);
        }
    }
    static const QStringList KEYBOARDS { "piano", "clavinet", "organ", "synth", "keyboard", "harpsichord", "celesta", "accordion" };
    for (const Part* p : ms->parts()) {
        if (p->nstaves() != 2 || (!inSections.empty() && !inSections.count(idText(p)))) {
            continue;
        }
        const QString iid = p->instrumentId().toQString();
        if (std::none_of(KEYBOARDS.begin(), KEYBOARDS.end(), [&](const QString& k) { return iid.contains(k); })) {
            continue;
        }
        const std::vector<VoiceNote> rh = starscoreStaffNotes(p->staves().at(0), ms, true);
        const std::vector<VoiceNote> lh = starscoreStaffNotes(p->staves().at(1), ms, false);
        if (rh.empty() || lh.empty()) {
            continue;
        }

        const QString name = p->partName().toQString();
        StarScoreVoiceRule rule;
        rule.upperPartId = idText(p);
        rule.lowerPartId = idText(p);
        rule.strict = true;
        rule.label = muse::qtrc("starscore", "%1: left hand below right hand").arg(name);
        rule.bars.assign(barCount, 4);
        size_t a = 0;
        size_t b = 0;
        while (a < rh.size() && b < lh.size()) {
            const VoiceNote& r = rh[a];
            const VoiceNote& l = lh[b];
            const int from = std::max(r.start, l.start);
            const int to = std::min(r.end, l.end);
            if (from < to) {
                const int bar = barOf(from);
                if (bar >= 0 && bar < barCount) {
                    int& cell = rule.bars[bar];
                    if (l.top > r.top) {
                        cell = 1;
                    } else if (l.top == r.top) {
                        if (cell != 1) {
                            cell = 2;
                        }
                    } else if (cell == 4) {
                        cell = 0;
                    }
                }
            }
            if (r.end < l.end) {
                ++a;
            } else {
                ++b;
            }
        }
        std::vector<int> crossed;
        std::vector<int> doubled;
        for (int bar = 0; bar < barCount; ++bar) {
            if (rule.bars[bar] == 1) {
                crossed.push_back(bar + 1);
            } else if (rule.bars[bar] == 2) {
                doubled.push_back(bar + 1);
            }
        }
        QStringList bits;
        if (!crossed.empty()) {
            bits << muse::qtrc("starscore", "left hand above in bars %1").arg(starscoreBarRanges(crossed));
        }
        if (!doubled.empty()) {
            bits << muse::qtrc("starscore", "hands share a note in bars %1").arg(starscoreBarRanges(doubled));
        }
        rule.summary = bits.isEmpty() ? muse::qtrc("starscore", "Always in order") : bits.join("; ");
        keys.rules.push_back(std::move(rule));
    }
    if (!keys.rules.empty()) {
        result.push_back(std::move(keys));
    }

    return result;
}
