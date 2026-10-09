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
#include "starscoreengraving.h"

#include <cmath>

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
#include "engraving/dom/linkedobjects.h"
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
    int no = 0;   // its bar number (Measure::no(), 0-based)
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
    return convertTimeOf(globalContext()->currentProject(), false, false);
}

//! toDouble false: "Convert from double time" (every note half as long, two bars of x/4 become one, x/8 -> x/16, the
//! tempo halved). toDouble true, the other way (1.18.25, for the export's Double-Time sheets): every note twice as long,
//! a bar of x/4 becomes two, x/8 -> x/4, x/16 -> x/8, the tempo doubled. Works on any loaded song, shown or not.
//! throwawayCopy: a copy nobody will undo in (the export's Half-Time and Double-Time sheets): the song's old bars,
//! which the undo step would hold on to, are let go as soon as they are out of the score (they are as much music again
//! as the song, in every part book: Dream of Mushroom's export peaked at 3.8 GB)
RetVal<QString> StarScoreService::convertTimeOf(const INotationProjectPtr& project, bool toDouble, bool keepBarNumbers,
                                                bool throwawayCopy)
{
    IMasterNotationPtr master = project ? project->masterNotation() : nullptr;
    MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms || !ms->firstMeasure()) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "no score open"));
    }
    INotationPtr n = master->notation();
    const size_t nstaves = ms->nstaves();
    n->undoStack()->prepareChanges(TranslatableString::untranslatable(toDouble ? "Convert to double time" : "Convert from double time"));
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
        b.no = m->no();
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
    for (size_t i = 0; toDouble && i < old.size(); ++i) {
        // every bar at twice its length: x/4 (and x/2) bars become two bars of the same time signature; an odd-length
        // bar (a pickup) becomes one bar twice as long; x/8 and shorter become one bar of the next longer note
        const OldBar& b = old[i];
        const Fraction two(2, 1);
        if (b.sig.denominator() >= 8) {
            bars.push_back({ Fraction(b.sig.numerator(), b.sig.denominator() / 2), b.tick, b.len, two, Fraction() });
            continue;
        }
        if (b.len == b.sig) {
            const Fraction half = b.len / 2;
            bars.push_back({ b.sig, b.tick, half, two, Fraction() });
            bars.push_back({ b.sig, b.tick + half, half, two, Fraction() });
            continue;
        }
        // the new length counted in the old time signature's beats (a 1/4 pickup in 4/4 becomes a 2/4 bar)
        const Fraction doubled = b.len * 2;
        const int beats = (doubled * b.sig.denominator()).numerator() / std::max(1, (doubled * b.sig.denominator()).denominator());
        const Fraction sig = beats > 0 && Fraction(beats, b.sig.denominator()) == doubled ? Fraction(beats, b.sig.denominator())
                             : doubled.reduced();
        bars.push_back({ sig, b.tick, b.len, two, Fraction() });
    }
    for (size_t i = 0; !toDouble && i < old.size(); ++i) {
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
    if (throwawayCopy) {
        // the edits so far closed as a step of their own and forgotten, with the old bars they hold; the rest of the
        // conversion is a new step. The score isn't laid out again for the closing (its updates are held): the layouts
        // the conversion makes stay the same ones, so the sheets come out the same.
        ms->lockUpdates(true);
        n->undoStack()->commitChanges();
        ms->undoStack()->clearHistory();
        n->undoStack()->prepareChanges(TranslatableString::untranslatable(toDouble ? "Convert to double time" : "Convert from double time"));
        ms->lockUpdates(false);
        LOGI() << "[starscore] double time: the old bars let go";
    }

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
            // at half the tempo (twice it for double time), and the number in the marking too ("= 216" becomes "= 108")
            const double factor = toDouble ? 2.0 : 0.5;
            // every number after the "=" ("= 133-153" becomes "= 266-306"), rounded to a whole number
            static const QRegularExpression number("(\\d+(?:\\.\\d+)?)");
            QString text = a.xml.toQString();
            const int eq = text.indexOf('=');
            if (eq >= 0) {
                // (only in the words: the tags, e.g. <font size="10"/>, keep their numbers)
                static const QRegularExpression tag("<[^>]*>");
                const QString tail = text.mid(eq);
                QString out;
                auto scaled = [&](const QString& words) {
                    QString r;
                    int last = 0;
                    QRegularExpressionMatchIterator it = number.globalMatch(words);
                    while (it.hasNext()) {
                        const QRegularExpressionMatch nm = it.next();
                        const double bpm = nm.captured(1).toDouble() * factor;
                        r += words.mid(last, nm.capturedStart(1) - last);
                        // whole numbers (Joel, 8 Oct 2026: "= 66.5-76.5" read as "= 67-77")
                        r += QString::number(std::lround(bpm));
                        last = nm.capturedEnd(1);
                    }
                    return r + words.mid(last);
                };
                int last = 0;
                QRegularExpressionMatchIterator tags = tag.globalMatch(tail);
                while (tags.hasNext()) {
                    const QRegularExpressionMatch tm = tags.next();
                    out += scaled(tail.mid(last, tm.capturedStart() - last)) + tm.captured();
                    last = tm.capturedEnd();
                }
                out += scaled(tail.mid(last));
                text = text.left(eq) + out;
            }
            TempoText* tt = a.clone && a.clone->isTempoText() ? toTempoText(a.clone) : Factory::createTempoText(seg);
            tt->setTrack(a.track);
            tt->setParent(seg);
            tt->setXmlText(String::fromQString(text));
            tt->setTempo(BeatsPerSecond(a.beatsPerSecond * factor));
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

    // 9b. bars filled with slashes (Format › Fill with slashes: stemless slash heads) get one slash per beat again, as
    // many as the time signature's top number (4/4: four quarter-note slashes, never two halves); 2/2 also gets four
    // quarter-note slashes (Joel, 7 Oct 2026: FYKB's section D came out in half-note slashes)
    int slashBars = 0;
    auto isFillSlash = [](const Chord* c) {
        if (!c->noStem() || c->notes().empty()) {
            return false;
        }
        return std::all_of(c->notes().begin(), c->notes().end(), [](const Note* n) {
            return n->headGroup() == NoteHeadGroup::HEAD_SLASH;
        });
    };
    for (Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        const Fraction sig = m->timesig();
        if (m->ticks() != sig) {
            continue;   // a pickup or other odd-length bar: left as it is
        }
        const bool cut = sig == Fraction(2, 2);
        const Fraction beat = cut ? Fraction(1, 4) : Fraction(1, sig.denominator());
        for (track_idx_t track = 0; track < ms->ntracks(); ++track) {
            std::vector<ChordRest*> crs;
            bool slashes = false;
            bool other = false;
            for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                EngravingItem* e = seg->element(track);
                if (!e) {
                    continue;
                }
                ChordRest* cr = toChordRest(e);
                if (cr->tuplet() || (cr->isChord() && !isFillSlash(toChord(cr)))) {
                    other = true;
                    break;
                }
                slashes = slashes || cr->isChord();
                crs.push_back(cr);
            }
            if (other || !slashes) {
                continue;
            }
            // Each stretch of slashes in a row, between rests (1.18.26: a bar with rests among its slashes, GIJO's "half
            // rest, four eighth-note slashes", lost its rests and became slashes all through). A stretch that starts and
            // ends on a beat gets one slash per beat over the same beats; the rests stay where they are. A stretch off
            // the beat is left as it is.
            for (size_t i = 0; i < crs.size();) {
                if (!crs[i]->isChord()) {
                    ++i;
                    continue;
                }
                size_t j = i + 1;
                while (j < crs.size() && crs[j]->isChord() && crs[j - 1]->tick() + crs[j - 1]->ticks() == crs[j]->tick()) {
                    ++j;
                }
                const Fraction start = crs[i]->tick() - m->tick();
                const Fraction end = crs[j - 1]->tick() + crs[j - 1]->ticks() - m->tick();
                const Fraction first = (start / beat).reduced();
                const Fraction last = (end / beat).reduced();
                const int beats = first.denominator() == 1 && last.denominator() == 1 ? last.numerator() - first.numerator() : 0;
                bool already = int(j - i) == beats;
                for (size_t k = i; k < j && already; ++k) {
                    already = crs[k]->ticks() == beat;
                }
                const bool wholeBar = start.isZero() && end == m->ticks();
                if (beats <= 0 || already) {
                    i = j;
                    continue;
                }
                // the slashes' note (pitch and spelling), as the first one had it
                const Note* model = toChord(crs[i])->notes().front();
                NoteVal nv(model->pitch());
                nv.tpc1 = model->tpc1();
                nv.tpc2 = model->tpc2();
                nv.headGroup = NoteHeadGroup::HEAD_SLASH;
                for (size_t k = i; k < j; ++k) {
                    ms->undoRemoveElement(crs[k]);
                }
                ms->setRest(m->tick() + start, track, end - start, false, nullptr, wholeBar);
                for (int b = 0; b < beats; ++b) {
                    Measure* mm = ms->tick2measure(m->tick());
                    Segment* seg = mm->undoGetSegment(SegmentType::ChordRest, m->tick() + start + beat * b);
                    seg = ms->setNoteRest(seg, track, nv, beat);
                    Chord* c = seg && seg->element(track) && seg->element(track)->isChord() ? toChord(seg->element(track)) : nullptr;
                    if (!c) {
                        continue;
                    }
                    if (c->links()) {
                        for (EngravingObject* l : *c->links()) {
                            toChord(l)->setSlash(true, true);
                        }
                    } else {
                        c->setSlash(true, true);
                    }
                }
                ++slashBars;
                i = j;
            }
        }
    }
    LOGI() << "[starscore] double time: " << slashBars << " stretch(es) of slashes made one slash per beat again";

    // 10. keepBarNumbers (the Half-Time and Double-Time sheets, Joel 7 Oct 2026): each new bar numbered like the bar of
    // the song it starts in, so the sheets' bar numbers match the standard sheets. Both halves of a doubled bar carry
    // its number (the first is left out of the count), and a bar made of two counts as both (the next bar skips one).
    if (keepBarNumbers) {
        ms->setLayoutAll();
        ms->doLayout();
        int counter = 0;   // what MuseScore would number the next bar (renumbering: no = counter + offset; a bar left
                           // out of the count doesn't move the counter on)
        Measure* m = ms->firstMeasure();
        for (size_t i = 0; i < bars.size() && m; ++i, m = m->nextMeasure()) {
            auto wanted = [&](size_t k) {
                int no = 0;
                for (const OldBar& b : old) {
                    if (b.tick <= bars[k].oldTick) {
                        no = b.no;
                    }
                }
                return no;
            };
            const int want = wanted(i);
            const bool shared = i + 1 < bars.size() && wanted(i + 1) == want;   // the next new bar has the same number
            if (m->irregular() != shared) {
                m->undoChangeProperty(Pid::IRREGULAR, shared);
            }
            if (m->noOffset() != want - counter) {
                m->undoChangeProperty(Pid::NO_OFFSET, want - counter);
            }
            counter = shared ? want : want + 1;
        }
        starscore::syncBarNumbering(ms, starscore::BarNumberingSync::Undoable);
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
                                                    "changed, %5 of %6 stretch(es) of music pasted, %7 tie(s), line(s) and "
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
