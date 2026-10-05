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
class Measure;
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

//! Score and every part score: tempo marks without fonts written into their text (a part could keep an old font
//! after the metronome mark: "= 100" in Petaluma Script on Balkan Wedding's Drums), and title frames of a fixed
//! height (a frame sized to its contents ignores the house style's height). Undoable when apply is true; returns
//! how many things differ (apply false) or changed.
int tidyTempoAndFrames(mu::engraving::MasterScore* master, bool apply);

//! Fixes a finished sheet's layout: a system lock on every system that has none, and a page break after the last
//! system of every page but the last (where there's no break already). Lays the score out first. Call inside a
//! command. Returns how many locks and breaks were added.
int lockSheetLayout(mu::engraving::Score* score);

//! Copies where the texts sit (staff and system text, tempo marks, rehearsal marks, expressions) from one part score
//! to another holding the same music: a text in the target at the same place in the song with the same words gets
//! the source's position (offset and placement). Call inside a command. Returns how many texts moved.
int copyTextPositions(const mu::engraving::Score* source, mu::engraving::Score* target);
//! Each bar's width ("stretch", Format › Stretch) from one score to another, bars matched by position in the song.
//! Undoable (inside a command). Returns how many bars changed.
int copyMeasureWidths(const mu::engraving::Score* source, mu::engraving::Score* target);
//! Whether a sheet's instrument name ("Trumpet in B♭", "Alto Saxophone") is a brass instrument, the only ones with a mute
bool isBrassSheet(const QString& sheetName);
//! Whether every note of a part (grace notes too) sounds within [lowest, highest] (MIDI pitches)
bool pitchesWithin(const mu::engraving::Part* part, int lowest, int highest);
//! A C flute's range, C4 to C7: the Flexible Flute sheet is written an octave up when any note falls outside it
constexpr int FLUTE_LOWEST = 60;
constexpr int FLUTE_HIGHEST = 96;
//! Hides the texts that are only a mute or open marking ("mute", "(open)", "cup mute", "con sord."…), for a sheet on
//! an instrument without a mute; "Open solos" stays. Undoable (inside a command). Returns how many were hidden.
int hideMuteMarkings(mu::engraving::Score* score);

//! Colour notes (noteheads, accidentals, dots) by pitch class using Joel's 12 colours, or reset them
//! to the default colour. Works on the selected notes, or the whole score when nothing is selected.
//! Must be called inside a command (startCmd/endCmd). Returns the number of notes changed.
int colorNotes(mu::engraving::Score* score, bool colorize);

//! True when the instrument has notes in fewer than a fifth of the bars (an empty or barely started part)
bool partLooksUnfinished(const mu::engraving::Score* score, const mu::engraving::Part* part);

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

//! The band's name for a horn ("Trumpet", "Tenor Sax", "Bari Sax", "Bass Trombone"…), or empty when the instrument
//! is not a horn
QString bandHornName(const QString& instrumentId);

//! Every staff gets the same end barline in each bar: where some staves have a double, final or other special
//! barline and the rest have a plain one, the plain ones get it too (in the score and every part book). Bars whose
//! staves disagree between two special barlines are left alone. Undoable when apply is true; returns how many
//! barlines differ (apply false) or changed.
int syncEndBarlines(mu::engraving::MasterScore* master, bool apply);

//! Gives the first staff of each of `to` the end barlines (double, final…) of the first staff of `from`, bar by bar,
//! in the score and every part book (undoable). Pasted music doesn't bring its barlines: a new stand-in version of
//! the Bass Trombone lost the double barline before each repeat. Returns how many barlines changed.
int copyEndBarlines(mu::engraving::MasterScore* master, const mu::engraving::Part* from,
                    const std::vector<mu::engraving::Part*>& to);

//! Additive time signature, e.g. numerators {4,4,4,3} over 8: bars of 4/8, 4/8, 4/8, 3/8, repeating.
//! The first bar shows "4+4+4+3 / 8"; the following changes are hidden. Runs from `start` to the end
//! of `last` (or, when last is null, up to the next existing time signature or the end of the score).
//! Must be called inside a command. Returns an error message, or an empty string.
QString applyAdditiveTimeSig(mu::engraving::MasterScore* score, mu::engraving::Measure* start, mu::engraving::Measure* last,
                             const std::vector<int>& numerators, int denominator);
}
