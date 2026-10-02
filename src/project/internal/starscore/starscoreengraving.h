/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <vector>

#include <QString>

namespace mu::engraving {
class Score;
class MasterScore;
class Part;
}

namespace mu::project::starscore {
struct LayoutCopyOptions {
    bool lineBreaks = true;
    bool pageBreaks = true;
    bool keepTogether = true;   // "no break" markers
    bool systemLocks = true;    // "lock measures into system" (MuseScore 4.4+)
    bool replaceExisting = true;
};

struct LayoutCopyResult {
    int scoresChanged = 0;
    int breaksAdded = 0;
    int breaksRemoved = 0;
    int locksCopied = 0;
};

//! Copy line/page breaks, "keep together" markers and system locks from one score
//! (main score or a part book) to others, matching bars by position in the song.
//! Must be called inside a command (startCmd/endCmd).
LayoutCopyResult copyLayout(const mu::engraving::Score* source, const std::vector<mu::engraving::Score*>& targets,
                            const LayoutCopyOptions& options);

//! Colour notes (noteheads, accidentals, dots) by pitch class using Joel's 12 colours, or reset them
//! to the default colour. Works on the selected notes, or the whole score when nothing is selected.
//! Must be called inside a command (startCmd/endCmd). Returns the number of notes changed.
int colorNotes(mu::engraving::Score* score, bool colorize);

//! True when the instrument has notes in fewer than a fifth of the bars (an empty or barely started part)
bool partLooksUnfinished(const mu::engraving::Score* score, const mu::engraving::Part* part);

//! Additive time signature, e.g. numerators {4,4,4,3} over 8: bars of 4/8, 4/8, 4/8, 3/8, repeating.
//! The first bar shows "4+4+4+3 / 8"; the following changes are hidden. Runs from `start` to the end
//! of `last` (or, when last is null, up to the next existing time signature or the end of the score).
//! Must be called inside a command. Returns an error message, or an empty string.
//! Lists, for every visible instrument, the bars with notes outside its pro range and outside its
//! amateur range (concert pitch, as MuseScore's own range colouring). Returns a readable report.
QString checkRanges(const mu::engraving::Score* score);

//! How syncBarNumbering changes the part books
enum class BarNumberingSync {
    Check,      // only count the part books that differ
    Undoable,   // through the undo stack (call inside a command)
    Direct      // set directly (a throwaway copy of the file)
};

//! Makes every part book number its bars like the main score: each bar's "exclude from measure count" and
//! "add to measure number" are copied from the main score. MuseScore keeps these per score, so a pickup bar
//! excluded in the main score after the part books were made stays counted in them, and those sheets'
//! bar numbers run one ahead. Returns the number of part books that differ(ed).
int syncBarNumbering(mu::engraving::MasterScore* master, BarNumberingSync how);

//! The Keys sheet: every keyboard part's lower staves (bass clef) "Hide when empty: Always" in every score, and the
//! keyboard's own part book with "Automatically hide all empty staves" on and "Don't hide empty staves in first
//! system" off. Returns how many settings it changed (0 = already so).
int applyKeysStaffRules(mu::engraving::MasterScore* master);

QString applyAdditiveTimeSig(mu::engraving::MasterScore* score, mu::engraving::Measure* start, mu::engraving::Measure* last,
                             const std::vector<int>& numerators, int denominator);
}
