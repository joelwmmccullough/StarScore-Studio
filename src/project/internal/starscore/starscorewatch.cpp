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
std::vector<WatchNote> watchNotes(const mu::notation::INotationPtr& notation, QString* name, std::vector<WatchBar>* bars)
{
    if (bars) {
        bars->clear();
    }
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
            if (bars) {
                bars->push_back({ ms->utick2utime(m->tick().ticks() + offset), m->no() + 1 });
            }
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

void WaitFollower::setNotes(const std::vector<WatchNote>& notes, int hands)
{
    m_hands = hands;
    m_groups.clear();
    m_end = 0;
    for (const WatchNote& n : notes) {   // (by start)
        m_end = std::max(m_end, n.end);
        if (!waitsFor(n)) {
            continue;
        }
        if (m_groups.empty() || n.start - m_groups.back().start > CHORD) {
            m_groups.push_back({ n.start, {} });
        }
        std::vector<int>& p = m_groups.back().pitches;
        if (std::find(p.begin(), p.end(), n.pitch) == p.end()) {
            p.push_back(n.pitch);
        }
    }
    seek(m_clock);
}

void WaitFollower::start(double from)
{
    seek(from);
    m_right = 0;
    m_wrongCount = 0;
}

void WaitFollower::seek(double from)
{
    m_clock = from;
    m_holding = false;
    m_next = std::lower_bound(m_groups.begin(), m_groups.end(), from - 0.005,
                              [](const Group& g, double v) { return g.start < v; }) - m_groups.begin();
    m_hit.fill(false);
    m_wrong.fill(false);
    advance(0);
}

void WaitFollower::advance(double secs)
{
    if (m_holding) {
        return;
    }
    m_clock += secs;
    if (!finished() && m_clock >= m_groups[m_next].start) {
        m_clock = m_groups[m_next].start;
        m_holding = true;
        checkDone();   // (played early)
    }
}

std::vector<int> WaitFollower::waitingFor() const
{
    return finished() ? std::vector<int>() : m_groups[m_next].pitches;
}

bool WaitFollower::expected(int pitch) const
{
    if (finished()) {
        return false;
    }
    const std::vector<int>& p = m_groups[m_next].pitches;
    return std::find(p.begin(), p.end(), pitch) != p.end();
}

bool WaitFollower::noteOn(int pitch)
{
    if (pitch < 0 || pitch >= 128) {
        return false;
    }
    m_held[pitch] = true;
    const bool inTime = !finished() && m_groups[m_next].start - m_clock <= EARLY;
    if (inTime && expected(pitch)) {
        m_hit[pitch] = true;
        m_wrong[pitch] = false;
        checkDone();
        return true;
    }
    m_wrong[pitch] = true;
    ++m_wrongCount;
    return false;
}

void WaitFollower::noteOff(int pitch)
{
    if (pitch >= 0 && pitch < 128) {
        m_held[pitch] = false;
        m_wrong[pitch] = false;
    }
}

void WaitFollower::checkDone()
{
    if (finished()) {
        return;
    }
    for (int p : m_groups[m_next].pitches) {
        if (!m_hit[p]) {
            return;
        }
    }
    m_right += int(m_groups[m_next].pitches.size());
    for (int p : m_groups[m_next].pitches) {
        m_hit[p] = false;
    }
    ++m_next;
    m_holding = false;
}
}
