/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Watch (1.27.0): the notes of the score showing, in playback seconds, for the falling notes.
 */
#pragma once

#include <QString>

#include <algorithm>

#include <array>
#include <vector>

#include "notation/inotation.h"

namespace mu::project::starscore {
struct WatchNote {
    double start = 0;   // seconds of playback (repeats played out, at the playback speed)
    double end = 0;
    int pitch = 60;     // as it plays (a solo's key part score: in its key)
    int hand = 0;       // the staff in its part: 0 the right hand (or the only staff), 1 the left, 2 any after those
    int tick = 0;       // (1.27.2) where its chord is in the score (not played out), and on which track
    int track = 0;
    int noteIdx = -1;   // (1.27.3) which note of its chord (< 0: a grace note), for playing it
};

//! (1.27.2) where a bar starts, for the falling notes' bar lines
struct WatchBar {
    double start = 0;   // seconds, like WatchNote
    int number = 1;     // as the score numbers it
};

//! The playing notes of the score showing (hidden and drum staves left out), by start; name: the part score's name
std::vector<WatchNote> watchNotes(const mu::notation::INotationPtr& notation, QString* name = nullptr,
                                   std::vector<WatchBar>* bars = nullptr);

//! (1.27.2) Wait for me (Synthesia's wait mode): the falling notes move on their own clock and stop at each note of the
//! hands chosen until it's played on a MIDI keyboard. Kept apart from the drawing so the autotest can play it.
class WaitFollower
{
public:
    //! hands: 1 the right hand (the first staff), 2 the left, 4 any staff after those; or'd together
    void setNotes(const std::vector<WatchNote>& notes, int hands);
    int hands() const { return m_hands; }
    bool waitsFor(const WatchNote& n) const { return (m_hands & (1 << std::clamp(n.hand, 0, 2))) != 0; }

    //! from: the time to start at; the first notes waited for are the ones starting there or later. start() also
    //! clears the counts of right and wrong notes
    void start(double from);
    void seek(double from);
    //! the clock moves on by secs (the notes' seconds already include the playback speed), stopping at the next notes
    void advance(double secs);
    //! a key pressed: true if it's one of the next notes (pressed up to EARLY seconds before they come)
    bool noteOn(int pitch);
    void noteOff(int pitch);

    double clock() const { return m_clock; }
    bool holding() const { return m_holding; }
    bool finished() const { return m_next >= m_groups.size(); }
    //! the next notes' keys, and which of them have been played
    std::vector<int> waitingFor() const;
    double nextStart() const { return finished() ? m_end : m_groups[m_next].start; }
    //! where the next notes are in the score (the first of them), for the playback cursor; tick < 0: none
    int nextTick() const { return finished() ? -1 : m_groups[m_next].tick; }
    int nextTrack() const { return finished() ? 0 : m_groups[m_next].track; }
    bool expected(int pitch) const;
    bool hit(int pitch) const { return pitch >= 0 && pitch < 128 && m_hit[pitch]; }
    bool wrong(int pitch) const { return pitch >= 0 && pitch < 128 && m_wrong[pitch]; }
    bool held(int pitch) const { return pitch >= 0 && pitch < 128 && m_held[pitch]; }
    int rightCount() const { return m_right; }
    int wrongCount() const { return m_wrongCount; }
    double end() const { return m_end; }
    //! (1.27.3) the notes of the hands not waited for that the clock has reached since the last call (indexes into the
    //! notes given to setNotes), for StarScore to play them: notes starting with the next notes waited for come once
    //! those are played
    std::vector<size_t> takeOthersDue();

    static constexpr double EARLY = 0.6;
    static constexpr double CHORD = 0.04;   // notes starting this close together are played together

private:
    struct Group {
        double start = 0;
        std::vector<int> pitches;
        int tick = 0;
        int track = 0;
    };
    void checkDone();

    std::vector<Group> m_groups;
    std::vector<std::pair<double, size_t> > m_others;   // (start, index) of the notes not waited for
    size_t m_nextOther = 0;
    size_t m_next = 0;
    int m_hands = 7;
    double m_clock = 0;
    double m_end = 0;
    bool m_holding = false;
    std::array<bool, 128> m_hit {};
    std::array<bool, 128> m_wrong {};
    std::array<bool, 128> m_held {};
    int m_right = 0;
    int m_wrongCount = 0;
};
}
