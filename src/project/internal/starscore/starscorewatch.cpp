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
#include "engraving/playback/playbackeventsrenderer.h"
#include "engraving/playback/playbackcontext.h"
#include "engraving/playback/playbacksetupdataresolver.h"
#include "engraving/playback/utils/pitchutils.h"
#include "mpe/iarticulationprofilesrepository.h"
#include "modularity/ioc.h"

#include <cmath>
#include <map>

using namespace mu::engraving;

namespace mu::project::starscore {
namespace {
int midiOf(muse::mpe::pitch_level_t level)
{
    return int(std::lround(muse::mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT + level / double(muse::mpe::PITCH_LEVEL_STEP)));
}

int expectedMidi(const Note* n)
{
    return midiOf(notePitchLevel(n->playingTpc(), n->playingOctave()));
}

//! (1.29.0) A chord's notes as playback plays them: the chord through playback's own renderer (trills, mordents and
//! turns as their notes, tremolos repeated, staccato and the like shortened, arpeggios rolled, grace notes, glissandos).
//! false: nothing could be rendered (the old way is used then)
bool renderedNotes(const Chord* chord, int offset, const muse::mpe::ArticulationsProfilePtr& profile,
                   const PlaybackContextPtr& ctx, int hand, std::vector<WatchNote>& out)
{
    static const PlaybackEventsRenderer renderer;
    muse::mpe::PlaybackEventsMap result;
    renderer.render(chord, offset, profile, ctx, result);
    struct Ev {
        double start, end;
        int pitch;
    };
    std::vector<Ev> evs;
    for (const auto& [ts, list] : result) {
        for (const muse::mpe::PlaybackEvent& e : list) {
            if (const auto* ne = std::get_if<muse::mpe::NoteEvent>(&e)) {
                const auto& a = ne->arrangementCtx();
                if (a.actualDuration <= 0) {
                    continue;
                }
                evs.push_back({ a.actualTimestamp / 1e6, (a.actualTimestamp + a.actualDuration) / 1e6, midiOf(ne->pitchCtx().nominalPitchLevel) });
            }
        }
    }
    if (evs.empty()) {
        return false;
    }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.start < b.start; });
    // the pitch as it plays here (a solo's key: its playback transposition) from playback's pitch
    const int shift = chord->notes().empty() ? 0 : chord->notes().front()->ppitch() - expectedMidi(chord->notes().front());
    const double chordStart = chord->score()->repeatList().utick2utime(chord->tick().ticks() + offset) - 0.001;
    std::vector<bool> used(evs.size(), false);
    std::vector<WatchNote> mine(evs.size());
    for (size_t i = 0; i < evs.size(); ++i) {
        mine[i] = { evs[i].start, evs[i].end, evs[i].pitch + shift, hand, chord->tick().ticks(), int(chord->track()), -1, true };
    }
    // the written notes: each note's first sound at or after the beat; then each grace note's first sound
    for (size_t ni = 0; ni < chord->notes().size(); ++ni) {
        const int want = expectedMidi(chord->notes()[ni]);
        for (size_t i = 0; i < evs.size(); ++i) {
            if (!used[i] && evs[i].pitch == want && evs[i].start >= chordStart) {
                used[i] = true;
                mine[i].noteIdx = int(ni);
                mine[i].extra = false;
                break;
            }
        }
    }
    for (const Chord* g : chord->graceNotes()) {
        for (const Note* gn : g->notes()) {
            const int want = expectedMidi(gn);
            for (size_t i = 0; i < evs.size(); ++i) {
                if (!used[i] && evs[i].pitch == want) {
                    used[i] = true;
                    mine[i].extra = false;   // (noteIdx stays -1: a grace note)
                    break;
                }
            }
        }
    }
    out.insert(out.end(), mine.begin(), mine.end());
    return true;
}
}

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
    // (1.29.0) playback's articulation profiles and context (dynamics, playing techniques), per part
    auto profiles = muse::modularity::globalIoc()->resolve<muse::mpe::IArticulationProfilesRepository>("starscore");
    std::map<const Part*, PlaybackContextPtr> contexts;
    std::map<int, muse::mpe::ArticulationsProfilePtr> profileFor;
    const PlaybackSetupDataResolver setupResolver;
    auto profileOf = [&](const Chord* chord) -> muse::mpe::ArticulationsProfilePtr {
        if (!profiles) {
            return nullptr;
        }
        muse::mpe::PlaybackSetupData setup;
        setupResolver.resolveSetupData(chord->part()->instrument(chord->tick()), setup);
        auto it = profileFor.find(int(setup.category));
        if (it == profileFor.end()) {
            it = profileFor.emplace(int(setup.category), profiles->defaultProfile(setup.category)).first;
        }
        return it->second;
    };
    auto contextOf = [&](const Part* part) {
        auto it = contexts.find(part);
        if (it == contexts.end()) {
            auto ctx = std::make_shared<PlaybackContext>();
            ctx->update(part->id(), score);
            it = contexts.emplace(part, ctx).first;
        }
        return it->second;
    };

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
                    // the hand: the staff the chord is drawn on (a chord moved to the other staff, as in cross-staff
                    // beaming, is the other hand's)
                    const int hand = int(std::min<staff_idx_t>(chord->vStaffIdx() - staff->part()->startTrack() / VOICES, 2));
                    if (const auto profile = profileOf(chord)) {
                        if (renderedNotes(chord, offset, profile, contextOf(staff->part()), hand, out)) {
                            continue;
                        }
                    }
                    const int chordTick = chord->tick().ticks();
                    const int chordTrack = int(chord->track());
                    auto add = [&](Chord* c, int startTick) {
                        for (size_t ni = 0; ni < c->notes().size(); ++ni) {
                            Note* n = c->notes()[ni];
                            if (n->tieBack() || !n->play()) {
                                continue;   // (a tied note sounds from the first of its chain)
                            }
                            const int from = startTick + offset;
                            const int to = from + std::max(1, n->playTicks());
                            out.push_back({ ms->utick2utime(from), ms->utick2utime(to), n->ppitch(), hand, chordTick, chordTrack,
                                            c == chord ? int(ni) : -1 });
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
    std::stable_sort(out.begin(), out.end(), [](const WatchNote& a, const WatchNote& b) { return a.start < b.start; });
    return out;
}

void WaitFollower::setNotes(const std::vector<WatchNote>& notes, int hands)
{
    m_hands = hands;
    m_groups.clear();
    m_others.clear();
    m_extras.clear();
    m_end = 0;
    for (size_t i = 0; i < notes.size(); ++i) {   // (by start)
        const WatchNote& n = notes[i];
        m_end = std::max(m_end, n.end);
        if (n.extra) {
            // (1.29.0) a trill's or tremolo's other sounds: not waited for, and not wrong when played
            if (waitsFor(n)) {
                m_extras.push_back(n);
            }
            continue;
        }
        if (!waitsFor(n)) {
            m_others.push_back({ n.start, i });
            continue;
        }
        if (m_groups.empty() || n.start - m_groups.back().start > CHORD) {
            m_groups.push_back({ n.start, {}, n.tick, n.track });
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
    m_nextOther = std::lower_bound(m_others.begin(), m_others.end(), from - 0.005,
                                   [](const std::pair<double, size_t>& o, double v) { return o.first < v; }) - m_others.begin();
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

std::vector<size_t> WaitFollower::takeOthersDue()
{
    std::vector<size_t> due;
    while (m_nextOther < m_others.size()) {
        const double start = m_others[m_nextOther].first;
        // while stopped, the notes starting with the ones waited for wait too
        const bool reached = m_holding ? start < m_clock - CHORD : start <= m_clock;
        if (!reached) {
            break;
        }
        due.push_back(m_others[m_nextOther].second);
        ++m_nextOther;
    }
    return due;
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
    for (auto it = std::lower_bound(m_extras.begin(), m_extras.end(), m_clock - 10.0,
                                    [](const WatchNote& n, double v) { return n.start < v; }); it != m_extras.end(); ++it) {
        const WatchNote& x = *it;
        if (x.start - EARLY > m_clock) {
            break;
        }
        if (x.pitch == pitch && m_clock <= x.end + 0.25) {
            return false;   // (playing the trill: fine)
        }
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
