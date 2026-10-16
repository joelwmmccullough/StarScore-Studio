/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "starscoreengraving.h"
#include "starscoreannotations.h"

#include <algorithm>
#include <map>
#include <set>

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/barline.h"
#include "engraving/dom/textbase.h"
#include "engraving/dom/tempotext.h"
#include "engraving/dom/box.h"
#include "engraving/dom/spanner.h"
#include "engraving/editing/editproperty.h"
#include "engraving/dom/marker.h"
#include "engraving/dom/jump.h"
#include "engraving/dom/page.h"
#include "engraving/dom/system.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/accidental.h"
#include "engraving/dom/clef.h"
#include "engraving/dom/notedot.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/articulation.h"
#include "engraving/types/symnames.h"
#include "engraving/dom/select.h"
#include "engraving/dom/timesig.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/range.h"
#include "engraving/dom/slur.h"
#include "engraving/editing/editmeasures.h"
#include "engraving/editing/editspanner.h"
#include "engraving/compat/dummyelement.h"
#include "engraving/dom/systemlock.h"
#include "engraving/editing/editsystemlocks.h"
#include "engraving/style/style.h"

using namespace mu::engraving;

namespace mu::project::starscore {
static Measure* starscoreMeasureStartingAt(Score* score, const Fraction& tick)
{
    Measure* m = score->tick2measure(tick);
    if (m && m->tick() == tick) {
        return m;
    }
    return nullptr;
}

static Measure* starscoreMeasureEndingAt(Score* score, const Fraction& endTick)
{
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->endTick() == endTick) {
            return m;
        }
        if (m->tick() >= endTick) {
            break;
        }
    }
    return nullptr;
}

LayoutCopyResult copyLayout(const Score* source, const std::vector<Score*>& targets, const LayoutCopyOptions& options)
{
    LayoutCopyResult result;
    if (!source) {
        return result;
    }

    std::map<int, const Measure*> sourceByTick;
    for (const Measure* m = source->firstMeasure(); m; m = m->nextMeasure()) {
        sourceByTick[m->tick().ticks()] = m;
    }

    for (Score* target : targets) {
        if (!target || target == source) {
            continue;
        }

        bool changed = false;

        auto applyBreak = [&](Measure* m, bool want, bool have, LayoutBreakType type) {
            if (want && !have) {
                m->undoSetBreak(true, type);
                ++result.breaksAdded;
                changed = true;
            } else if (!want && have && options.replaceExisting) {
                m->undoSetBreak(false, type);
                ++result.breaksRemoved;
                changed = true;
            }
        };

        // Bars inside or ending a locked system of the source get no line break or "keep together" here: adding the
        // lock takes them out again, and a break added and taken out in the same undo step is deleted twice when a
        // throwaway copy is closed (a crash found with the combined parts, 13 Oct 2026)
        std::vector<std::pair<int, int> > lockedRanges;
        if (options.systemLocks) {
            for (const SystemLock* lock : source->systemLocks()->allLocks()) {
                lockedRanges.emplace_back(lock->startMB()->tick().ticks(), lock->endMB()->endTick().ticks());
            }
        }
        auto locked = [&](const Measure* m) {
            return std::any_of(lockedRanges.begin(), lockedRanges.end(), [&](const std::pair<int, int>& r) {
                return m->tick().ticks() >= r.first && m->tick().ticks() < r.second;
            });
        };
        for (Measure* m = target->firstMeasure(); m; m = m->nextMeasure()) {
            auto it = sourceByTick.find(m->tick().ticks());
            if (it == sourceByTick.end()) {
                continue;
            }
            const Measure* sm = it->second;
            const bool inLock = locked(m);

            if (options.pageBreaks) {
                applyBreak(m, sm->pageBreak(), m->pageBreak(), LayoutBreakType::PAGE);
            }
            // (in a locked system only taken away, never added)
            if (options.lineBreaks) {
                applyBreak(m, sm->lineBreak() && !inLock, m->lineBreak(), LayoutBreakType::LINE);
            }
            if (options.keepTogether) {
                applyBreak(m, sm->noBreak() && !inLock, m->noBreak(), LayoutBreakType::NOBREAK);
            }
        }

        if (options.systemLocks) {
            const std::vector<const SystemLock*> sourceLocks = source->systemLocks()->allLocks();
            const bool targetHasLocks = !target->systemLocks()->allLocks().empty();

            if (options.replaceExisting && targetHasLocks) {
                EditSystemLocks::undoRemoveAllLocks(target);
                changed = true;
            }

            const bool mmrests = target->style().styleB(Sid::createMultiMeasureRests);

            for (const SystemLock* lock : sourceLocks) {
                if (!lock->startMB()->isMeasure() || !lock->endMB()->isMeasure()) {
                    continue;
                }

                Measure* a = starscoreMeasureStartingAt(target, lock->startMB()->tick());
                Measure* b = starscoreMeasureEndingAt(target, lock->endMB()->endTick());
                if (!a || !b) {
                    continue;
                }
                if (mmrests) {
                    a = a->coveringMMRestOrThis();
                    b = b->coveringMMRestOrThis();
                }
                if (!(a == b || a->isBefore(b))) {
                    continue;
                }
                if (target->systemLocks()->lockContaining(a) || target->systemLocks()->lockContaining(b)) {
                    continue;
                }

                EditSystemLocks::undoAddSystemLock(target, new SystemLock(a, b));
                ++result.locksCopied;
                changed = true;
            }
        }

        if (changed) {
            target->setLayoutAll();
            ++result.scoresChanged;
        }
    }

    return result;
}

// C, C#, D, D#, E, F, F#, G, G#, A, A#, B
static const char* STARSCORE_PITCH_COLORS[12] = {
    "#C7C102", "#FF5DA1", "#028800", "#2DD3C0", "#001CA4", "#CC75F8",
    "#92D303", "#F47700", "#915700", "#AD000E", "#829EFF", "#770096"
};

static void starscoreCollectChordNotes(Chord* chord, std::vector<Note*>& notes)
{
    for (Note* n : chord->notes()) {
        notes.push_back(n);
    }
    for (Chord* grace : chord->graceNotes()) {
        for (Note* n : grace->notes()) {
            notes.push_back(n);
        }
    }
}

int colorNotes(Score* score, bool colorize)
{
    if (!score) {
        return 0;
    }

    std::vector<Note*> notes;
    const Selection& sel = score->selection();

    if (!sel.isNone()) {
        for (EngravingItem* e : sel.elements()) {
            if (e->isNote()) {
                notes.push_back(toNote(e));
            } else if (e->isChord()) {
                starscoreCollectChordNotes(toChord(e), notes);
            }
        }
    } else {
        for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
            for (track_idx_t t = 0; t < score->ntracks(); ++t) {
                EngravingItem* e = s->element(t);
                if (e && e->isChord()) {
                    starscoreCollectChordNotes(toChord(e), notes);
                }
            }
        }
    }

    // remove duplicates (a chord and its notes can both be selected)
    std::sort(notes.begin(), notes.end());
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());

    int changed = 0;
    for (Note* note : notes) {
        const Color color = colorize
                            ? Color::fromString(STARSCORE_PITCH_COLORS[((note->pitch() % 12) + 12) % 12])
                            : note->propertyDefault(Pid::COLOR).value<Color>();

        auto apply = [&](EngravingItem* item) {
            if (item && item->getProperty(Pid::COLOR).value<Color>() != color) {
                item->undoChangeProperty(Pid::COLOR, color);
                return true;
            }
            return false;
        };

        bool any = apply(note);
        any |= apply(note->accidental());
        for (NoteDot* dot : note->dots()) {
            any |= apply(dot);
        }
        if (any) {
            ++changed;
        }
    }

    return changed;
}

static const TimeSig* starscoreTimeSigAt(const Measure* m)
{
    const Segment* seg = m->findSegmentR(SegmentType::TimeSig, Fraction(0, 1));
    if (!seg) {
        return nullptr;
    }
    const EngravingItem* e = seg->element(0);
    return (e && e->isTimeSig()) ? toTimeSig(e) : nullptr;
}

//! Re-bars the bars first…last in one go into bars of the given lengths (the time signature each bar is written in
//! and, for a last bar cut short, its real length), keeping every note where it is: as MuseScore's
//! Score::rewriteMeasures, which only knows one time signature. Barlines move once, so no notes are split and tied at
//! barlines that then move again. Must be called inside a command on the master score.
static bool starscoreRebar(MasterScore* score, Measure* first, Measure* last, const std::vector<std::pair<Fraction, Fraction> >& bars)
{
    if (!first || !last || bars.empty()) {
        return false;
    }
    const Fraction startTick = first->tick();
    const Fraction endTick = last->endTick();

    // the old time signatures in the stretch go first, as deleting a time signature does (all scores, through the links):
    // the first one would otherwise be kept and changed, hidden if it was hidden
    std::vector<Segment*> oldTimeSigs;
    for (Measure* m = first; m; m = m->nextMeasure()) {
        if (Segment* seg = m->findSegment(SegmentType::TimeSig, m->tick())) {
            oldTimeSigs.push_back(seg);
        }
        if (m == last) {
            break;
        }
    }
    for (Segment* seg : oldTimeSigs) {
        score->undoRemoveElement(seg);
    }

    ScoreRange range;
    Measure* after = last->nextMeasure();
    range.read(first->first(), after ? after->first() : last->last());

    for (Score* s : score->scoreList()) {
        Measure* m1 = s->tick2measure(startTick);
        Measure* m2 = s->tick2measure(last->tick());
        if (!m1 || !m2) {
            continue;
        }
        auto spanners = s->spannerMap().findOverlapping(startTick.ticks(), endTick.ticks());
        for (auto it : spanners) {
            if (it.value->tick() >= startTick && it.value->tick() < endTick) {
                s->doUndoRemoveElement(it.value);
            }
        }
        s->undoRemoveMeasures(m1, m2, true);

        Measure* newFirst = nullptr;
        Measure* newLast = nullptr;
        Fraction tick = startTick;
        for (const auto& [nominal, actual] : bars) {
            Measure* m = Factory::createMeasure(s->dummy()->system());
            m->setPrev(newLast);
            if (newLast) {
                newLast->setNext(m);
            }
            m->setTimesig(nominal);
            m->setTicks(actual);
            m->setTick(tick);
            tick += actual;
            newLast = m;
            if (!newFirst) {
                newFirst = m;
            }
        }
        newFirst->setPrev(m1->prev());
        newLast->setNext(m2->next());
        s->undo(new InsertMeasures(newFirst, newLast));
    }

    if (!range.write(score, startTick)) {
        return false;
    }
    for (Score* s : score->scoreList()) {
        s->connectTies(true);
    }
    for (auto spanner : score->spannerMap().findOverlapping(startTick.ticks(), endTick.ticks())) {
        Slur* slur = spanner.value->isSlur() ? toSlur(spanner.value) : nullptr;
        if (slur && slur->startElement() && slur->endElement()) {
            score->undo(new ChangeStartEndSpanner(slur, slur->findStartCR(), slur->findEndCR()));
        }
    }
    return true;
}

QString applyAdditiveTimeSig(MasterScore* score, Measure* start, Measure* last, const std::vector<int>& numerators, int denominator)
{
    if (!score || !start || numerators.empty() || denominator <= 0) {
        return QString("Nothing to do");
    }
    for (int n : numerators) {
        if (n <= 0 || n > 64) {
            return QString("Each number must be between 1 and 64");
        }
    }
    if (denominator & (denominator - 1)) {
        return QString("The bottom number must be 1, 2, 4, 8, 16, 32 or 64");
    }

    if (start->isMMRest()) {
        start = start->mmRestFirst();
    }

    // The stretch: to the end of a selected range, else to the next bar with a time signature you can see (hidden ones
    // are an earlier additive pattern's, replaced now) or the end of the song
    Measure* stretchLast = last;
    if (!stretchLast) {
        stretchLast = start;
        for (Measure* m = start->nextMeasure(); m; m = m->nextMeasure()) {
            if (const TimeSig* ts = starscoreTimeSigAt(m); ts && ts->visible()) {
                break;
            }
            stretchLast = m;
        }
    }
    if (stretchLast->isMMRest()) {
        stretchLast = stretchLast->mmRestLast();
    }
    Measure* resumeBar = stretchLast->nextMeasure();
    const Fraction resumeSig = resumeBar && !starscoreTimeSigAt(resumeBar) ? resumeBar->timesig() : Fraction();
    const Fraction startTick = start->tick();
    const Fraction length = stretchLast->endTick() - startTick;

    // The bars: the pattern over and over, the last one cut short where the stretch ends (Joel, 13 Oct 2026). One
    // re-barring keeps the stretch exactly as long as it was, with every note where it was: adding a time signature a
    // bar at a time re-barred the rest of the stretch each time, filling out (or, when it was empty, cutting off) its
    // last bar, so the stretch grew or shrank and deleting the pattern again left a different number of bars.
    std::vector<std::pair<Fraction, Fraction> > bars;
    Fraction sum(0, 1);
    for (size_t i = 0; sum < length && bars.size() < 10000; ++i) {
        const Fraction sig(numerators[i % numerators.size()], denominator);
        const Fraction actual = std::min(sig, length - sum);
        bars.push_back({ sig, actual });
        sum += actual;
    }
    if (!starscoreRebar(score, start, stretchLast, bars)) {
        return QString("The bars could not be rewritten");
    }

    // The time signatures: the visible "4+4+4+3/8" on the first bar, a hidden one on every other bar. The bars are in
    // place, so adding them changes no barline.
    String numeratorText;
    for (size_t i = 0; i < numerators.size(); ++i) {
        if (i > 0) {
            numeratorText += u"+";
        }
        numeratorText += String::number(numerators[i]);
    }
    Fraction tick = startTick;
    for (size_t i = 0; i < bars.size(); ++i) {
        Measure* m = score->tick2measure(tick);
        if (!m || m->tick() != tick) {
            break;
        }
        if (i > 0 && numerators.size() == 1 && bars[i].first == bars[0].first) {
            tick += bars[i].second;   // one number ("3/4"): an ordinary time signature, shown once and nothing hidden
            continue;
        }
        TimeSig* ts = Factory::createTimeSig(score->dummy()->segment());
        ts->setSig(bars[i].first);
        if (i == 0) {
            if (numerators.size() > 1) {
                // One time signature: "4+4+4+3" over a single visible denominator. When only the numerator string is
                // set, MuseScore draws no denominator, so both are set.
                ts->setNumeratorString(numeratorText);
                ts->setDenominatorString(String::number(denominator));
            }
        } else {
            ts->setVisible(false);
            ts->setShowCourtesySig(false);
        }
        score->cmdAddTimeSig(m, 0, ts, false);
        tick += bars[i].second;
    }

    // after a selected range, the time signature from before comes back (visibly); an earlier additive pattern
    // carrying on past the range shows its time signature where it carries on
    if (resumeSig.isValid() && !resumeSig.isZero()) {
        if (Measure* m = score->tick2measure(tick); m && m->tick() == tick) {
            TimeSig* ts = Factory::createTimeSig(score->dummy()->segment());
            ts->setSig(resumeSig);
            score->cmdAddTimeSig(m, 0, ts, false);
        }
    } else if (last) {
        if (Measure* m = score->tick2measure(tick); m && m->tick() == tick) {
            if (Segment* seg = m->findSegment(SegmentType::TimeSig, m->tick())) {
                for (EngravingItem* e : seg->elist()) {
                    if (e && e->isTimeSig() && !e->visible()) {
                        e->undoChangeProperty(Pid::VISIBLE, true);
                        e->undoChangeProperty(Pid::SHOW_COURTESY, true);
                    }
                }
            }
        }
    }
    return QString();
}

static QString starscoreBarList(const std::set<int>& bars)
{
    QStringList out;
    auto it = bars.begin();
    while (it != bars.end()) {
        const int start = *it;
        int end = start;
        auto next = std::next(it);
        while (next != bars.end() && *next == end + 1) {
            end = *next;
            ++next;
        }
        out << (end > start ? QString("%1–%2").arg(start).arg(end) : QString::number(start));
        it = next;
    }
    return out.join(", ");
}

QString checkRanges(const Score* score)
{
    if (!score) {
        return QString();
    }

    struct Found {
        std::set<int> pro;
        std::set<int> amateur;
    };
    std::map<const Part*, Found> found;

    auto check = [&](const Chord* c, const Part* part, int bar) {
        const Instrument* in = part->instrument(c->tick());
        if (!in) {
            return;
        }
        for (const Note* n : c->notes()) {
            const int pitch = n->ppitch();
            if (pitch < in->minPitchP() || pitch > in->maxPitchP()) {
                found[part].pro.insert(bar);
            } else if (pitch < in->minPitchA() || pitch > in->maxPitchA()) {
                found[part].amateur.insert(bar);
            }
        }
    };

    for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        const int bar = m->no() + 1;
        for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            for (track_idx_t t = 0; t < score->ntracks(); ++t) {
                const EngravingItem* e = s->element(t);
                if (!e || !e->isChord()) {
                    continue;
                }
                const Chord* c = toChord(e);
                const Staff* staff = c->staff();
                if (!staff || staff->isDrumStaff(c->tick()) || !staff->part() || !staff->part()->show()) {
                    continue;
                }
                check(c, staff->part(), bar);
                for (const Chord* g : c->graceNotes()) {
                    check(g, staff->part(), bar);
                }
            }
        }
    }

    QStringList lines;
    for (const Part* part : score->parts()) {
        auto it = found.find(part);
        if (it == found.end()) {
            continue;
        }
        QStringList bits;
        if (!it->second.pro.empty()) {
            bits << QString("outside the pro range in bars %1").arg(starscoreBarList(it->second.pro));
        }
        if (!it->second.amateur.empty()) {
            bits << QString("outside the amateur range in bars %1").arg(starscoreBarList(it->second.amateur));
        }
        lines << QString("%1: %2.").arg(part->partName().toQString(), bits.join("; "));
    }

    if (lines.isEmpty()) {
        return QString("Every note of the visible instruments is inside their amateur ranges.");
    }
    return lines.join("\n");
}
}

bool mu::project::starscore::partLooksUnfinished(const mu::engraving::Score* score, const mu::engraving::Part* part)
{
    using namespace mu::engraving;
    if (!score || !part || part->staves().empty()) {
        return false;
    }
    const track_idx_t first = part->startTrack();
    const track_idx_t last = part->endTrack();
    int bars = 0;
    int withNotes = 0;
    for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        ++bars;
        bool found = false;
        for (const Segment* seg = m->first(SegmentType::ChordRest); seg && !found; seg = seg->next(SegmentType::ChordRest)) {
            for (track_idx_t t = first; t < last && !found; ++t) {
                const EngravingItem* e = seg->element(t);
                found = e && e->isChord();
            }
        }
        withNotes += found ? 1 : 0;
    }
    return bars > 0 && withNotes * 5 < bars;
}

int mu::project::starscore::syncBarNumbering(MasterScore* master, BarNumberingSync how)
{
    if (!master) {
        return 0;
    }
    int differing = 0;
    for (Excerpt* ex : master->excerpts()) {
        Score* es = ex ? ex->excerptScore() : nullptr;
        if (!es) {
            continue;
        }
        bool differs = false;
        for (Measure* m = master->firstMeasure(); m; m = m->nextMeasure()) {
            Measure* em = es->tick2measure(m->tick());
            if (!em || em->tick() != m->tick()) {
                continue;
            }
            if (em->irregular() != m->irregular()) {
                differs = true;
                if (how == BarNumberingSync::Undoable) {
                    em->undoChangeProperty(Pid::IRREGULAR, m->irregular());
                } else if (how == BarNumberingSync::Direct) {
                    em->setIrregular(m->irregular());
                }
            }
            if (em->noOffset() != m->noOffset()) {
                differs = true;
                if (how == BarNumberingSync::Undoable) {
                    em->undoChangeProperty(Pid::NO_OFFSET, m->noOffset());
                } else if (how == BarNumberingSync::Direct) {
                    em->setNoOffset(m->noOffset());
                }
            }
        }
        if (differs) {
            ++differing;
            if (how != BarNumberingSync::Check) {
                es->setLayoutAll();
            }
        }
    }
    return differing;
}

namespace {
bool starscoreIsKeyboardPart(const Part* p)
{
    if (!p || p->nstaves() < 2) {
        return false;
    }
    const QString id = p->instrumentId().toQString();
    return id == "electric-piano" || id.contains("organ") || id == "clavinet" || id.contains("piano")
           || id.contains("keyboard") || id.contains("synth") || id == "harpsichord" || id == "celesta";
}
}

int mu::project::starscore::applyKeysStaffRules(MasterScore* master, const std::vector<const Part*>& leadSheetParts)
{
    if (!master) {
        return 0;
    }
    int changed = 0;
    for (Part* p : master->parts()) {
        if (!starscoreIsKeyboardPart(p)) {
            continue;
        }
        // the Keys player's bass staff stays in the full score, empty or not (Joel, 13 Oct 2026); the lead sheet's
        // shows only where it has music, there too
        const bool keepInMain = std::find(leadSheetParts.begin(), leadSheetParts.end(), p) == leadSheetParts.end();
        for (size_t i = 1; i < p->nstaves(); ++i) {
            Staff* lower = p->staves().at(i);
            std::vector<Staff*> all = lower->staffList();
            if (std::find(all.begin(), all.end(), lower) == all.end()) {
                all.push_back(lower);
            }
            for (Staff* st : all) {
                const AutoOnOff want = keepInMain && st->score() == master ? AutoOnOff::OFF : AutoOnOff::ON;
                if (st->hideWhenEmpty() != want) {
                    st->setHideWhenEmpty(want);
                    ++changed;
                }
            }
        }
    }
    for (Excerpt* ex : master->excerpts()) {
        Score* es = ex ? ex->excerptScore() : nullptr;
        // (a part score of keyboards only: one keyboard, or a Keys part score holding several)
        bool keys = es && !es->parts().empty();
        for (const Part* ep : es ? es->parts() : std::vector<Part*>()) {
            keys = keys && (starscoreIsKeyboardPart(ep) || ep->nstaves() == 1);
        }
        if (!keys || std::none_of(es->parts().begin(), es->parts().end(), [](const Part* ep) { return starscoreIsKeyboardPart(ep); })) {
            continue;
        }
        if (!es->style().styleB(Sid::hideEmptyStaves)) {
            es->style().set(Sid::hideEmptyStaves, true);
            ++changed;
        }
        if (es->style().styleB(Sid::dontHideStavesInFirstSystem)) {
            es->style().set(Sid::dontHideStavesInFirstSystem, false);
            ++changed;
        }
    }
    if (changed) {
        for (Score* score : master->scoreList()) {
            score->setLayoutAll();
        }
    }
    return changed;
}

int mu::project::starscore::hideEmptyKeysStavesInCopy(MasterScore* master)
{
    int changed = 0;
    for (Part* p : master ? master->parts() : std::vector<Part*>()) {
        if (!starscoreIsKeyboardPart(p)) {
            continue;
        }
        for (size_t i = 1; i < p->nstaves(); ++i) {
            if (p->staves().at(i)->hideWhenEmpty() != AutoOnOff::ON) {
                p->staves().at(i)->setHideWhenEmpty(AutoOnOff::ON);
                ++changed;
            }
        }
    }
    if (changed) {
        master->setLayoutAll();
    }
    return changed;
}

int mu::project::starscore::copyEndBarlines(MasterScore* master, const Part* from, const std::vector<Part*>& to)
{
    if (!master || !from || from->staves().empty()) {
        return 0;
    }
    const track_idx_t srcTrack = from->staves().front()->idx() * VOICES;
    int changed = 0;
    for (Measure* m = master->firstMeasure(); m; m = m->nextMeasure()) {
        Segment* seg = m->findSegment(SegmentType::EndBarLine, m->endTick());
        if (!seg) {
            continue;
        }
        EngravingItem* se = seg->element(srcTrack);
        if (!se || !se->isBarLine()) {
            continue;
        }
        const BarLineType type = toBarLine(se)->barLineType();
        if (type == BarLineType::START_REPEAT || type == BarLineType::END_REPEAT || type == BarLineType::END_START_REPEAT) {
            continue;   // repeats belong to the bar, so every staff has them already
        }
        for (Part* p : to) {
            if (!p || p->staves().empty()) {
                continue;
            }
            EngravingItem* de = seg->element(p->staves().front()->idx() * VOICES);
            if (de && de->isBarLine() && toBarLine(de)->barLineType() != type) {
                master->undoChangeBarLineType(toBarLine(de), type, false);
                ++changed;
            }
        }
    }
    return changed;
}

int mu::project::starscore::syncEndBarlines(MasterScore* master, bool apply)
{
    if (!master) {
        return 0;
    }
    int count = 0;
    const size_t nstaves = master->nstaves();
    for (Measure* m = master->firstMeasure(); m; m = m->nextMeasure()) {
        Segment* seg = m->findSegment(SegmentType::EndBarLine, m->endTick());
        if (!seg) {
            continue;
        }
        // the special barline the bar has, if its staves agree on one
        bool found = false, conflict = false;
        BarLineType special = BarLineType::NORMAL;
        std::vector<BarLine*> plain;
        for (size_t st = 0; st < nstaves; ++st) {
            EngravingItem* e = seg->element(st * VOICES);
            if (!e || !e->isBarLine()) {
                continue;
            }
            BarLine* bl = toBarLine(e);
            const BarLineType t = bl->barLineType();
            if (t == BarLineType::START_REPEAT || t == BarLineType::END_REPEAT || t == BarLineType::END_START_REPEAT) {
                conflict = true;   // repeats belong to the bar; leave such bars as they are
                break;
            }
            if (t == BarLineType::NORMAL) {
                plain.push_back(bl);
            } else if (!found) {
                special = t;
                found = true;
            } else if (t != special) {
                conflict = true;
            }
        }
        if (!found || conflict || plain.empty()) {
            continue;
        }
        for (BarLine* bl : plain) {
            ++count;
            if (apply) {
                master->undoChangeBarLineType(bl, special, false);
            }
        }
    }

    // The part scores: each staff's barline like its staff in the score. A part can lose one the score has (the double
    // barline before a repeat went missing in Balkan Wedding's Bass Sax, Bari Sax and Contrabassoon parts).
    for (Excerpt* ex : master->excerpts()) {
        Score* es = ex ? ex->excerptScore() : nullptr;
        if (!es) {
            continue;
        }
        for (Measure* m = es->firstMeasure(); m; m = m->nextMeasure()) {
            Segment* seg = m->findSegment(SegmentType::EndBarLine, m->endTick());
            Measure* mm = master->tick2measure(m->tick());
            Segment* mseg = mm ? mm->findSegment(SegmentType::EndBarLine, mm->endTick()) : nullptr;
            if (!seg || !mseg) {
                continue;
            }
            for (size_t st = 0; st < es->nstaves(); ++st) {
                EngravingItem* e = seg->element(st * VOICES);
                Staff* staff = es->staff(st);
                Staff* linked = staff ? staff->findLinkedInScore(master) : nullptr;
                if (!e || !e->isBarLine() || !linked) {
                    continue;
                }
                EngravingItem* me = mseg->element(linked->idx() * VOICES);
                if (!me || !me->isBarLine()) {
                    continue;
                }
                const BarLineType want = toBarLine(me)->barLineType();
                BarLine* bl = toBarLine(e);
                if (want == BarLineType::NORMAL || want == BarLineType::START_REPEAT || want == BarLineType::END_REPEAT
                    || want == BarLineType::END_START_REPEAT || bl->barLineType() != BarLineType::NORMAL) {
                    continue;
                }
                ++count;
                if (apply) {
                    es->undoChangeBarLineType(bl, want, false);
                }
            }
        }
    }
    return count;
}

int mu::project::starscore::copyTextPositions(const Score* source, Score* target)
{
    if (!source || !target) {
        return 0;
    }
    // where a text sits, whether it shows, and how it looks: alignment, font, frame (Joel, 7 Oct 2026: a copied part
    // keeps the source part's text alignment too)
    static const std::vector<Pid> TEXT_PROPS { Pid::OFFSET, Pid::PLACEMENT, Pid::AUTOPLACE, Pid::VISIBLE, Pid::ALIGN,
                                               Pid::FONT_FACE, Pid::FONT_SIZE, Pid::FONT_STYLE, Pid::FRAME_TYPE };
    int moved = 0;
    // Changed on this one item only. undoChangeProperty would pass a text's font or alignment set in a part score on
    // to the main score and every other part (a system text is in all of them), and the next copy would undo it.
    auto copyProps = [&](const EngravingItem* from, EngravingItem* to, const std::vector<Pid>& props) {
        bool changed = false;
        for (Pid pid : props) {
            const PropertyValue v = from->getProperty(pid);
            if (v.isValid() && to->getProperty(pid) != v) {
                to->score()->undo(new ChangeProperty(to, pid, v, from->propertyFlags(pid)));
                if (!to->score()->isMaster()) {
                    to->unlinkPropertyFromMaster(pid);
                }
                changed = true;
            }
        }
        moved += changed ? 1 : 0;
    };

    // a player's annotations are that sheet's own: never matched to another sheet's texts
    std::set<const EngravingItem*> annotations;
    for (Score* sc : { const_cast<Score*>(source), target }) {
        for (const AnnotationItem& a : annotationItems(sc)) {
            annotations.insert(a.item);
        }
    }
    // texts attached to the music: staff and system text, tempo, rehearsal marks, expressions, dynamics, chord symbols
    auto movable = [&annotations](const EngravingItem* e) {
        return e && !annotations.count(e) && (e->isStaffText() || e->isSystemText() || e->isTempoText() || e->isRehearsalMark() || e->isExpression()
                     || e->isPlayTechAnnotation() || e->isDynamic() || e->isHarmony());
    };
    // the words that identify a text; a chord symbol by its kind (main or alternate) instead, since a transposing
    // instrument's chords read differently
    auto words = [](const EngravingItem* e) {
        return e->isHarmony() ? QString("#harmony %1").arg(int(toTextBase(e)->textStyleType()))
               : toTextBase(e)->plainText().toQString();
    };
    // the target's texts by place in the song and words
    std::multimap<std::pair<int, QString>, EngravingItem*> targetTexts;
    for (Segment* seg = target->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (EngravingItem* e : seg->annotations()) {
            if (movable(e)) {
                targetTexts.emplace(std::make_pair(seg->tick().ticks(), words(e)), e);
            }
        }
    }
    std::set<EngravingItem*> used;
    for (const Segment* seg = source->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (const EngravingItem* e : seg->annotations()) {
            if (!movable(e)) {
                continue;
            }
            const auto key = std::make_pair(seg->tick().ticks(), words(e));
            auto range = targetTexts.equal_range(key);
            for (auto it = range.first; it != range.second; ++it) {
                EngravingItem* t = it->second;
                if (used.count(t) || t->type() != e->type()) {
                    continue;
                }
                used.insert(t);
                // a chord symbol's font comes from its own chord style: only where it sits and whether it shows
                copyProps(e, t, e->isHarmony() ? std::vector<Pid> { Pid::OFFSET, Pid::PLACEMENT, Pid::AUTOPLACE, Pid::VISIBLE } : TEXT_PROPS);
                break;
            }
        }
    }

    // texts that belong to a bar: codas, segnos, D.S. al Coda, Fine…
    for (const Measure* m = source->firstMeasure(); m; m = m->nextMeasure()) {
        Measure* tm = target->tick2measure(m->tick());
        if (!tm || tm->tick() != m->tick()) {
            continue;
        }
        std::set<EngravingItem*> taken;
        for (const EngravingItem* e : m->el()) {
            if (!e || !(e->isMarker() || e->isJump())) {
                continue;
            }
            for (EngravingItem* t : tm->el()) {
                if (t && !taken.count(t) && t->type() == e->type()
                    && toTextBase(t)->plainText() == toTextBase(e)->plainText()) {
                    taken.insert(t);
                    copyProps(e, t, TEXT_PROPS);
                    break;
                }
            }
        }
    }

    // notes and rests hidden in the source part score only (1.18.51: the 5-Horn Trumpet 1 had the slash under a whole
    // note hidden in its part score; the 4-Horn 2-Trumpet's copy showed it). Matched by place in the song, voice, and
    // kind (a rest to a rest; a chord's notes to a chord of as many notes)
    const track_idx_t tracks = std::min(source->ntracks(), target->ntracks());
    for (const Segment* seg = source->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        Segment* tseg = nullptr;
        for (track_idx_t t = 0; t < tracks; ++t) {
            const EngravingItem* e = seg->element(t);
            if (!e) {
                continue;
            }
            if (!tseg) {
                tseg = target->tick2segment(seg->tick(), true, SegmentType::ChordRest);
                if (!tseg || tseg->tick() != seg->tick()) {
                    break;
                }
            }
            EngravingItem* te = tseg->element(t);
            if (!te || te->type() != e->type()) {
                continue;
            }
            if (e->isRest()) {
                copyProps(e, te, { Pid::VISIBLE });
            } else if (e->isChord() && toChord(e)->notes().size() == toChord(te)->notes().size()) {
                const std::vector<Note*>& from = toChord(e)->notes();
                const std::vector<Note*>& to = toChord(te)->notes();
                for (size_t i = 0; i < from.size(); ++i) {
                    copyProps(from[i], to[i], { Pid::VISIBLE });
                }
            }
        }
    }

    // frames (the title frame first): their height and gaps, and where each of their texts sits, matched by kind
    // (title, subtitle, composer, instrument name…), never the words
    std::vector<const Box*> sourceBoxes;
    std::vector<Box*> targetBoxes;
    for (const MeasureBase* mb = source->first(); mb; mb = mb->next()) {
        if (mb->isVBox()) {
            sourceBoxes.push_back(toBox(mb));
        }
    }
    for (MeasureBase* mb = target->first(); mb; mb = mb->next()) {
        if (mb->isVBox()) {
            targetBoxes.push_back(toBox(mb));
        }
    }
    for (size_t i = 0; i < sourceBoxes.size() && i < targetBoxes.size(); ++i) {
        copyProps(sourceBoxes[i], targetBoxes[i], { Pid::BOX_HEIGHT, Pid::TOP_GAP, Pid::BOTTOM_GAP });
        std::set<EngravingItem*> taken;
        for (const EngravingItem* e : sourceBoxes[i]->el()) {
            if (!e || !e->isTextBase()) {
                continue;
            }
            for (EngravingItem* t : targetBoxes[i]->el()) {
                if (t && t->isTextBase() && !taken.count(t) && toTextBase(t)->textStyleType() == toTextBase(e)->textStyleType()) {
                    taken.insert(t);
                    copyProps(e, t, { Pid::OFFSET, Pid::ALIGN });
                    break;
                }
            }
        }
    }

    // lines (hairpins, 8va, text lines, voltas…): placement, and each piece's offsets when both are laid out in as
    // many pieces (the breaks are the same after copyLayout)
    target->doLayout();
    std::multimap<std::tuple<int, int, int, int>, Spanner*> targetLines;
    for (const auto& [tick, sp] : target->spanner()) {
        if (sp && !sp->isSlur() && !sp->isTie()) {
            targetLines.emplace(std::make_tuple(int(sp->type()), sp->tick().ticks(), sp->tick2().ticks(), int(sp->track())), sp);
        }
    }
    std::set<Spanner*> usedLines;
    for (const auto& [tick, sp] : source->spanner()) {
        if (!sp || sp->isSlur() || sp->isTie()) {
            continue;
        }
        auto range = targetLines.equal_range(std::make_tuple(int(sp->type()), sp->tick().ticks(), sp->tick2().ticks(), int(sp->track())));
        for (auto it = range.first; it != range.second; ++it) {
            Spanner* t = it->second;
            if (usedLines.count(t)) {
                continue;
            }
            usedLines.insert(t);
            copyProps(sp, t, { Pid::PLACEMENT, Pid::AUTOPLACE, Pid::VISIBLE });
            if (sp->spannerSegments().size() == t->spannerSegments().size()) {
                for (size_t k = 0; k < sp->spannerSegments().size(); ++k) {
                    copyProps(sp->spannerSegments()[k], t->spannerSegments()[k], { Pid::OFFSET, Pid::OFFSET2, Pid::AUTOPLACE });
                }
            }
            break;
        }
    }
    return moved;
}

//! A version sheet for a brass instrument (the only ones that use a mute)
bool mu::project::starscore::isBrassSheet(const QString& sheetName)
{
    static const QRegularExpression brass("\\b(trumpet|trombone|flugelhorn|cornet|horn in f|french horn|tuba|euphonium|baritone horn)\\b",
                                          QRegularExpression::CaseInsensitiveOption);
    return brass.match(sheetName).hasMatch();
}

QStringList mu::project::starscore::muteOptions(const QString& id)
{
    if (id.contains("trumpet") || id.contains("cornet") || id.contains("flugelhorn")) {
        return { "Straight Mute", "Cup Mute", "Harmon Mute", "Plunger", "Bucket Mute",
                 "Solotone Mute", "Pixie Mute", "Derby (Hat)", "Practice Mute" };
    }
    if (id.contains("trombone") || id.contains("euphonium") || id.contains("baritone-horn")) {
        return { "Straight Mute", "Cup Mute", "Harmon Mute", "Plunger", "Pixie Mute",
                 "Bucket Mute", "Solotone Mute", "Derby (Hat)", "Practice Mute" };
    }
    if (id.contains("tuba") || id.contains("sousaphone")) {
        return { "Straight Mute", "Cup Mute", "Practice Mute" };
    }
    if (id == "horn" || id.contains("french-horn") || id.startsWith("horn-")) {
        return { "Straight Mute", "Stopping Mute", "Practice Mute" };
    }
    return {};
}

void mu::project::starscore::tempoBeatFor(int numerator, int denominator, int& noteValue, bool& dotted)
{
    dotted = numerator > 3 && numerator % 3 == 0 && denominator >= 2;
    noteValue = dotted ? denominator / 2 : denominator;
    noteValue = std::clamp(noteValue, 1, 16);
    if (dotted && noteValue == 1) {
        dotted = false;   // (no dotted whole note to count in: 6/2 counts half notes)
        noteValue = 2;
    }
}

QString mu::project::starscore::muteLines(const QStringList& mutes)
{
    QStringList lines;
    for (int i = 0; i < mutes.size(); ++i) {
        QString line = mutes[i];
        if (i == 0) {
            line = "with " + line;
        } else if (i == mutes.size() - 1) {
            line = "and " + line;
        }
        if (mutes.size() > 2 && i < mutes.size() - 1) {
            line += ",";
        }
        lines << line;
    }
    return lines.join("\n");
}

bool mu::project::starscore::isMuteMarking(const QString& text)
{
    // (Joel, 14 Oct 2026: every mute text, "harmon" and "plunger" on their own too) A text made only of mute words, with
    // at least one that names a mute or the change: "mute", "(open)", "Harmon (stem out)", "w/ plunger", "to cup",
    // "plunger in hat", "con sord.", "+ o" (plunger closed and open). "Open solos" and "straight 8ths" are not.
    static const QSet<QString> core { "mute", "muted", "mutes", "unmute", "unmuted", "open", "harmon", "plunger", "cup",
                                      "bucket", "solotone", "pixie", "derby", "hat", "stopping", "stopped", "wah", "wah-wah",
                                      "sord", "sordino", "sordini" };
    static const QSet<QString> filler { "straight", "practice", "in", "out", "on", "off", "with", "w", "no", "to", "and",
                                        "stem", "the", "a", "con", "senza", "half", "closed", "+", "o" };
    static const QRegularExpression token("[a-z]+(?:-[a-z]+)?|\\+");
    const QString lower = text.simplified().toLower();
    QStringList words;
    for (auto it = token.globalMatch(lower); it.hasNext();) {
        words << it.next().captured(0);
    }
    if (words.isEmpty()) {
        return false;
    }
    // what is left besides the words: brackets, dots, commas, slashes, dashes, arrows
    QString rest = lower;
    rest.remove(token);
    static const QRegularExpression punct("[\\s()\\[\\].,:;!/\\-–—→>]");
    rest.remove(punct);
    if (!rest.isEmpty()) {
        return false;
    }
    bool plungerSigns = true;
    bool any = false;
    for (const QString& w : words) {
        if (!core.contains(w) && !filler.contains(w)) {
            return false;
        }
        any |= core.contains(w);
        plungerSigns &= w == "+" || w == "o";
    }
    return any || plungerSigns;
}

bool mu::project::starscore::isWoodwindInstrument(const QString& id)
{
    // (no mute: saxophones, clarinets, flutes, double reeds; an English horn is a woodwind whatever its name)
    static const QRegularExpression ww("sax|clarinet|flute|piccolo|fife|oboe|english-horn|bassoon|recorder|ocarina|whistle");
    return ww.match(id).hasMatch();
}

//! Hides the mute and open markings in a sheet for an instrument that has no mute: every text that is only such a
//! marking (isMuteMarking), and the brass mute symbols on notes (plunger +/o, Harmon stem). Undoable (inside a
//! command). Returns how many were hidden; `hidden` gets them, for showing them again.
int mu::project::starscore::hideMuteMarkings(Score* score, std::vector<mu::engraving::EngravingItem*>* hidden)
{
    if (!score) {
        return 0;
    }
    std::vector<mu::engraving::EngravingItem*> hide;
    for (mu::engraving::Segment* seg = score->firstSegment(mu::engraving::SegmentType::ChordRest); seg;
         seg = seg->next1(mu::engraving::SegmentType::ChordRest)) {
        for (mu::engraving::EngravingItem* e : seg->annotations()) {
            if (!e || !e->visible() || !(e->isStaffText() || e->isSystemText() || e->isExpression() || e->isPlayTechAnnotation())) {
                continue;
            }
            if (isMuteMarking(mu::engraving::toTextBase(e)->plainText().toQString())) {
                hide.push_back(e);
            }
        }
        for (mu::engraving::EngravingItem* e : seg->elist()) {
            if (!e || !e->isChord()) {
                continue;
            }
            for (mu::engraving::Articulation* a : mu::engraving::toChord(e)->articulations()) {
                const std::string name = mu::engraving::SymNames::nameForSymId(a->symId()).ascii();
                if (a->visible() && (name.rfind("brassMute", 0) == 0 || name.rfind("brassHarmonMute", 0) == 0)) {
                    hide.push_back(a);
                }
            }
        }
    }
    for (mu::engraving::EngravingItem* e : hide) {
        e->undoChangeProperty(mu::engraving::Pid::VISIBLE, false);
    }
    if (hidden) {
        *hidden = hide;
    }
    return int(hide.size());
}

bool mu::project::starscore::isRhythmMarking(const mu::engraving::Note* n)
{
    if (!n || n->noteType() != mu::engraving::NoteType::NORMAL || n->play() || !n->chord()) {
        return false;
    }
    const bool small = n->isSmall() || n->chord()->isSmall();
    return small && n->headGroup() != mu::engraving::NoteHeadGroup::HEAD_NORMAL;
}

namespace {
//! The space above the staff (-1), or below it (the bottom line + 1)
int rhythmMarkingLine(const mu::engraving::Note* n, bool below)
{
    const mu::engraving::StaffType* st = n->staffType();
    return below && st ? st->bottomLine() + 1 : -1;
}
}

int mu::project::starscore::pinRhythmMarkings(Score* score, bool apply)
{
    if (!score) {
        return 0;
    }
    int count = 0;
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (EngravingItem* e : s->elist()) {
            if (!e || !e->isChord()) {
                continue;
            }
            for (Note* n : toChord(e)->notes()) {
                if (n->fixed() || !isRhythmMarking(n)) {
                    continue;
                }
                ++count;
                if (apply) {
                    // the side it shows on now: above the middle line, the space above; else the space below
                    const mu::engraving::StaffType* st = n->staffType();
                    const bool below = st && n->line() > st->middleLine();
                    n->undoChangeProperty(Pid::FIXED, true);
                    n->undoChangeProperty(Pid::FIXED_LINE, rhythmMarkingLine(n, below));
                }
            }
        }
    }
    return count;
}

int mu::project::starscore::clearRhythmRestOffsets(MasterScore* master, bool apply)
{
    if (!master) {
        return 0;
    }
    std::vector<Score*> scores { master };
    for (Excerpt* ex : master->excerpts()) {
        if (ex && ex->excerptScore()) {
            scores.push_back(ex->excerptScore());
        }
    }
    int count = 0;
    for (Score* score : scores) {
        for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
            std::set<track_idx_t> markingTracks;
            for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
                for (EngravingItem* e : s->elist()) {
                    if (!e || !e->isChord()) {
                        continue;
                    }
                    const Chord* c = toChord(e);
                    bool all = !c->notes().empty();
                    for (const Note* n : c->notes()) {
                        all = all && n->fixed() && isRhythmMarking(n);
                    }
                    if (all) {
                        markingTracks.insert(c->track());
                    }
                }
            }
            if (markingTracks.empty()) {
                continue;
            }
            for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
                for (EngravingItem* e : s->elist()) {
                    if (e && e->isRest() && markingTracks.count(e->track()) && !RealIsNull(e->offset().y())) {
                        ++count;
                        if (apply) {
                            e->undoChangeProperty(Pid::OFFSET, PointF(e->offset().x(), 0.0));
                        }
                    }
                }
            }
        }
    }
    return count;
}

int mu::project::starscore::makeRhythmMarkings(Score* score, bool below)
{
    if (!score) {
        return 0;
    }
    std::set<Chord*> chords;
    for (EngravingItem* e : score->selection().elements()) {
        if (e && e->isNote()) {
            chords.insert(toNote(e)->chord());
        } else if (e && e->isChord()) {
            chords.insert(toChord(e));
        }
    }
    // (in a part score too: the full score and the other part scores take the same, as the part's own size, playback
    // and stem direction otherwise stay in that part only)
    std::set<Chord*> linked;
    for (Chord* c : chords) {
        if (!c || c->isGrace()) {
            continue;
        }
        for (EngravingObject* o : c->linkList()) {
            if (o && o->isChord()) {
                linked.insert(toChord(o));
            }
        }
    }
    int count = 0;
    for (Chord* c : linked) {
        const bool counted = chords.count(c) > 0;
        c->undoChangeProperty(Pid::SMALL, true);
        c->undoChangeProperty(Pid::STEM_DIRECTION, PropertyValue::fromValue<DirectionV>(below ? DirectionV::DOWN : DirectionV::UP));
        for (Note* n : c->notes()) {
            if (n->headGroup() == NoteHeadGroup::HEAD_NORMAL) {
                n->undoChangeProperty(Pid::HEAD_GROUP, NoteHeadGroup::HEAD_SLASH);
            }
            n->undoChangeProperty(Pid::PLAY, false);
            n->undoChangeProperty(Pid::FIXED, true);
            n->undoChangeProperty(Pid::FIXED_LINE, rhythmMarkingLine(n, below));
            count += counted ? 1 : 0;
        }
    }
    return count;
}

bool mu::project::starscore::partHasNotes(const Part* part)
{
    if (!part || !part->score()) {
        return false;
    }
    const Score* score = part->score();
    for (const Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (track_idx_t t = part->startTrack(); t < part->endTrack(); ++t) {
            const EngravingItem* e = s->element(t);
            if (e && e->isChord()) {
                return true;
            }
        }
    }
    return false;
}

bool mu::project::starscore::pitchesWithin(const Part* part, int lowest, int highest)
{
    if (!part || !part->score()) {
        return true;
    }
    const Score* score = part->score();
    for (const Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (track_idx_t t = part->startTrack(); t < part->endTrack(); ++t) {
            const EngravingItem* e = s->element(t);
            if (!e || !e->isChord()) {
                continue;
            }
            const Chord* c = toChord(e);
            std::vector<const Note*> notes(c->notes().begin(), c->notes().end());
            for (const Chord* g : c->graceNotes()) {
                notes.insert(notes.end(), g->notes().begin(), g->notes().end());
            }
            for (const Note* n : notes) {
                if (n->pitch() < lowest || n->pitch() > highest) {
                    return false;
                }
            }
        }
    }
    return true;
}

int mu::project::starscore::setFirstClefs(Part* part, int concert, int transposing)
{
    if (!part || part->staves().empty()) {
        return 0;
    }
    int changed = 0;
    for (Staff* st : part->staves().front()->staffList()) {
        Measure* first = st->score()->firstMeasure();
        // the first bar, and the multimeasure rest that stands for it when the song starts with rests (it has
        // its own copy of the clef)
        for (Measure* m : { first, first ? first->mmRest() : nullptr }) {
            Segment* seg = m ? m->findFirstR(SegmentType::HeaderClef, Fraction(0, 1)) : nullptr;
            Clef* clef = seg ? toClef(seg->element(st->idx() * VOICES)) : nullptr;
            if (!clef) {
                continue;
            }
            if (clef->generated()) {
                clef->setClefType(ClefTypeList(ClefType(concert), ClefType(transposing)));   // (layout keeps it in step)
                continue;
            }
            if (int(clef->clefTypeList().concertClef) != concert) {
                clef->undoChangeProperty(Pid::CLEF_TYPE_CONCERT, PropertyValue(ClefType(concert)));
                ++changed;
            }
            if (int(clef->clefTypeList().transposingClef) != transposing) {
                clef->undoChangeProperty(Pid::CLEF_TYPE_TRANSPOSING, PropertyValue(ClefType(transposing)));
                ++changed;
            }
        }
    }
    return changed;
}

int mu::project::starscore::copyMeasureWidths(const Score* source, Score* target)
{
    if (!source || !target) {
        return 0;
    }
    std::map<int, const Measure*> byTick;
    for (const Measure* m = source->firstMeasure(); m; m = m->nextMeasure()) {
        byTick[m->tick().ticks()] = m;
    }
    int changed = 0;
    for (Measure* m = target->firstMeasure(); m; m = m->nextMeasure()) {
        auto it = byTick.find(m->tick().ticks());
        if (it == byTick.end()) {
            continue;
        }
        const PropertyValue want = it->second->getProperty(Pid::USER_STRETCH);
        if (m->getProperty(Pid::USER_STRETCH) != want) {
            m->undoChangeProperty(Pid::USER_STRETCH, want);
            ++changed;
        }
    }
    if (changed) {
        target->setLayoutAll();
    }
    return changed;
}

int mu::project::starscore::tidyTempoAndFrames(MasterScore* master, bool apply)
{
    if (!master) {
        return 0;
    }
    static const QRegularExpression fontFace("<font\\s+face=\"[^\"]*\"\\s*/>");
    int count = 0;
    for (Score* score : master->scoreList()) {
        // title frame: fixed height
        for (MeasureBase* mb = score->first(); mb && !mb->isMeasure(); mb = mb->next()) {
            if (mb->isVBox()) {
                if (mb->getProperty(Pid::BOX_AUTOSIZE).toBool()) {
                    ++count;
                    if (apply) {
                        mb->undoChangeProperty(Pid::BOX_AUTOSIZE, false);
                    }
                }
                break;
            }
        }
        // tempo marks: the text style's font throughout
        for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
            for (EngravingItem* e : seg->annotations()) {
                if (!e || !e->isTempoText()) {
                    continue;
                }
                TextBase* t = toTextBase(e);
                const QString xml = t->xmlText().toQString();
                QString clean = xml;
                clean.remove(fontFace);
                if (clean != xml) {
                    ++count;
                    if (apply) {
                        t->undoChangeProperty(Pid::TEXT, String::fromQString(clean));
                    }
                }
            }
        }
    }
    return count;
}

int mu::project::starscore::lockSheetLayout(Score* score)
{
    if (!score) {
        return 0;
    }
    score->doLayout();
    int added = 0;
    std::vector<std::pair<MeasureBase*, MeasureBase*> > locks;   // collected first: adding one changes the layout
    std::vector<MeasureBase*> pageEnds;
    const std::vector<Page*>& pages = score->pages();
    for (size_t pi = 0; pi < pages.size(); ++pi) {
        MeasureBase* lastOnPage = nullptr;
        for (System* sys : pages[pi]->systems()) {
            MeasureBase* first = nullptr;
            MeasureBase* last = nullptr;
            for (MeasureBase* mb : sys->measures()) {
                if (mb && mb->isMeasure()) {
                    if (!first) {
                        first = mb;
                    }
                    last = mb;
                }
            }
            if (!first) {
                continue;   // a frame
            }
            if (!score->systemLocks()->lockContaining(first) && !score->systemLocks()->lockContaining(last)) {
                locks.emplace_back(first, last);
            }
            lastOnPage = last;
        }
        if (lastOnPage && pi + 1 < pages.size()) {
            MeasureBase* end = lastOnPage->isMeasure() && toMeasure(lastOnPage)->isMMRest()
                               ? static_cast<MeasureBase*>(toMeasure(lastOnPage)->mmRestLast()) : lastOnPage;
            if (end && !end->pageBreak() && !end->sectionBreak()) {
                pageEnds.push_back(end);
            }
        }
    }
    for (const auto& [a, b] : locks) {
        EditSystemLocks::undoAddSystemLock(score, new SystemLock(a, b));
        ++added;
    }
    for (MeasureBase* end : pageEnds) {
        end->undoSetPageBreak(true);
        ++added;
    }
    return added;
}
