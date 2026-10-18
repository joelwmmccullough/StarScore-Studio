/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — checks on a laid-out sheet before it's printed (Joel, 16 Oct 2026: texts running off the page,
 * texts printed over each other and over chord symbols, labels stacked in a staircase on the Combined Parts).
 *
 * The texts looked at are the ones written above or below the music: staff and system texts, expressions, rehearsal
 * marks, tempo markings, chord symbols, codas and jumps. Their places are taken from the layout (page coordinates).
 */
#pragma once

#include <vector>

#include <QString>
#include <QStringList>

namespace mu::engraving {
class Score;
}

namespace mu::project::starscore {
struct LayoutIssue {
    int page = 0;           // 1-based
    int bar = 0;            // the bar number printed (0 when unknown)
    QString kind;           // "off page", "overlap"
    QString what;           // the text(s), for the log
};

//! What's wrong with the sheet's texts as it is laid out now (lay it out first)
std::vector<LayoutIssue> layoutIssues(const mu::engraving::Score* score);

//! Texts that run off the left or right edge of the page are moved in, to start (or end) inside the page margins.
//! Undoable; call inside a command. Lays the score out again when it moved anything. Returns how many it moved.
int keepTextsOnPage(mu::engraving::Score* score);

//! Every text above or below the music back where the style puts it (offset and autoplace), on this score only: for a
//! sheet whose layout is new (the Combined Parts), where positions set by hand for another layout land anywhere.
//! Undoable; call inside a command. Returns how many texts changed.
int resetTextPositions(mu::engraving::Score* score);

//! The Combined Parts' arrangement labels ("2H / 4H–7H / 3H", "All") on one height per line of music, those above the
//! staff level with the highest of them and those below with the lowest (Joel, 16 Oct 2026: labels a bar apart sat
//! at the height of each bar's notes, like a staircase). Undoable; call inside a command. Lays the score out again
//! when it moved anything. Returns how many it moved.
int alignCombinedLabels(mu::engraving::Score* score);

//! A text written by hand over a chord symbol or another text: put back where the style places it (its offset and
//! autoplace on), so MuseScore keeps it clear; one still over a chord symbol after that is moved up (above the staff)
//! or down (below) until it clears. Not chord symbols, rehearsal marks or tempo markings: they keep their places.
//! Undoable; call inside a command. Lays the score out again when it moved anything. Returns how many it moved.
int separateOverlappingTexts(mu::engraving::Score* score);

//! A rest inside a beam printed over the next or previous note's accidental or head (MuseScore moves such a rest clear
//! of the beam after the notes are spaced): its bar a little wider, until it clears them (Joel, 17 Oct 2026). On this
//! score only. Undoable; call inside a command. Lays the score out again when it changed anything. Returns how many
//! bars it widened.
int spaceOutBeamedRests(mu::engraving::Score* score);

//! The issues as log lines ("p2 b89 off page: Chords provided to help…")
QStringList describeIssues(const std::vector<LayoutIssue>& issues);
}
