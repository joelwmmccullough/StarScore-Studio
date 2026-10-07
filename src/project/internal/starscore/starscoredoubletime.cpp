/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — "Convert from double time" (Joel, 6 Oct 2026): a song written in double time (G.I. Jorge and
 * Shatter, at quarter = 216) rewritten in standard time. Every note, rest and marking keeps its place in the music at
 * half its written length; the tempo is halved, so the song sounds the same.
 *
 * The bars: in 2/4, 3/4, 4/4… two bars become one bar of the same time signature, counted within each stretch of bars
 * between rehearsal marks, repeats, double barlines and time signature changes; a bar left over at the end of a stretch
 * keeps its length (G.I. Jorge's fermata bar). In 7/8 and other eighth-note time signatures each bar becomes one bar of
 * sixteenths (7/8 -> 7/16), as Joel chose for Shatter.
 *
 * How: bar repeat signs are written out first (a repeated bar becomes half of a new bar, so the sign can't stay); the
 * music is copied; the score's bars are replaced by empty bars in the new layout; the music is pasted back at half its
 * length (MuseScore's "Paste half duration"); then what that paste leaves out is put back: rehearsal marks, system texts,
 * repeat barlines, double and final barlines. Hand-placed system and page breaks don't carry over: the parts are laid
 * out again from scratch.
 */

#include "starscoreservice.h"

#include <QRegularExpression>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/timesig.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/systemtext.h"
#include "engraving/dom/tempotext.h"
#include "engraving/dom/barline.h"
#include "engraving/dom/select.h"
#include "engraving/dom/mscore.h"
#include "engraving/dom/system.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/fermata.h"
#include "engraving/dom/spanner.h"
#include "engraving/rw/xmlreader.h"

#include "log.h"
#include "translation.h"

using namespace mu::project;
using namespace mu::engraving;
using namespace muse;

namespace {
struct OldBar {
    Fraction tick;
    Fraction len;
    Fraction sig;
    bool repeatStart = false;
    bool repeatEnd = false;
    int repeatCount = 2;
    BarLineType endType = BarLineType::NORMAL;
    bool startsMark = false;
};

struct NewBar {
    Fraction sig;
    Fraction oldTick;
    Fraction oldLen;
    Fraction scale;
    Fraction newTick;
};

struct Annotation {
    Fraction oldTick;
    String xml;
    track_idx_t track = 0;
    bool rehearsal = false;
    bool tempo = false;          // a tempo marking (the paste leaves those out too: it looks for "TempoText", files say "Tempo")
    double beatsPerSecond = 0.0;
    bool followText = true;
    TextBase* clone = nullptr;   // the marking itself (its placement, frame and style kept)
};

//! Where an old tick lands in the new layout
Fraction mapTick(const std::vector<NewBar>& bars, const Fraction& oldTick)
{
    for (const NewBar& b : bars) {
        if (oldTick >= b.oldTick && oldTick < b.oldTick + b.oldLen) {
            return b.newTick + (oldTick - b.oldTick) * b.scale;
        }
    }
    const NewBar& last = bars.back();
    return last.newTick + last.oldLen * last.scale;
}

//! The bars from `first` to `last`, every staff (or one), as MuseScore copies them
ByteArray copyBars(MasterScore* ms, Measure* first, Measure* last, staff_idx_t staffFrom, staff_idx_t staffTo)
{
    // (set directly, bar by bar: selecting bars snaps to multimeasure rests)
    ms->deselectAll();
    // (by ticks: a paste leaves its own range waiting to be selected, which a plain setRange doesn't replace)
    ms->selection().setRangeTicks(first->tick(), last->endTick(), staffFrom, staffTo + 1);
    ms->selection().updateSelectedElements();
    ByteArray data = ms->selection().mimeData();
    ms->deselectAll();
    return data;
}

Measure* measureAt(MasterScore* ms, size_t index)
{
    Measure* m = ms->firstMeasure();
    for (size_t i = 0; m && i < index; ++i) {
        m = m->nextMeasure();
    }
    return m;
}

//! (for the log) bars and chords in the score now
QString stageInfo(MasterScore* ms)
{
    int bars = 0;
    int chords = 0;
    for (Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        ++bars;
    }
    for (Segment* s = ms->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (track_idx_t t = 0; t < ms->ntracks(); ++t) {
            chords += s->element(t) && s->element(t)->isChord() ? 1 : 0;
        }
    }
    return QString("%1 bars, %2 chords, ends at %3").arg(bars).arg(chords).arg(ms->lastMeasure() ? ms->lastMeasure()->endTick().toString().toQString() : QString());
}

size_t measuresFrom(Measure* m)
{
    size_t n = 0;
    for (; m; m = m->nextMeasure()) {
        ++n;
    }
    return n;
}
}

RetVal<QString> StarScoreService::convertFromDoubleTime()
{
    INotationProjectPtr project = globalContext()->currentProject();
    IMasterNotationPtr master = project ? project->masterNotation() : nullptr;
    MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms || !ms->firstMeasure()) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "no score open"));
    }
    INotationPtr n = master->notation();
    const size_t nstaves = ms->nstaves();
    n->undoStack()->prepareChanges(TranslatableString::untranslatable("Convert from double time"));
    // multimeasure rests off while the bars are copied and rebuilt (put back at the end)
    const bool mmRests = ms->style().styleB(Sid::createMultiMeasureRests);
    if (mmRests) {
        ms->undoChangeStyleVal(Sid::createMultiMeasureRests, false);
        ms->setLayoutAll();
    ms->doLayout();
    }

    // 1. bar repeat signs written out, left to right (a repeat of a repeat copies what's already written out)
    int writtenOut = 0;
    for (Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        for (staff_idx_t s = 0; s < nstaves; ++s) {
            if (!m->isMeasureRepeatGroup(s) || m->isMeasureRepeatGroupWithPrevM(s)) {
                continue;
            }
            const int count = m->measureRepeatNumMeasures(s);
            Measure* src = m;
            for (int i = 0; i < count && src; ++i) {
                src = src->prevMeasure();
            }
            if (!src) {
                continue;
            }
            Measure* srcLast = src;
            for (int i = 1; i < count && srcLast; ++i) {
                srcLast = srcLast->nextMeasure();
            }
            const ByteArray data = copyBars(ms, src, srcLast, s, s);
            Segment* dst = m->first(SegmentType::ChordRest);
            XmlReader reader(data);
            if (dst && !data.empty() && ms->pasteStaff(reader, dst, s)) {
                ++writtenOut;
            }
        }
    }
    ms->setLayoutAll();
    ms->doLayout();

    LOGI() << "[starscore] double time: after writing out bar repeats " << stageInfo(ms);
    // 2. the bars as they are: lengths, time signatures, repeats, barlines; rehearsal marks and system texts
    std::vector<OldBar> old;
    std::vector<Annotation> annotations;
    for (Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        OldBar b;
        b.tick = m->tick();
        b.len = m->ticks();
        b.sig = m->timesig();
        b.repeatStart = m->repeatStart();
        b.repeatEnd = m->repeatEnd();
        b.repeatCount = m->repeatCount();
        b.endType = m->endBarLineType();
        for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            for (EngravingItem* a : seg->annotations()) {
                // the copy on the top staff (MuseScore shows system objects on more staves itself)
                if (a->track() != 0 && (a->isRehearsalMark() || a->isSystemText())) {
                    continue;
                }
                if (a->isTempoText()) {
                    const TempoText* tt = toTempoText(a);
                    annotations.push_back({ seg->tick(), tt->xmlText(), a->track(), false, true, tt->tempo().val, tt->followText(),
                                            toTextBase(a->clone()) });
                } else if (a->isRehearsalMark() || a->isSystemText()) {
                    annotations.push_back({ seg->tick(), toTextBase(a)->xmlText(), a->track(), a->isRehearsalMark(), false, 0.0, true,
                                            toTextBase(a->clone()) });
                    if (a->isRehearsalMark() && seg->tick() == m->tick()) {
                        b.startsMark = true;
                    }
                }
            }
        }
        old.push_back(b);
    }
    const Fraction oldEnd = old.back().tick + old.back().len;

    // 3. the new bars
    std::vector<NewBar> bars;
    size_t runStart = 0;
    auto endsRun = [&](size_t i) {   // does bar i end a stretch?
        if (i + 1 >= old.size()) {
            return true;
        }
        const OldBar& a = old[i];
        const OldBar& b = old[i + 1];
        return a.sig != b.sig || a.repeatEnd || b.repeatStart || b.startsMark
               || (a.endType != BarLineType::NORMAL && a.endType != BarLineType::BROKEN && a.endType != BarLineType::DOTTED)
               || a.len != a.sig || b.len != b.sig;
    };
    for (size_t i = 0; i < old.size(); ++i) {
        if (!endsRun(i)) {
            continue;
        }
        const Fraction sig = old[runStart].sig;
        if (sig.denominator() <= 4) {
            size_t k = runStart;
            for (; k + 1 <= i; k += 2) {
                bars.push_back({ sig, old[k].tick, old[k].len + old[k + 1].len, Fraction(1, 2), Fraction() });
            }
            if (k == i) {   // one left over: kept as it is
                bars.push_back({ old[k].len.reduced(), old[k].tick, old[k].len, Fraction(1, 1), Fraction() });
            }
        } else {
            for (size_t k = runStart; k <= i; ++k) {
                bars.push_back({ Fraction(old[k].sig.numerator(), old[k].sig.denominator() * 2), old[k].tick, old[k].len,
                                 Fraction(1, 2), Fraction() });
            }
        }
        runStart = i + 1;
    }
    Fraction t(0, 1);
    for (NewBar& b : bars) {
        b.newTick = t;
        t += b.oldLen * b.scale;
    }

    LOGI() << "[starscore] double time: planned " << bars.size() << " new bars from " << old.size();
    // 4. the music copied, in stretches pasted at one scale
    struct Chunk {
        Fraction oldTick;
        Fraction newTick;
        Fraction scale;
        ByteArray data;
    };
    std::vector<Chunk> chunks;
    for (size_t i = 0; i < bars.size();) {
        size_t j = i;
        while (j + 1 < bars.size() && bars[j + 1].scale == bars[i].scale) {
            ++j;
        }
        Measure* first = ms->tick2measure(bars[i].oldTick);
        Measure* last = ms->tick2measure(bars[j].oldTick + bars[j].oldLen - Fraction(1, 1920));
        chunks.push_back({ bars[i].oldTick, bars[i].newTick, bars[i].scale, copyBars(ms, first, last, 0, nstaves - 1) });
        {
            const std::string head(reinterpret_cast<const char*>(chunks.back().data.constData()),
                                   std::min<size_t>(200, chunks.back().data.size()));
            LOGI() << "[starscore] double time: copied bars " << first->no() + 1 << "-" << last->no() + 1 << ": "
                   << QString::fromStdString(head).simplified();
        }
        i = j + 1;
    }

    // 4b. what a paste in stretches loses at their edges, kept to put back: ties from one stretch into the next (G.I.
    // Jorge's fermata bar keeps its length, so it's a stretch of its own), other lines across an edge, rit./accel. lines
    // (the paste never takes those), and fermatas over a stretch's last barline (the copy stops before it)
    std::vector<Fraction> edges;
    for (size_t c = 1; c < chunks.size(); ++c) {
        edges.push_back(chunks[c].oldTick);
    }
    auto crossesEdge = [&](const Fraction& from, const Fraction& to) {
        return std::any_of(edges.begin(), edges.end(), [&](const Fraction& e) { return from < e && to >= e; });
    };
    struct TieKept {
        track_idx_t track;
        int pitch;
        Fraction from;
        Fraction to;
    };
    std::vector<TieKept> ties;
    for (Segment* seg = ms->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (track_idx_t t = 0; t < ms->ntracks(); ++t) {
            EngravingItem* e = seg->element(t);
            if (!e || !e->isChord()) {
                continue;
            }
            for (Note* note : toChord(e)->notes()) {
                Tie* tie = note->tieFor();
                Note* end = tie ? tie->endNote() : nullptr;
                if (end && crossesEdge(seg->tick(), end->chord()->tick())) {
                    ties.push_back({ t, note->pitch(), seg->tick(), end->chord()->tick() });
                }
            }
        }
    }
    struct SpannerKept {
        Spanner* clone;
        Fraction from;
        Fraction to;
    };
    std::vector<SpannerKept> lines;
    for (const auto& [tick, sp] : ms->spanner()) {
        if (sp->isTie() || sp->isSlur() /* chord to chord: kept by the paste unless they cross an edge */ && !crossesEdge(sp->tick(), sp->tick2())) {
            continue;
        }
        if (sp->isGradualTempoChange() || crossesEdge(sp->tick(), sp->tick2())) {
            lines.push_back({ toSpanner(sp->clone()), sp->tick(), sp->tick2() });
        }
    }
    struct FermataKept {
        Fermata* clone;
        Fraction barEnd;
    };
    std::vector<FermataKept> barFermatas;
    for (Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        const bool chunkEnd = std::any_of(edges.begin(), edges.end(), [&](const Fraction& e) { return m->endTick() == e; })
                              || !m->nextMeasure();
        Segment* es = chunkEnd ? m->findSegmentR(SegmentType::EndBarLine, m->ticks()) : nullptr;
        for (EngravingItem* a : es ? es->annotations() : std::vector<EngravingItem*>()) {
            if (a->isFermata()) {
                barFermatas.push_back({ toFermata(a->clone()), m->endTick() });
            }
        }
    }

    // 5. the bars replaced: new empty bars in front (they take the first bar's clefs, keys and time signature), the old
    // ones removed, then the time signatures set bar by bar
    Measure* oldFirst = ms->firstMeasure();
    Measure* oldLast = ms->lastMeasure();
    Score::InsertMeasureOptions options;
    for (size_t i = 0; i < bars.size(); ++i) {
        ms->Score::insertMeasure(ElementType::MEASURE, oldFirst, options);
    }
    LOGI() << "[starscore] double time: after inserting " << stageInfo(ms);
    ms->deleteMeasures(oldFirst, oldLast);
    LOGI() << "[starscore] double time: after deleting the old bars " << stageInfo(ms);
    for (size_t i = 0; i < bars.size(); ++i) {
        Measure* m = measureAt(ms, i);
        if (!m) {
            break;
        }
        if (m->timesig() != bars[i].sig || m->ticks() != bars[i].sig) {
            TimeSig* ts = Factory::createTimeSig(ms->dummy()->segment());
            ts->setSig(bars[i].sig);
            ms->cmdAddTimeSig(m, 0, ts, false);
            ms->setLayoutAll();
            ms->doLayout();
            m = measureAt(ms, i);
            LOGI() << "[starscore] double time: time signature " << bars[i].sig.toString() << " at bar " << i + 1 << ": " << stageInfo(ms);
        }
        // as many bars as the layout needs from here on (a new time signature rebars the empty bars after it)
        const size_t have = measuresFrom(m);
        const size_t need = bars.size() - i;
        if (have < need) {
            ms->appendMeasures(int(need - have));
        } else if (have > need) {
            ms->deleteMeasures(measureAt(ms, bars.size()), ms->lastMeasure());
        }
    }
    ms->setLayoutAll();
    ms->doLayout();

    LOGI() << "[starscore] double time: after the new bars " << stageInfo(ms);
    {
        std::set<const void*> systems;
        for (System* sys : ms->systems()) {
            systems.insert(sys);
        }
        int i = 0;
        int stale = 0;
        const void* prev = nullptr;
        for (MeasureBase* mb = ms->first(); mb; mb = mb->next(), ++i) {
            const void* parent = mb->explicitParent();
            if (parent && !systems.count(parent)) {
                ++stale;
                if (stale < 5) {
                    LOGW() << "[starscore] double time: bar " << i << " has a system that isn't the score's (" << (mb->isMeasure() ? "measure" : "box") << ")";
                }
            }
            if (mb->prev() != prev) {
                LOGW() << "[starscore] double time: bar " << i << " prev link broken";
            }
            prev = mb;
        }
        LOGI() << "[starscore] double time: checked " << i << " measure bases, " << stale << " with stale systems, last next " << (ms->last() ? (ms->last()->next() ? "SET" : "null") : "-");
    }
    // 6. the music pasted back
    int pasted = 0;
    for (const Chunk& c : chunks) {
        Measure* m = ms->tick2measure(c.newTick);
        Segment* seg = m ? m->first(SegmentType::ChordRest) : nullptr;
        XmlReader reader(c.data);
        if (seg && ms->pasteStaff(reader, seg, 0, c.scale)) {
            ++pasted;
        } else {
            LOGW() << "[starscore] double time: a stretch of music couldn't be pasted at " << c.newTick.toString();
        }
    }
    ms->setLayoutAll();
    ms->doLayout();

    LOGI() << "[starscore] double time: after pasting " << stageInfo(ms);
    // 7. what the paste leaves out: rehearsal marks, system texts and tempo markings, repeat barlines, double and final
    // barlines; ties, lines and barline fermatas at the edges of the stretches
    int temposHalved = 0;
    int edgesRestored = 0;
    auto noteAt = [&](track_idx_t track, int pitch, const Fraction& at) -> Note* {
        Measure* m = ms->tick2measure(at);
        Segment* seg = m ? m->findSegment(SegmentType::ChordRest, at) : nullptr;
        EngravingItem* e = seg ? seg->element(track) : nullptr;
        if (!e || !e->isChord()) {
            return nullptr;
        }
        for (Note* n : toChord(e)->notes()) {
            if (n->pitch() == pitch) {
                return n;
            }
        }
        return nullptr;
    };
    for (const TieKept& k : ties) {
        Note* a = noteAt(k.track, k.pitch, mapTick(bars, k.from));
        Note* b = noteAt(k.track, k.pitch, mapTick(bars, k.to));
        if (!a || !b || a->tieFor()) {
            continue;
        }
        Tie* tie = Factory::createTie(a);
        tie->setStartNote(a);
        tie->setEndNote(b);
        tie->setTrack(a->track());
        tie->setTick(a->chord()->tick());
        tie->setTicks(b->chord()->tick() - a->chord()->tick());
        ms->undoAddElement(tie);
        ++edgesRestored;
    }
    for (const SpannerKept& k : lines) {
        k.clone->setTick(mapTick(bars, k.from));
        k.clone->setTick2(mapTick(bars, k.to));
        k.clone->setStartElement(nullptr);
        k.clone->setEndElement(nullptr);
        ms->undoAddElement(k.clone);
        ++edgesRestored;
    }
    for (const FermataKept& k : barFermatas) {
        const Fraction end = mapTick(bars, k.barEnd);
        Measure* m = end > Fraction(0, 1) ? ms->tick2measure(end - Fraction(1, 1920)) : nullptr;
        if (!m || m->endTick() != end) {
            delete k.clone;
            continue;
        }
        Segment* es = m->undoGetSegmentR(SegmentType::EndBarLine, m->ticks());
        const bool there = std::any_of(es->annotations().begin(), es->annotations().end(), [&](EngravingItem* a) {
            return a->isFermata() && a->track() == k.clone->track();
        });
        if (there) {
            delete k.clone;
            continue;
        }
        k.clone->setParent(es);
        ms->undoAddElement(k.clone);
        ++edgesRestored;
    }
    for (const Annotation& a : annotations) {
        const Fraction at = mapTick(bars, a.oldTick);
        Measure* m = ms->tick2measure(at);
        if (!m) {
            continue;
        }
        Segment* seg = m->findSegment(SegmentType::ChordRest, at);
        if (!seg) {
            for (Segment* s = m->first(SegmentType::ChordRest); s && s->tick() <= at; s = s->next(SegmentType::ChordRest)) {
                seg = s;
            }
        }
        if (!seg) {
            continue;
        }
        if (a.tempo) {
            // at half the tempo, and the number in the marking too ("= 216" becomes "= 108")
            static const QRegularExpression number("=\\s*(\\d+(?:\\.\\d+)?)");
            QString text = a.xml.toQString();
            const QRegularExpressionMatch nm = number.match(text);
            if (nm.hasMatch()) {
                const double bpm = nm.captured(1).toDouble() / 2.0;
                const QString half = bpm == std::floor(bpm) ? QString::number(int(bpm)) : QString::number(bpm, 'f', 1);
                text.replace(nm.capturedStart(1), nm.capturedLength(1), half);
            }
            TempoText* tt = a.clone && a.clone->isTempoText() ? toTempoText(a.clone) : Factory::createTempoText(seg);
            tt->setTrack(a.track);
            tt->setParent(seg);
            tt->setXmlText(String::fromQString(text));
            tt->setTempo(BeatsPerSecond(a.beatsPerSecond / 2.0));
            tt->setFollowText(a.followText);
            ms->undoAddElement(tt);
            ++temposHalved;
            continue;
        }
        TextBase* el = a.clone ? a.clone
                       : a.rehearsal ? static_cast<TextBase*>(Factory::createRehearsalMark(seg))
                       : static_cast<TextBase*>(Factory::createSystemText(seg));
        el->setTrack(a.track);
        el->setParent(seg);
        el->setXmlText(a.xml);
        ms->undoAddElement(el);
    }
    for (const OldBar& b : old) {
        if (b.repeatStart) {
            const Fraction at = mapTick(bars, b.tick);
            for (Score* s : ms->scoreList()) {
                if (Measure* lm = s->tick2measure(at); lm && lm->tick() == at) {
                    lm->undoChangeProperty(Pid::REPEAT_START, true);
                }
            }
        }
        const Fraction end = mapTick(bars, b.tick + b.len);
        Measure* em = end > Fraction(0, 1) ? ms->tick2measure(end - Fraction(1, 1920)) : nullptr;
        if (!em || em->endTick() != end) {
            continue;   // the old bar ends inside a new bar (the first of a pair)
        }
        if (b.repeatEnd) {
            for (Score* s : ms->scoreList()) {
                if (Measure* lm = s->tick2measure(em->tick())) {
                    lm->undoChangeProperty(Pid::REPEAT_END, true);
                    lm->undoChangeProperty(Pid::REPEAT_COUNT, b.repeatCount);
                }
            }
        } else if (b.endType != BarLineType::NORMAL && b.endType != BarLineType::END_REPEAT
                   && b.endType != BarLineType::END_START_REPEAT && b.endType != BarLineType::START_REPEAT) {
            Segment* es = em->findSegmentR(SegmentType::EndBarLine, em->ticks());
            BarLine* bl = es && es->element(0) && es->element(0)->isBarLine() ? toBarLine(es->element(0)) : nullptr;
            if (bl) {
                ms->undoChangeBarLineType(bl, b.endType, true);
            }
        }
    }

    // 9. a bar of rests is one whole-bar rest again (a whole-bar rest pasted at half length came out as two half rests)
    int restBars = 0;
    for (Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        for (staff_idx_t st = 0; st < nstaves; ++st) {
            const track_idx_t track = st * VOICES;   // the first voice (the others are left as they are)
            std::vector<EngravingItem*> rests;
            bool onlyRests = true;
            for (Segment* seg = m->first(SegmentType::ChordRest); seg && onlyRests; seg = seg->next(SegmentType::ChordRest)) {
                EngravingItem* e = seg->element(track);
                if (!e) {
                    continue;
                }
                if (!e->isRest() || toRest(e)->isMeasureRepeat() || toChordRest(e)->tuplet()) {
                    onlyRests = false;
                } else {
                    rests.push_back(e);
                }
            }
            if (!onlyRests || rests.size() < 2) {
                continue;
            }
            for (EngravingItem* r : rests) {
                ms->undoRemoveElement(r);
            }
            ms->setRest(m->tick(), track, m->ticks(), false, nullptr, true);
            ++restBars;
        }
    }

    if (mmRests) {
        ms->undoChangeStyleVal(Sid::createMultiMeasureRests, true);
    }
    n->undoStack()->commitChanges();
    ms->setLayoutAll();
    ms->setLayoutAll();
    ms->doLayout();
    project->markAsUnsaved();
    n->notationChanged().notify();

    const QString summary = muse::qtrc("starscore", "%1 bars became %2 (%3 bar repeat sign(s) written out, %4 tempo marking(s) "
                                                    "halved, %5 of %6 stretch(es) of music pasted, %7 tie(s), line(s) and "
                                                    "fermata(s) put back at their edges).")
                            .arg(old.size()).arg(bars.size()).arg(writtenOut).arg(temposHalved).arg(pasted).arg(chunks.size())
                            .arg(edgesRestored);
    LOGI() << "[starscore] convert from double time: " << summary;
    if (pasted != int(chunks.size())) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, summary.toStdString());
    }
    (void)oldEnd;
    return RetVal<QString>::make_ok(summary);
}
