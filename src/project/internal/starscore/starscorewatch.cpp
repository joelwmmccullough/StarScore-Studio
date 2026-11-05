/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Watch: the notes for the falling notes (see starscorewatch.h)
 */
#include "starscorewatch.h"

#include <algorithm>

#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "notation/inotationelements.h"

using namespace mu::engraving;

namespace mu::project::starscore {
std::vector<WatchNote> watchNotes(const mu::notation::INotationPtr& notation, QString* name)
{
    std::vector<WatchNote> out;
    if (!notation || !notation->elements()) {
        return out;
    }
    Score* score = notation->elements()->msScore();
    if (!score) {
        return out;
    }
    MasterScore* ms = score->masterScore();
    if (name) {
        *name = score->isMaster() ? QString() : score->name().toQString();
    }
    const RepeatList& repeats = ms->repeatList();
    for (const RepeatSegment* rs : repeats) {
        const int offset = rs->utick - rs->tick;
        const int segEnd = rs->tick + rs->len();
        for (Measure* m = score->tick2measure(Fraction::fromTicks(rs->tick)); m && m->tick().ticks() < segEnd; m = m->nextMeasure()) {
            for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
                for (track_idx_t t = 0; t < score->ntracks(); ++t) {
                    EngravingItem* e = s->element(t);
                    if (!e || !e->isChord()) {
                        continue;
                    }
                    Chord* chord = toChord(e);
                    Staff* staff = chord->staff();
                    if (!staff || !staff->show() || staff->isDrumStaff(chord->tick()) || !staff->part() || !staff->part()->show()) {
                        continue;
                    }
                    const int hand = int(std::min<staff_idx_t>(chord->vStaffIdx() - staff->part()->startTrack() / VOICES, 2));
                    auto add = [&](Chord* c, int startTick) {
                        for (Note* n : c->notes()) {
                            if (n->tieBack() || !n->play()) {
                                continue;   // (a tied note sounds from the first of its chain)
                            }
                            const int from = startTick + offset;
                            const int to = from + std::max(1, n->playTicks());
                            out.push_back({ ms->utick2utime(from), ms->utick2utime(to), n->ppitch(), hand });
                        }
                    };
                    // grace notes before the beat, briefly
                    for (Chord* g : chord->graceNotesBefore()) {
                        add(g, std::max(0, chord->tick().ticks() - g->actualTicks().ticks()));
                    }
                    add(chord, chord->tick().ticks());
                }
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const WatchNote& a, const WatchNote& b) { return a.start < b.start; });
    return out;
}
}
