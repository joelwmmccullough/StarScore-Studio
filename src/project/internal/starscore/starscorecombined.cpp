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

#include <QRegularExpression>

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
    static const QRegularExpression simple("<(placement|autoplace|minDistance)>[^<]*</\\1>");
    xml.replace(staffId, "<Staff>");
    xml.replace(staffAttr, QString());
    xml.replace(offset, QString());
    xml.replace(simple, QString());
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
    std::vector<bool> silent(nb, true);   // everyone rests
    for (size_t i = 0; i < nb; ++i) {
        QStringList seen;
        for (size_t k = 0; k < n; ++k) {
            QString sig = normalized(clip(parts[k], bars[i]->tick(), bars[i]->endTick()));
            rests[i][k] = restsOnly(sig);
            sigs[i][k] = sig;
            const QString key = rests[i][k] ? QString("REST") : sig;
            int g = int(seen.indexOf(key));
            if (g < 0) {
                g = int(seen.size());
                seen << key;
            }
            group[i][k] = g;
            silent[i] = silent[i] && rests[i][k];
        }
    }

    // 2. Phrases: the bars between two bars where everyone rests, and between rehearsal marks. The grouping holds
    // for a whole phrase (as Dorico condenses), so lines don't split and join bar by bar: two arrangements share a
    // line only if they play the same thing in every bar of the phrase. Phrases in a row with the same grouping are
    // one passage (labelled once); bars where everyone rests belong to the passage they're in.
    auto rehearsalAt = [&](size_t i) {
        for (Segment* seg = bars[i]->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            for (const EngravingItem* e : seg->annotations()) {
                if (e->isRehearsalMark()) {
                    return true;
                }
            }
        }
        return false;
    };
    struct Run {
        size_t first = 0, last = 0;
        std::vector<int> grouping;
        int groups = 1;
        size_t firstPlayed = 0;   // the first bar where someone plays (where the labels go)
    };
    std::vector<Run> runs;
    size_t i = 0;
    while (i < nb) {
        if (silent[i]) {
            if (runs.empty()) {
                runs.push_back({ i, i, std::vector<int>(n, 0), 1, i });
            } else {
                runs.back().last = i;
            }
            ++i;
            continue;
        }
        // the phrase from bar i
        size_t j = i;
        while (j + 1 < nb && !silent[j + 1] && !rehearsalAt(j + 1)) {
            ++j;
        }
        // arrangements together only if together in every bar of it
        std::vector<int> grouping(n, -1);
        int groups = 0;
        for (size_t k = 0; k < n; ++k) {
            if (grouping[k] >= 0) {
                continue;
            }
            grouping[k] = groups;
            for (size_t k2 = k + 1; k2 < n; ++k2) {
                bool same = grouping[k2] < 0;
                for (size_t b = i; b <= j && same; ++b) {
                    same = group[b][k] == group[b][k2];
                }
                if (same) {
                    grouping[k2] = groups;
                }
            }
            ++groups;
        }
        const bool allSilentSoFar = !runs.empty() && std::all_of(silent.begin() + runs.back().first,
                                                                  silent.begin() + runs.back().last + 1, [](bool b) { return b; });
        if (!runs.empty() && runs.back().grouping == grouping && !allSilentSoFar) {
            runs.back().last = j;
        } else if (allSilentSoFar) {
            runs.back().last = j;
            runs.back().grouping = grouping;
            runs.back().groups = groups;
            runs.back().firstPlayed = i;
        } else {
            runs.push_back({ i, j, grouping, groups, i });
        }
        i = j + 1;
    }

    // 3. per passage: each group's voice (higher line first; a group that only rests last), and the clips
    struct Paste {
        int staff = 0;   // 0 the combined staff, 1 the second staff
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
    int previousGroups = 1;
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
                gs[g].voices = std::max(gs[g].voices, topVoice(sigs[i][gs[g].rep]) + 1);
            }
            gs[g].pitch = meanPitch(repSigs);
        }
        std::stable_sort(gs.begin(), gs.end(), [](const G& a, const G& b) {
            if (a.restsThroughout != b.restsThroughout) {
                return !a.restsThroughout;
            }
            return a.pitch > b.pitch;
        });
        int used[2] = { 0, 0 };
        // (STARSCORE_COMBINED_VOICES: fewer voices per staff, for testing the second staff)
        static const int perStaff = std::clamp(qEnvironmentVariableIntValue("STARSCORE_COMBINED_VOICES") > 0
                                               ? qEnvironmentVariableIntValue("STARSCORE_COMBINED_VOICES") : int(VOICES), 1, int(VOICES));
        std::vector<int> tacet;   // arrangements resting through the passage that got no line of their own
        for (G& g : gs) {
            // a group that only rests goes on the second staff never: it's told it rests in words
            if (g.restsThroughout && used[0] + g.voices > perStaff) {
                g.staff = -1;
                tacet.insert(tacet.end(), g.horns.begin(), g.horns.end());
                continue;
            }
            g.staff = used[0] + g.voices <= perStaff ? 0 : 1;
            g.base = used[g.staff];
            used[g.staff] += g.voices;
            if (used[1] > perStaff) {
                r.problem = QString("bar %1: more than eight lines at once").arg(bars[run.first]->no() + 1);
                return r;
            }
        }
        r.secondStaff = r.secondStaff || used[1] > 0;
        if (run.groups > 1) {
            ++r.splitPassages;
        }

        if (!tacet.empty()) {
            labels.push_back({ 0, run.firstPlayed, used[0] > 1 ? 1 : 0,
                               muse::qtrc("starscore", "%1 tacet").arg(arrangementLabel(tacet)) });
        }
        for (const G& g : gs) {
            if (g.staff < 0) {
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
            if (run.groups > 1) {
                labels.push_back({ g.staff, run.firstPlayed, g.base, arrangementLabel(g.horns) });
            }
        }
        if (run.groups == 1 && previousGroups > 1) {
            labels.push_back({ 0, run.firstPlayed, 0, muse::qtrc("starscore", "All") });
        }
        previousGroups = run.groups;
    }

    // 4. into the copy: the largest arrangement's part emptied, a second staff if needed, the lines and the labels
    if (r.secondStaff) {
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
                const QString words = toTextBase(e)->plainText().toQString();
                bool dup = false;
                if (e->isHarmony() || e->isDynamic()) {
                    dup = !seen.insert(std::make_pair(int(e->type()) * 100000 + int(seg->rtick().ticks()), words)).second;
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
