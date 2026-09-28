/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "starscoreengraving.h"

#include <map>
#include <set>

#include <QStringList>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/accidental.h"
#include "engraving/dom/notedot.h"
#include "engraving/dom/select.h"
#include "engraving/dom/timesig.h"
#include "engraving/dom/factory.h"
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

        for (Measure* m = target->firstMeasure(); m; m = m->nextMeasure()) {
            auto it = sourceByTick.find(m->tick().ticks());
            if (it == sourceByTick.end()) {
                continue;
            }
            const Measure* sm = it->second;

            if (options.pageBreaks) {
                applyBreak(m, sm->pageBreak(), m->pageBreak(), LayoutBreakType::PAGE);
            }
            if (options.lineBreaks) {
                applyBreak(m, sm->lineBreak(), m->lineBreak(), LayoutBreakType::LINE);
            }
            if (options.keepTogether) {
                applyBreak(m, sm->noBreak(), m->noBreak(), LayoutBreakType::NOBREAK);
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

    // Where the pattern stops
    const Fraction startTick = start->tick();
    Fraction endTick;
    Fraction resumeSig;   // time signature to restore after a selected range
    if (last) {
        endTick = last->endTick();
        resumeSig = last->nextMeasure() ? last->nextMeasure()->timesig() : Fraction();
        if (last->nextMeasure() && starscoreTimeSigAt(last->nextMeasure())) {
            resumeSig = Fraction();   // a time signature already follows
        }
    } else {
        endTick = score->lastMeasure()->endTick();
        for (Measure* m = start->nextMeasure(); m; m = m->nextMeasure()) {
            if (starscoreTimeSigAt(m)) {
                endTick = m->tick();
                break;
            }
        }
    }

    String numeratorText;
    for (size_t i = 0; i < numerators.size(); ++i) {
        if (i > 0) {
            numeratorText += u"+";
        }
        numeratorText += String::number(numerators[i]);
    }

    Fraction tick = startTick;
    size_t i = 0;
    int guard = 0;
    while (tick < endTick && guard++ < 10000) {
        Measure* m = score->tick2measure(tick);
        if (!m || m->tick() != tick) {
            break;
        }
        const Fraction sig(numerators[i % numerators.size()], denominator);

        TimeSig* ts = Factory::createTimeSig(score->dummy()->segment());
        ts->setSig(sig);
        if (i == 0) {
            if (numerators.size() > 1) {
                // One time signature: "4+4+4+3" over a single visible denominator. When only the
                // numerator string is set, MuseScore draws no denominator, so both are set.
                ts->setNumeratorString(numeratorText);
                ts->setDenominatorString(String::number(denominator));
            }
        } else {
            ts->setVisible(false);
            ts->setShowCourtesySig(false);
        }
        score->cmdAddTimeSig(m, 0, ts, false);

        tick += sig;
        ++i;
    }

    if (resumeSig.isValid() && !resumeSig.isZero()) {
        if (Measure* m = score->tick2measure(tick); m && m->tick() == tick) {
            TimeSig* ts = Factory::createTimeSig(score->dummy()->segment());
            ts->setSig(resumeSig);
            score->cmdAddTimeSig(m, 0, ts, false);
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
