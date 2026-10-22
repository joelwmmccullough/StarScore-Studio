/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Combined parts (see starscorecombined.h). How the lines are put together follows the usual practice for several
 * parts on one staff (Gould, Behind Bars; Dorico's condensing): parts that play exactly the same thing (notes,
 * rhythm, articulations, dynamics, texts) share one line; otherwise each gets its own voice, the higher line in the
 * upper voice; a grouping holds for a whole passage, not bar by bar, and bars where everyone rests don't break it;
 * every change of grouping is labelled where it happens, and "All" marks where the lines join again.
 */
#include "starscorecombined.h"
#include "starscoreengraving.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>

#include <QFile>
#include <QRegularExpression>
#include <QTextStream>

#include "engraving/dom/factory.h"
#include "engraving/dom/fermata.h"
#include "engraving/dom/hairpin.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "engraving/dom/stem.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/systemlock.h"
#include "engraving/editing/editsystemlocks.h"
#include "engraving/rw/xmlreader.h"
#include "engraving/style/style.h"

#include "notation/inotationparts.h"

#include "translation.h"
#include "log.h"

using namespace mu::engraving;
using namespace muse;

namespace mu::project::starscore {
namespace {
//! "Trumpet 1" and "Trumpet" are the same chair; "Trumpet 2" is another
QString chairKey(const QString& base)
{
    static const QRegularExpression first("^(.+) 1$");
    const QRegularExpressionMatch m = first.match(base);
    return m.hasMatch() ? m.captured(1) : base;
}

//! A bar's contents as the clipboard writes them, without what doesn't change the music (which staff it is, where a
//! text or dynamic was dragged to)
QString normalized(QString xml)
{
    static const QRegularExpression staffId("<Staff id=\"\\d+\">");
    static const QRegularExpression staffAttr(" staff=\"\\d+\"");
    static const QRegularExpression offset("<offset [^>]*/>");
    // (play: whether a note or chord symbol sounds in playback, not what's printed; 14 Oct 2026: Drum Battle's
    // trumpet lines differed only in that and were printed as two voices in unison)
    static const QRegularExpression simple("<(placement|autoplace|minDistance|play|eid|velocity)>[^<]*</\\1>");
    // an articulation's glyph above or below follows the stem; the clipboard's list of the voices it holds
    static const QRegularExpression aboveBelow("<subtype>(artic\\w+?)(Above|Below)</subtype>");
    static const QRegularExpression voiceOffset("<voiceOffset>.*?</voiceOffset>", QRegularExpression::DotMatchesEverythingOption);
    xml.replace(aboveBelow, "<subtype>\\1</subtype>");
    xml.replace(voiceOffset, QString());
    xml.replace(staffId, "<Staff>");
    xml.replace(staffAttr, QString());
    xml.replace(offset, QString());
    xml.replace(simple, QString());
    // (and the indentation of what was taken out)
    static const QRegularExpression between(">\\s+<");
    xml.replace(between, "><");
    // where a slur or glissando ending here started (in an earlier bar, compared there; G.I. Jorge's alto sax bar 24 was
    // the same notes in five arrangements, split only by where the slur into it began)
    static const QRegularExpression spannerEnd("<Spanner type=\"[^\"]+\"><prev><location>.*?</location></prev></Spanner>",
                                               QRegularExpression::DotMatchesEverythingOption);
    xml.replace(spannerEnd, QString());
    return xml;
}

//! What decides whether two lines are the same: dynamics and hairpins left out (Joel, 14 Oct 2026: the same notes
//! with f in one arrangement and mf in another were printed as two voices in unison; the line keeps the dynamics of
//! the arrangement it's taken from)
QString withoutDynamics(QString xml)
{
    static const QRegularExpression dynamic("<Dynamic>.*?</Dynamic>", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression hairpin("<Spanner type=\"HairPin\">.*?</Spanner>", QRegularExpression::DotMatchesEverythingOption);
    xml.replace(dynamic, QString());
    xml.replace(hairpin, QString());
    return xml;
}

//! Nothing but rests (no notes, chord symbols or texts)
bool restsOnly(const QString& sig)
{
    return !sig.contains("<Chord") && !sig.contains("<Harmony") && !sig.contains("Text>") && !sig.contains("<Dynamic");
}

//! The clip moved up by `base` voices (its voice 1 into voice base+1, and so on)
QString shifted(QString xml, int base)
{
    if (base == 0) {
        return xml;
    }
    static const QRegularExpression voiceId("<voice id=\"(\\d)\">");
    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = voiceId.globalMatch(xml);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += xml.mid(last, m.capturedStart() - last) + QString("<voice id=\"%1\">").arg(m.captured(1).toInt() + base);
        last = m.capturedEnd();
    }
    out += xml.mid(last);
    const int end = out.indexOf("</voiceOffset>");
    if (end >= 0) {
        out.insert(end + int(QString("</voiceOffset>").size()), QString("<location><voices>%1</voices></location>").arg(base));
    }
    return out;
}

//! The highest voice the clip uses (0 = voice 1)
int topVoice(const QString& xml)
{
    static const QRegularExpression voiceId("<voice id=\"(\\d)\">");
    int top = 0;
    QRegularExpressionMatchIterator it = voiceId.globalMatch(xml);
    while (it.hasNext()) {
        top = std::max(top, it.next().captured(1).toInt());
    }
    return top;
}

//! A bar's rhythm: each chord and rest with its length, tuplets, ties and grace notes (pitches left out)
QString rhythmOf(const QString& sig)
{
    static const QRegularExpression tok("<(Chord|Rest)>|<durationType>[^<]*</durationType>|<dots>\\d</dots>|<Tuplet\\b|<endTuplet/>"
                                        "|<Spanner type=\"Tie\">|<(acciaccatura|appoggiatura|grace\\w*)/>|<ticks>[^<]*</ticks>"
                                        // (beat slashes and rhythm markings don't share stems with notes)
                                        "|<head>[^<]*</head>|<noStem>[^<]*</noStem>|<small>[^<]*</small>");
    QString out;
    QRegularExpressionMatchIterator it = tok.globalMatch(sig);
    while (it.hasNext()) {
        out += it.next().captured(0);
    }
    return out;
}

//! Each chord's pitches in a bar, in order
std::vector<std::vector<int> > chordPitches(const QString& sig)
{
    static const QRegularExpression chord("<Chord>(.*?)</Chord>", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression pitch("<pitch>(\\d+)</pitch>");
    std::vector<std::vector<int> > out;
    QRegularExpressionMatchIterator it = chord.globalMatch(sig);
    while (it.hasNext()) {
        std::vector<int> ps;
        QRegularExpressionMatchIterator pit = pitch.globalMatch(it.next().captured(1));
        while (pit.hasNext()) {
            ps.push_back(pit.next().captured(1).toInt());
        }
        out.push_back(ps);
    }
    return out;
}

double meanPitch(const QStringList& sigs)
{
    static const QRegularExpression pitch("<pitch>(\\d+)</pitch>");
    double sum = 0.0;
    int count = 0;
    for (const QString& s : sigs) {
        QRegularExpressionMatchIterator it = pitch.globalMatch(s);
        while (it.hasNext()) {
            sum += it.next().captured(1).toDouble();
            ++count;
        }
    }
    return count ? sum / count : -1.0;
}
}

QString arrangementLabel(std::vector<int> horns)
{
    std::sort(horns.begin(), horns.end());
    horns.erase(std::unique(horns.begin(), horns.end()), horns.end());
    QStringList parts;
    for (size_t i = 0; i < horns.size();) {
        size_t j = i;
        while (j + 1 < horns.size() && horns[j + 1] == horns[j] + 1) {
            ++j;
        }
        if (j - i >= 2) {
            parts << QString("%1H–%2H").arg(horns[i]).arg(horns[j]);
        } else {
            for (size_t k = i; k <= j; ++k) {
                parts << QString("%1H").arg(horns[k]);
            }
        }
        i = j + 1;
    }
    return parts.join(", ");
}

std::vector<CombinedChair> combinedChairs(const MasterScore* ms, const std::vector<StarScoreSection>& sections)
{
    std::vector<CombinedChair> out;
    if (!ms) {
        return out;
    }
    static const QRegularExpression hornKey("^([1-7])-horn$");
    static const QRegularExpression named("^([1-7])H: (.+)$");
    std::map<QString, std::vector<CombinedSource> > byChair;
    std::vector<QString> order;
    for (const StarScoreSection& s : sections) {
        const QRegularExpressionMatch hk = hornKey.match(s.templateKey);
        if (!hk.hasMatch()) {
            continue;
        }
        const int horns = hk.captured(1).toInt();
        for (const QString& pid : s.partIds) {
            if (s.alternates.count(pid)) {
                continue;   // a stand-in version (1H saxes and trombone, 7H low horn versions, a piccolo's flute)
            }
            const Part* p = ms->partById(ID(pid));
            if (!p || !partHasNotes(p)) {
                continue;
            }
            const QRegularExpressionMatch m = named.match(p->partName().toQString());
            if (!m.hasMatch()) {
                continue;
            }
            const QString key = chairKey(m.captured(2));
            std::vector<CombinedSource>& list = byChair[key];
            if (std::any_of(list.begin(), list.end(), [&](const CombinedSource& c) { return c.horns == horns; })) {
                continue;
            }
            if (list.empty()) {
                order.push_back(key);
            }
            list.push_back({ horns, pid });
        }
    }
    for (const QString& key : order) {
        std::vector<CombinedSource> list = byChair[key];
        if (list.size() < 2) {
            continue;
        }
        std::sort(list.begin(), list.end(), [](const CombinedSource& a, const CombinedSource& b) { return a.horns < b.horns; });
        CombinedChair c;
        c.name = byChair.count(key + " 2") ? key + " 1" : key;
        c.sources = list;
        out.push_back(c);
    }
    return out;
}

CombinedResult buildCombinedPart(const mu::notation::IMasterNotationPtr& copy, const CombinedChair& chair)
{
    CombinedResult r;
    MasterScore* ms = copy ? copy->masterScore() : nullptr;
    if (!ms || !ms->firstMeasure() || chair.sources.size() < 2) {
        r.problem = "nothing to combine";
        return r;
    }
    std::vector<const Part*> parts;
    for (const CombinedSource& s : chair.sources) {
        const Part* p = ms->partById(ID(s.partId));
        if (!p || p->staves().empty()) {
            r.problem = "a part is missing from the copy";
            return r;
        }
        parts.push_back(p);
    }
    Part* target = const_cast<Part*>(parts.back());
    const size_t n = parts.size();

    // the bars as they are (selecting bars snaps to multimeasure rests)
    if (ms->style().styleB(Sid::createMultiMeasureRests)) {
        copy->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("No multimeasure rests"));
        ms->undoChangeStyleVal(Sid::createMultiMeasureRests, false);
        copy->notation()->undoStack()->commitChanges();
    }
    ms->setLayoutAll();
    ms->doLayout();

    auto clip = [&](const Part* p, const Fraction& from, const Fraction& to) {
        const staff_idx_t st = p->staves().front()->idx();
        ms->deselectAll();
        ms->selection().setRangeTicks(from, to, st, st + 1);
        ms->selection().updateSelectedElements();
        const ByteArray mime = ms->selection().mimeData();
        ms->deselectAll();
        return QString::fromUtf8(reinterpret_cast<const char*>(mime.constData()), int(mime.size()));
    };

    // 1. each bar: which arrangements play the same thing
    std::vector<Measure*> bars;
    for (Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        bars.push_back(m);
    }
    const size_t nb = bars.size();
    std::vector<std::vector<int> > group(nb, std::vector<int>(n, 0));     // bar, arrangement -> group
    std::vector<std::vector<bool> > rests(nb, std::vector<bool>(n, true));   // bar, arrangement -> only rests
    std::vector<std::vector<QString> > sigs(nb, std::vector<QString>(n));
    // the voices each bar uses (from the clip as written: normalized() takes out the list of voices)
    std::vector<std::vector<int> > voicesUsed(nb, std::vector<int>(n, 1));
    std::vector<bool> silent(nb, true);   // everyone rests
    for (size_t i = 0; i < nb; ++i) {
        QStringList seen;
        for (size_t k = 0; k < n; ++k) {
            const QString raw = clip(parts[k], bars[i]->tick(), bars[i]->endTick());
            voicesUsed[i][k] = topVoice(raw) + 1;
            QString sig = normalized(raw);
            rests[i][k] = restsOnly(sig);
            sigs[i][k] = sig;
            const QString key = rests[i][k] ? QString("REST") : withoutDynamics(sig);
            int g = int(seen.indexOf(key));
            if (g < 0) {
                g = int(seen.size());
                seen << key;
            }
            group[i][k] = g;
            silent[i] = silent[i] && rests[i][k];
        }
    }
    // (STARSCORE_COMBINED_DEBUG=<file>: each bar's grouping, and where the different lines first differ)
    if (const QString dbg = qEnvironmentVariable("STARSCORE_COMBINED_DEBUG"); !dbg.isEmpty()) {
        QFile f(dbg);
        if (f.open(QIODevice::Append | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << "== " << chair.name << "\n";
            for (size_t b = 0; b < nb; ++b) {
                QStringList g;
                for (size_t k = 0; k < n; ++k) {
                    g << QString("%1H:%2").arg(chair.sources[k].horns).arg(rests[b][k] ? QString("-") : QString::number(group[b][k]));
                }
                ts << "bar " << b + 1 << " (" << bars[b]->no() + 1 << ") " << g.join(' ') << "\n";
                if (!silent[b] && qEnvironmentVariableIsSet("STARSCORE_COMBINED_DEBUG_NOTES")) {
                    // each line's notes: duration and pitches
                    static const QRegularExpression tok("<(Chord|Rest)>|<durationType>([^<]+)</durationType>|<pitch>(\\d+)</pitch>");
                    for (size_t k = 0; k < n; ++k) {
                        QStringList out;
                        QRegularExpressionMatchIterator it = tok.globalMatch(sigs[b][k]);
                        while (it.hasNext()) {
                            const QRegularExpressionMatch mm = it.next();
                            if (mm.captured(1) == "Rest") {
                                out << "r";
                            } else if (mm.captured(1) == "Chord") {
                                out << "|";
                            } else if (!mm.captured(2).isEmpty()) {
                                out.last() += mm.captured(2).left(3);
                            } else {
                                out.last() += "." + mm.captured(3);
                            }
                        }
                        ts << "     " << chair.sources[k].horns << "H " << out.join(' ') << "\n";
                    }
                }
                for (size_t k = 1; k < n; ++k) {
                    if (rests[b][k] || rests[b][0] || group[b][k] == group[b][0]) {
                        continue;
                    }
                    // the first differing spot between arrangement 0's line and this one's
                    const QString& a = sigs[b][0];
                    const QString& c = sigs[b][k];
                    int p = 0;
                    while (p < a.size() && p < c.size() && a[p] == c[p]) {
                        ++p;
                    }
                    const int from = std::max(0, p - 80);
                    ts << "   vs " << chair.sources[k].horns << "H @" << p << "\n     A: "
                       << QString(a.mid(from, 200)).replace('\n', ' ') << "\n     B: " << QString(c.mid(from, 200)).replace('\n', ' ') << "\n";
                }
            }
        }
    }

    // 2. Passages: bars in a row with the same grouping (bars where everyone rests belong to the passage they're
    // in). Until 1.18.46 the grouping held for a whole phrase (from one bar where everyone rests to the next, as
    // Dorico condenses), so one bar that differed split the whole phrase into voices, and arrangements playing the
    // same thing were printed in unison in two voices for bars on end (Joel, 14 Oct 2026: "bizarre unisons"). Now
    // the lines split only where they differ. So that they don't split and join bar by bar, a single bar where lines
    // come together between two passages split two ways stays split.
    struct Run {
        size_t first = 0, last = 0;
        std::vector<int> grouping;
        int groups = 1;
        size_t firstPlayed = 0;   // the first bar where someone plays (where the labels go)
        int played = 0;           // bars where someone plays
    };
    std::vector<Run> runs;
    for (size_t i = 0; i < nb; ++i) {
        if (silent[i]) {
            if (runs.empty()) {
                runs.push_back({ i, i, std::vector<int>(n, 0), 1, i, 0 });
            } else {
                runs.back().last = i;
            }
            continue;
        }
        const std::vector<int>& grouping = group[i];   // (group ids in order of first appearance: comparable)
        const int groups = *std::max_element(grouping.begin(), grouping.end()) + 1;
        if (!runs.empty() && runs.back().played == 0) {
            Run& r0 = runs.back();
            r0.last = i;
            r0.grouping = grouping;
            r0.groups = groups;
            r0.firstPlayed = i;
            r0.played = 1;
        } else if (!runs.empty() && runs.back().grouping == grouping) {
            runs.back().last = i;
            ++runs.back().played;
        } else {
            runs.push_back({ i, i, grouping, groups, i, 1 });
        }
    }
    // whether `coarse` keeps together every arrangements `fine` keeps together
    auto coarser = [&](const std::vector<int>& coarse, const std::vector<int>& fine) {
        for (size_t k = 0; k < n; ++k) {
            for (size_t k2 = k + 1; k2 < n; ++k2) {
                if (fine[k] == fine[k2] && coarse[k] != coarse[k2]) {
                    return false;
                }
            }
        }
        return true;
    };
    for (bool merged = true; merged;) {
        merged = false;
        for (size_t r = 1; r + 1 < runs.size(); ++r) {
            // (only between two-way splits: a bar where three or more lines come together is shown as one line, not
            // as the same notes on three staves; G.I. Jorge, alto sax, B3)
            if (runs[r].played == 1 && runs[r - 1].grouping == runs[r + 1].grouping && runs[r].groups < runs[r - 1].groups
                && runs[r - 1].groups <= 2 && coarser(runs[r].grouping, runs[r - 1].grouping)) {
                runs[r - 1].last = runs[r + 1].last;
                runs[r - 1].played += runs[r].played + runs[r + 1].played;
                runs.erase(runs.begin() + long(r), runs.begin() + long(r) + 2);
                merged = true;
                break;
            }
        }
    }

    // 3. per passage: each group's voice (higher line first; a group that only rests last), and the clips
    struct Paste {
        int staff = 0;   // 0 the combined staff, 1… the staves under it
        Fraction tick;
        QString xml;
    };
    struct Label {
        int staff = 0;
        size_t bar = 0;
        int voice = 0;
        QString text;
    };
    std::vector<Paste> pastes;
    std::vector<Label> labels;
    // where each passage's lines went: (staff, first voice, the arrangement whose line it is), to tie notes over
    // from one passage into the next (a tie out of a clip isn't pasted)
    struct Placed {
        int staff = 0;
        int base = 0;
        size_t rep = 0;
    };
    std::vector<std::vector<Placed> > placedByRun;
    // lines whose notes go into the top line's chords: (first bar, last bar, the arrangement whose line it is)
    struct Shared {
        size_t first = 0, last = 0;
        size_t rep = 0;
        int staff = 0;   // where its head line is
        int base = 0;
    };
    std::vector<Shared> sharedLines;
    int previousGroups = 1;
    QStringList previousShown;
    // Two lines to a staff at most (Gould: more than two parts on a staff can't be read at sight); a third line
    // goes on a staff of its own under it, shown only where it's needed. Before 1.18.46 a staff took four voices
    // (Joel, 14 Oct 2026: four lines on one staff in Industrial Park Driveway). (STARSCORE_COMBINED_VOICES: other
    // than two, for testing)
    static const int perStaff = std::clamp(qEnvironmentVariableIntValue("STARSCORE_COMBINED_VOICES") > 0
                                           ? qEnvironmentVariableIntValue("STARSCORE_COMBINED_VOICES") : 2, 1, int(VOICES));
    static constexpr int MAX_STAVES = 4;
    int stavesNeeded = 1;
    for (const Run& run : runs) {
        ++r.passages;
        struct G {
            int id = 0;
            size_t rep = 0;           // the arrangement whose line it is
            std::vector<int> horns;
            bool restsThroughout = true;
            double pitch = -1.0;
            int voices = 1;
            int staff = 0;
            int base = 0;
            bool shared = false;
        };
        std::vector<G> gs(run.groups);
        for (int g = 0; g < run.groups; ++g) {
            gs[g].id = g;
            bool repSet = false;
            for (size_t k = 0; k < n; ++k) {
                if (run.grouping[k] != g) {
                    continue;
                }
                gs[g].horns.push_back(chair.sources[k].horns);
                if (!repSet) {
                    gs[g].rep = k;
                    repSet = true;
                }
            }
            QStringList repSigs;
            for (size_t i = run.first; i <= run.last; ++i) {
                if (silent[i]) {
                    continue;
                }
                repSigs << sigs[i][gs[g].rep];
                gs[g].restsThroughout = gs[g].restsThroughout && rests[i][gs[g].rep];
                gs[g].voices = std::max(gs[g].voices, voicesUsed[i][gs[g].rep]);
            }
            gs[g].pitch = meanPitch(repSigs);
        }
        std::stable_sort(gs.begin(), gs.end(), [](const G& a, const G& b) {
            if (a.restsThroughout != b.restsThroughout) {
                return !a.restsThroughout;
            }
            return a.pitch > b.pitch;
        });
        // Lines in the same rhythm that don't cross share stems: one chord, each line's note in it, the labels over
        // it top line first, "1H / 2H / 3H" (Gould: parts in the same rhythm share a stem; a unison is one note).
        // Before 1.18.46 each got a voice or a staff of its own, so a horn stab in four arrangements took four staves
        // (Joel, 14 Oct 2026: Industrial Park Driveway). Lines are taken from the top down: one joins the line above
        // it when it has the same rhythm wherever it plays (and that line plays there too) and its notes are never
        // above the notes of the line just above it.
        std::vector<G*> playing;
        for (G& g : gs) {
            if (!g.restsThroughout) {
                playing.push_back(&g);
            }
        }
        std::map<const G*, const G*> headOf;   // a line sharing stems -> the line whose chords take its notes
        for (size_t k = 1; k < playing.size(); ++k) {
            const G* above = playing[k - 1];
            const G* head = headOf.count(above) ? headOf[above] : above;
            const G* g = playing[k];
            bool fits = head->voices == 1 && g->voices == 1;
            for (size_t i = run.first; i <= run.last && fits; ++i) {
                if (silent[i] || rests[i][g->rep]) {
                    continue;
                }
                const QString& mine = sigs[i][g->rep];
                fits = !rests[i][head->rep] && rhythmOf(mine) == rhythmOf(sigs[i][head->rep]);
                if (fits) {
                    // (the line above, or the head where the line above rests)
                    const std::vector<std::vector<int> > hi = chordPitches(sigs[i][rests[i][above->rep] ? head->rep : above->rep]);
                    const std::vector<std::vector<int> > lo = chordPitches(mine);
                    fits = hi.size() == lo.size();
                    for (size_t c = 0; c < lo.size() && fits; ++c) {
                        fits = !hi[c].empty() && !lo[c].empty()
                               && *std::min_element(hi[c].begin(), hi[c].end()) >= *std::max_element(lo[c].begin(), lo[c].end());
                    }
                }
            }
            if (fits) {
                headOf[g] = head;
            }
        }
        int used[MAX_STAVES] = { 0, 0, 0, 0 };
        // Two lines share a staff (one stems up, labelled above; one stems down, labelled below). Three or four lines
        // get a staff each, labelled above, so no label sits between two staves where it could be either's; more
        // than four go two to a staff again.
        const int lines = int(playing.size() - headOf.size());
        const int runPerStaff = qEnvironmentVariableIntValue("STARSCORE_COMBINED_VOICES") > 0 ? perStaff
                                : (lines >= 3 && lines <= MAX_STAVES ? 1 : perStaff);
        for (G& g : gs) {
            // A group that only rests gets no line: the lines played are labelled with who plays them, so the others
            // know they rest (Gould: a part labelled "1." is played by the first player alone). Before 1.18.46 it got
            // a voice of rests and a label, or "4H–7H tacet" where there was no room (Joel, 14 Oct 2026: labels
            // stacked three deep in Industrial Park Driveway).
            if (g.restsThroughout) {
                g.staff = -1;
                continue;
            }
            if (headOf.count(&g)) {
                continue;   // placed with its head, below
            }
            // the first staff with room (a line in more voices than that has a staff to itself)
            int st = 0;
            while (st < MAX_STAVES && used[st] > 0 && used[st] + g.voices > std::max(runPerStaff, g.voices)) {
                ++st;
            }
            if (st >= MAX_STAVES || used[st] + g.voices > int(VOICES)) {
                r.problem = QString("bar %1: too many different lines at once").arg(bars[run.first]->no() + 1);
                return r;
            }
            g.staff = st;
            g.base = used[st];
            used[st] += g.voices;
            stavesNeeded = std::max(stavesNeeded, st + 1);
        }
        for (auto& [g, head] : headOf) {
            G* m = const_cast<G*>(g);
            m->staff = head->staff;
            m->base = head->base;
            m->shared = true;   // its notes go into its head's chords
        }
        if (run.groups > 1) {
            ++r.splitPassages;
        }

        placedByRun.emplace_back();
        for (const G& g : gs) {
            if (g.staff < 0) {
                continue;
            }
            placedByRun.back().push_back({ g.staff, g.base, g.rep });
            if (g.shared) {
                sharedLines.push_back({ run.first, run.last, g.rep, g.staff, g.base });
                continue;
            }
            const bool main = g.staff == 0 && g.base == 0;
            // the top line takes the whole passage (rests where everyone rests); the others only where something
            // is played, so a bar of rests shows one rest
            size_t i = run.first;
            while (i <= run.last) {
                if (!main && silent[i]) {
                    ++i;
                    continue;
                }
                size_t j = i;
                while (j + 1 <= run.last && (main || !silent[j + 1])) {
                    ++j;
                }
                const QString xml = clip(parts[g.rep], bars[i]->tick(), bars[j]->endTick());
                pastes.push_back({ g.staff, bars[i]->tick(), shifted(xml, g.base) });
                i = j + 1;
            }
        }
        // the labels: who plays each line, where that changes (not again when only who rests changed)
        QStringList shown;
        for (const G& g : gs) {
            if (g.staff >= 0) {
                shown << QString("%1/%2/%3").arg(g.staff).arg(g.base).arg(arrangementLabel(g.horns));
            }
        }
        if (run.groups > 1 && shown != previousShown) {
            for (const G& g : gs) {
                if (g.staff < 0 || g.shared) {
                    continue;
                }
                QStringList stack { arrangementLabel(g.horns) };   // its own, then the lines sharing its stems, top down
                for (const G& m : gs) {
                    auto h = headOf.find(&m);
                    if (h != headOf.end() && h->second == &g) {
                        stack << arrangementLabel(m.horns);
                    }
                }
                labels.push_back({ g.staff, run.firstPlayed, g.base, stack.join(" / ") });
            }
        }
        if (run.groups == 1 && previousGroups > 1) {
            labels.push_back({ 0, run.firstPlayed, 0, muse::qtrc("starscore", "All") });
        }
        previousGroups = run.groups;
        previousShown = run.groups > 1 ? shown : QStringList();
    }

    // 4. into the copy: the largest arrangement's part emptied, the staves needed under it, the lines and the labels
    r.staves = stavesNeeded;
    r.secondStaff = stavesNeeded > 1;
    for (int k = 1; k < stavesNeeded; ++k) {
        Staff* st = Factory::createStaff(target);
        st->setDefaultClefType(target->staves().front()->defaultClefType());
        copy->parts()->appendStaff(st, target->id());
    }
    const staff_idx_t dst0 = target->staves().front()->idx();
    copy->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Combined part"));
    ms->deselectAll();
    ms->selection().setRangeTicks(ms->firstMeasure()->tick(), ms->lastMeasure()->endTick(), dst0, dst0 + target->nstaves());
    ms->selection().updateSelectedElements();
    ms->cmdDeleteSelection();
    ms->deselectAll();
    for (const Paste& p : pastes) {
        Measure* m = ms->tick2measure(p.tick);
        Segment* seg = m ? m->first(SegmentType::ChordRest) : nullptr;
        if (!seg) {
            continue;
        }
        const QByteArray bytes = p.xml.toUtf8();
        XmlReader reader(ByteArray::fromQByteArrayNoCopy(bytes));
        if (!ms->pasteStaff(reader, seg, dst0 + p.staff)) {
            LOGW() << "[starscore] combined part " << chair.name.toStdString() << ": a passage at bar " << m->no() + 1
                   << " couldn't be pasted";
        }
        ms->deselectAll();
    }
    // Lines sharing stems: each note of theirs into the top line's chord at the same beat (a unison is one note), tied
    // as in their own arrangement
    for (const Shared& sh : sharedLines) {
        const staff_idx_t srcStaff = parts[sh.rep]->staves().front()->idx();
        std::map<const Note*, Note*> placed;   // the arrangement's note -> the note in the chord
        std::set<const Note*> made;            // notes this line added (not a unison with the head line's)
        for (size_t b = sh.first; b <= sh.last; ++b) {
            for (Segment* seg = bars[b]->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                const EngravingItem* se = seg->element(srcStaff * VOICES);
                EngravingItem* de = seg->element((dst0 + sh.staff) * VOICES + sh.base);
                if (!se || !se->isChord() || !de || !de->isChord()) {
                    continue;
                }
                Chord* dc = toChord(de);
                for (const Note* sn : toChord(se)->notes()) {
                    Note* there = dc->findNote(sn->pitch());
                    if (!there) {
                        there = Factory::createNote(dc);
                        there->setParent(dc);
                        there->setTrack(dc->track());
                        there->setNval(sn->noteVal());
                        ms->undoAddElement(there);
                        made.insert(there);
                    }
                    placed[sn] = there;
                }
                // its grace notes are left out: the rhythm compared doesn't let them differ
            }
        }
        for (const auto& [sn, there] : placed) {
            const Tie* st = sn->tieFor();
            auto to = st ? placed.find(st->endNote()) : placed.end();
            // (only between notes this line added: a unison note is the head line's, tied or not as that line is)
            if (to == placed.end() || there->tieFor() || to->second->tieBack() || !made.count(there) || !made.count(to->second)) {
                continue;
            }
            Tie* tie = Factory::createTie(ms->dummy());
            tie->setStartNote(there);
            tie->setEndNote(to->second);
            tie->setTick(there->tick());
            tie->setTick2(to->second->tick());
            tie->setTrack(there->track());
            there->setTieFor(tie);
            to->second->setTieBack(tie);
            ms->undoAddElement(tie);
        }
        ++r.sharedStemPassages;
    }

    // Ties from one passage into the next: a clip's tie into a bar after it isn't pasted. Where the line's
    // arrangement ties a note over, the note is tied to the same pitch at the start of the next passage on its staff
    // (in its own voice when it has the note there, else in another).
    int tiesMade = 0;
    for (size_t ri = 0; ri + 1 < runs.size() && ri < placedByRun.size(); ++ri) {
        const Fraction boundary = bars[runs[ri].last]->endTick();
        Measure* lastBar = bars[runs[ri].last];
        for (const Placed& pl : placedByRun[ri]) {
            const staff_idx_t srcStaff = parts[pl.rep]->staves().front()->idx();
            const staff_idx_t dstStaff = dst0 + pl.staff;
            for (Segment* seg = lastBar->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                for (voice_idx_t v = 0; v < VOICES; ++v) {
                    const EngravingItem* se = seg->element(srcStaff * VOICES + v);
                    if (!se || !se->isChord()) {
                        continue;
                    }
                    for (const Note* sn : toChord(se)->notes()) {
                        const Tie* st = sn->tieFor();
                        if (!st || !st->endNote() || st->endNote()->tick() != boundary) {
                            continue;
                        }
                        // the note it starts from: the same pitch in the line's voice, in the chord at the same beat
                        const voice_idx_t dv = voice_idx_t(pl.base) + v;
                        if (dv >= VOICES) {
                            continue;
                        }
                        const EngravingItem* de = seg->element(dstStaff * VOICES + dv);
                        Note* from = de && de->isChord() ? toChord(de)->findNote(sn->pitch()) : nullptr;
                        if (!from || from->tieFor()) {
                            continue;
                        }
                        Segment* next = ms->tick2segment(boundary, true, SegmentType::ChordRest);
                        Note* to = nullptr;
                        for (int pass = 0; pass < 2 && next && !to; ++pass) {
                            for (voice_idx_t w = 0; w < VOICES && !to; ++w) {
                                if ((pass == 0) != (w == dv)) {
                                    continue;
                                }
                                const EngravingItem* ne = next->element(dstStaff * VOICES + w);
                                Note* cand = ne && ne->isChord() ? toChord(ne)->findNote(sn->pitch()) : nullptr;
                                if (cand && !cand->tieBack()) {
                                    to = cand;
                                }
                            }
                        }
                        if (!to) {
                            continue;
                        }
                        Tie* tie = Factory::createTie(ms->dummy());
                        tie->setStartNote(from);
                        tie->setEndNote(to);
                        tie->setTick(from->tick());
                        tie->setTick2(to->tick());
                        tie->setTrack(from->track());
                        from->setTieFor(tie);
                        to->setTieBack(tie);
                        ms->undoAddElement(tie);
                        ++tiesMade;
                    }
                }
            }
        }
    }
    r.tiesMade = tiesMade;

    // A chord symbol or dynamic that several lines brought in, once at its beat; a text with the same words, once
    // within two bars (the arrangements put it in slightly different places). What's left sits where MuseScore puts
    // it: the positions were set by hand for another layout.
    std::vector<EngravingItem*> twice;
    std::map<QString, int> textBar;   // words -> the bar it was kept in
    int barNo = 0;
    for (Measure* m = ms->firstMeasure(); m; m = m->nextMeasure(), ++barNo) {
        std::set<std::pair<int, QString> > seen;
        for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            for (EngravingItem* e : seg->annotations()) {
                if (e->staffIdx() < dst0 || e->staffIdx() >= dst0 + target->nstaves() || !e->isTextBase()) {
                    continue;
                }
                // ("(open)" and "open" are the same words)
                const QString words = toTextBase(e)->plainText().toQString().remove('(').remove(')').simplified().toLower();
                bool dup = false;
                if (e->isHarmony() || e->isDynamic()) {
                    // (once on each staff: a line on the staff under the combined one keeps its own)
                    dup = !seen.insert(std::make_pair(int(e->staffIdx()) * 10000000 + int(e->type()) * 100000 + int(seg->rtick().ticks()),
                                                      words)).second;
                } else {
                    const QString key = QString::number(int(e->type())) + "|" + words;
                    auto it = textBar.find(key);
                    dup = it != textBar.end() && barNo - it->second <= 2;
                    if (!dup) {
                        textBar[key] = barNo;
                    }
                }
                if (dup) {
                    twice.push_back(e);
                } else if (!e->isHarmony()) {
                    e->undoResetProperty(Pid::OFFSET);
                }
            }
        }
    }
    // a fermata on a barline that several lines brought in, once
    for (Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        for (Segment* seg = m->first(); seg; seg = seg->next()) {
            if (!seg->isType(SegmentType::BarLineType)) {
                continue;
            }
            std::set<std::pair<int, int> > seen;
            for (EngravingItem* e : seg->annotations()) {
                if (e->isFermata() && e->staffIdx() >= dst0 && e->staffIdx() < dst0 + target->nstaves()
                    && !seen.insert(std::make_pair(int(e->staffIdx()), int(toFermata(e)->symId()))).second) {
                    twice.push_back(e);
                }
            }
        }
    }
    // hairpins and lines (cresc., dim.) the same in several lines, once
    std::set<std::tuple<int, int, int, int> > lines;
    for (const auto& [tick, sp] : ms->spanner()) {
        if (sp->staffIdx() < dst0 || sp->staffIdx() >= dst0 + target->nstaves() || !(sp->isHairpin() || sp->isTextLine())) {
            continue;
        }
        const auto key = std::make_tuple(int(sp->type()) * 10 + (sp->isHairpin() ? int(toHairpin(sp)->hairpinType()) : 0),
                                         sp->tick().ticks(), sp->tick2().ticks(), int(sp->staffIdx()));
        if (!lines.insert(key).second) {
            twice.push_back(sp);
        }
    }
    for (EngravingItem* e : twice) {
        ms->undoRemoveElement(e);
    }
    for (const Label& l : labels) {
        Segment* seg = bars[l.bar]->first(SegmentType::ChordRest);
        if (!seg) {
            continue;
        }
        StaffText* t = Factory::createStaffText(seg);
        t->setTrack((dst0 + l.staff) * VOICES + l.voice);
        t->setParent(seg);
        t->setXmlText(String(u"<b>%1</b>").arg(String::fromQString(l.text)));
        // smaller than other staff text and starting at the note, not centred on it (Joel, 16 Oct 2026: a label centred
        // on a line's first note ran left of the staff, and labels a bar apart, each wider than the bar, stacked up in
        // a staircase)
        t->setSize(std::max(6.0, t->size() * 0.8));
        t->setPropertyFlags(Pid::FONT_SIZE, PropertyFlags::UNSTYLED);
        t->setAlign(Align(AlignH::LEFT, AlignV::BASELINE));
        t->setPropertyFlags(Pid::ALIGN, PropertyFlags::UNSTYLED);
        t->setPosition(AlignH::LEFT);
        t->setPropertyFlags(Pid::POSITION, PropertyFlags::UNSTYLED);
        if (l.voice % 2 == 1) {
            t->setPlacement(PlacementV::BELOW);
            t->setPropertyFlags(Pid::PLACEMENT, PropertyFlags::UNSTYLED);
        }
        ms->undoAddElement(t, false);
    }
    copy->notation()->undoStack()->commitChanges();

    r.ok = true;
    r.targetPartId = target->id().toQString();
    return r;
}

int hideSlashesOverNotes(Score* score)
{
    if (!score) {
        return 0;
    }
    auto beatSlash = [](const Chord* c) {
        if (c->notes().empty() || c->isGrace()) {
            return false;
        }
        for (const Note* n : c->notes()) {
            if (n->headGroup() != NoteHeadGroup::HEAD_SLASH || isRhythmMarking(n)) {
                return false;
            }
        }
        return c->noStem() || !c->stem() || !c->stem()->visible();
    };
    int hidden = 0;
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (staff_idx_t si = 0; si < score->nstaves(); ++si) {
            std::vector<Chord*> slashes;
            std::vector<Chord*> notes;
            for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                for (voice_idx_t v = 0; v < VOICES; ++v) {
                    EngravingItem* e = seg->element(si * VOICES + v);
                    if (!e || !e->isChord() || !e->visible()) {
                        continue;
                    }
                    Chord* c = toChord(e);
                    if (std::none_of(c->notes().begin(), c->notes().end(), [](const Note* n) { return n->visible(); })) {
                        continue;   // (already hidden)
                    }
                    (beatSlash(c) ? slashes : notes).push_back(c);
                }
            }
            if (slashes.empty() || notes.empty()) {
                continue;
            }
            const double shrink = score->style().spatium() * 0.05;   // touching isn't overlapping
            for (Chord* s : slashes) {
                bool hit = false;
                for (const Note* sn : s->notes()) {
                    const RectF r = sn->pageBoundingRect().adjusted(shrink, shrink, -shrink, -shrink);
                    for (const Chord* o : notes) {
                        if (o->track() == s->track()) {
                            continue;
                        }
                        hit = hit || o->shape().translated(o->pagePos()).intersects(r);
                        for (const Note* on : o->notes()) {
                            hit = hit || on->pageBoundingRect().intersects(r);
                        }
                    }
                }
                if (!hit) {
                    continue;
                }
                const EngravingItem::LocalVisibilityScope local;   // (only in the combined sheet)
                // the slash's notehead only: an invisible chord in a bar of several voices doesn't count in the spacing,
                // so the beat it stood on was spaced by the other voice's longer note (Joel, 21 Oct 2026: Bumper Cars E1,
                // the whole note's beat half as wide as each slash's)
                for (Note* sn : s->notes()) {
                    sn->undoChangeProperty(Pid::VISIBLE, false);
                }
                ++hidden;
            }
        }
    }
    return hidden;
}

QStringList copySectionLayouts(const std::vector<CombinedLayoutSource>& sources, Score* target)
{
    QStringList log;
    if (!target || !target->firstMeasure() || sources.empty()) {
        return log;
    }
    // the sections: from each rehearsal mark to the next (and the bars before the first)
    std::vector<Fraction> starts { target->firstMeasure()->tick() };
    for (Measure* m = target->firstMeasure(); m; m = m->nextMeasure()) {
        bool mark = false;
        for (Segment* seg = m->first(SegmentType::ChordRest); seg && !mark; seg = seg->next(SegmentType::ChordRest)) {
            for (const EngravingItem* e : seg->annotations()) {
                mark = mark || e->isRehearsalMark();
            }
        }
        if (mark && m->tick() != starts.front()) {
            starts.push_back(m->tick());
        }
    }
    const Fraction end = target->lastMeasure()->endTick();
    starts.push_back(end);

    // how busy a part is between two ticks: its notes and chords played
    auto busy = [](const Part* part, const Fraction& from, const Fraction& to) {
        int count = 0;
        const Score* sc = part->score();
        const track_idx_t t0 = part->startTrack(), t1 = part->endTrack();
        for (const Segment* seg = sc->tick2segment(from, true, SegmentType::ChordRest); seg && seg->tick() < to;
             seg = seg->next1(SegmentType::ChordRest)) {
            for (track_idx_t t = t0; t < t1; ++t) {
                const EngravingItem* e = seg->element(t);
                if (e && e->isChord()) {
                    count += 1 + int(toChord(e)->notes().size());
                }
            }
        }
        return count;
    };

    // what each bar gets: none of the book's own breaks or locks are kept
    std::map<int, std::pair<bool, bool> > breaks;   // tick -> (system break, keep together)
    std::vector<std::pair<Fraction, Fraction> > locks;   // first bar's tick, last bar's end
    for (size_t si = 0; si + 1 < starts.size(); ++si) {
        const Fraction from = starts[si], to = starts[si + 1];
        const CombinedLayoutSource* best = nullptr;
        int bestCount = -1;
        for (const CombinedLayoutSource& src : sources) {
            const int c = src.part ? busy(src.part, from, to) : -1;
            if (c >= bestCount) {   // (the later, larger arrangement when even)
                best = &src;
                bestCount = c;
            }
        }
        const Measure* fm = target->tick2measure(from);
        const QString where = fm ? QString("bar %1").arg(fm->no() + 1) : QString("?");
        if (!best || !best->book) {
            log << QString("%1: no part score to follow").arg(where);
            continue;
        }
        int pageBreaks = 0;
        for (const Measure* sm = best->book->tick2measure(from); sm && sm->tick() < to; sm = sm->nextMeasure()) {
            breaks[sm->tick().ticks()] = { sm->lineBreak() || sm->pageBreak(), sm->noBreak() };
            pageBreaks += sm->pageBreak() ? 1 : 0;
        }
        for (const SystemLock* lock : best->book->systemLocks()->allLocks()) {
            if (lock->startMB()->isMeasure() && lock->endMB()->isMeasure() && lock->startMB()->tick() >= from
                && lock->endMB()->endTick() <= to) {
                locks.emplace_back(lock->startMB()->tick(), lock->endMB()->endTick());
            }
        }
        log << QString("%1: from %2H (%3 notes)%4").arg(where).arg(best->horns).arg(bestCount)
            .arg(pageBreaks ? QString(", %1 page break(s) made system breaks").arg(pageBreaks) : QString());
    }

    // (no break inside or at the end of a locked system: adding the lock would take it out again, and a break added
    // and taken out in the same undo step was deleted twice when the copy was closed)
    auto locked = [&](const Fraction& t) {
        return std::any_of(locks.begin(), locks.end(), [&](const std::pair<Fraction, Fraction>& l) {
            return t >= l.first && t < l.second;
        });
    };
    for (Measure* m = target->firstMeasure(); m; m = m->nextMeasure()) {
        const auto it = breaks.find(m->tick().ticks());
        const bool line = it != breaks.end() && it->second.first && !locked(m->tick());
        const bool keep = it != breaks.end() && it->second.second && !locked(m->tick());
        if (m->pageBreak()) {
            m->undoSetBreak(false, LayoutBreakType::PAGE);
        }
        if (m->lineBreak() != line) {
            m->undoSetBreak(line, LayoutBreakType::LINE);
        }
        if (m->noBreak() != keep) {
            m->undoSetBreak(keep, LayoutBreakType::NOBREAK);
        }
    }
    if (!target->systemLocks()->allLocks().empty()) {
        EditSystemLocks::undoRemoveAllLocks(target);
    }
    const bool mmrests = target->style().styleB(Sid::createMultiMeasureRests);
    for (const auto& [a0, b0] : locks) {
        Measure* a = target->tick2measure(a0);
        Measure* b = nullptr;
        for (Measure* m = a; m && m->tick() < b0; m = m->nextMeasure()) {
            b = m;
        }
        if (!a || !b || a->tick() != a0 || b->endTick() != b0) {
            continue;
        }
        if (mmrests) {
            a = a->coveringMMRestOrThis();
            b = b->coveringMMRestOrThis();
        }
        if (!(a == b || a->isBefore(b)) || target->systemLocks()->lockContaining(a) || target->systemLocks()->lockContaining(b)) {
            continue;
        }
        EditSystemLocks::undoAddSystemLock(target, new SystemLock(a, b));
    }
    target->setLayoutAll();
    return log;
}
}
