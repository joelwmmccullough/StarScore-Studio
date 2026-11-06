/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Watch (1.27.0): the notes of the score showing, in playback seconds, for the falling notes.
 */
#pragma once

#include <QString>

#include <vector>

#include "notation/inotation.h"

namespace mu::project::starscore {
struct WatchNote {
    double start = 0;   // seconds of playback (repeats played out, at the playback speed)
    double end = 0;
    int pitch = 60;     // as it plays (a solo's key part score: in its key)
    int hand = 0;       // the staff in its part: 0 the right hand (or the only staff), 1 the left, 2 any after those
};

//! (1.27.2) where a bar starts, for the falling notes' bar lines
struct WatchBar {
    double start = 0;   // seconds, like WatchNote
    int number = 1;     // as the score numbers it
};

//! The playing notes of the score showing (hidden and drum staves left out), by start; name: the part score's name
std::vector<WatchNote> watchNotes(const mu::notation::INotationPtr& notation, QString* name = nullptr,
                                   std::vector<WatchBar>* bars = nullptr);
}
