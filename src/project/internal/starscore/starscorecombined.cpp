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
#include "engraving/dom/hairpin.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftext.h"
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
}
