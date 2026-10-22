/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Spelling borrowed from the sheets written by hand (Joel, 22 Oct 2026). A Flexible version sheet is a chair written
 * in concert pitch, transposed for an instrument when it's exported: the transposition picks each note's spelling
 * from the key, and none of the care taken over the same music in the Standard arrangements' part scores in that
 * transposition (E♯ rather than F, a double flat, a courtesy accidental in parentheses) carries over. Here each bar
 * of the version sheet is looked up among the bars of those part scores: where one holds the same music (same
 * rhythm, same notes in any octave, same key signature), its spellings and its hand-made accidentals are used.
 */
#pragma once

#include <vector>

#include <QString>

namespace mu::engraving {
class Score;
}

namespace mu::project::starscore {
struct SpellingResult {
    int bars = 0;               // bars of the target with notes
    int matched = 0;            // of those, with a bar of the same music in a reference
    int sameBeat = 0;           // of those, the match at the same place in the song
    int respelled = 0;          // notes given another spelling
    int accidentalsCopied = 0;  // parentheses or hand-made accidentals copied
    int tiesFixed = 0;          // tied notes given their tie's spelling
};

//! The target's first staff (a version sheet, at written pitch) spelled as `refs` spell the same bars (part scores at
//! the same written transposition, at written pitch). A bar matches when its key signature, rhythm and notes (pitch
//! classes, any octave) are the same; a match at the same place in the song wins, else the spelling most of the
//! matches agree on. Notes tied over a barline keep one spelling. Undoable: call between prepareChanges and
//! commitChanges; lays the target out.
SpellingResult copyReferenceSpellings(mu::engraving::Score* target, const std::vector<const mu::engraving::Score*>& refs);
}
