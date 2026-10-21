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
class EngravingItem;
class Note;
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

//! Copies where the texts sit and how they look from one part score to another holding the same music: staff and
//! system text, tempo marks, rehearsal marks, expressions, dynamics and chord symbols (matched by place in the song and
//! words), codas and jumps, the frames' heights and their texts (title, composer… matched by kind), and lines
//! (hairpins, 8va, text lines). Offset, placement, visibility, alignment, font and frame. Lays the target out.
//! Call inside a command. Returns how many things changed.
int copyTextPositions(const mu::engraving::Score* source, mu::engraving::Score* target);
//! Each bar's width ("stretch", Format › Stretch) from one score to another, bars matched by position in the song.
//! Undoable (inside a command). Returns how many bars changed.
int copyMeasureWidths(const mu::engraving::Score* source, mu::engraving::Score* target);
//! Room for the lyrics on that staff (Joel, 21 Oct 2026: a Bass + Voice sheet with the bass part's systems squeezed
//! a bar of eight syllables to a quarter of the line, "Iain't'fraid ofnoghost"). Lays the score out, finds syllables
//! closer than a gap (more where a hyphen goes between), widens their bars (stretch), and splits a system in two
//! halves where widening hasn't made room. Inside an undo step. Returns how many changes it made.
int spaceLyrics(mu::engraving::Score* score, mu::engraving::staff_idx_t staffIdx);

//! The clef saved at the very start of a part's staff (and of every staff linked to it, as in its part score) set to
//! `concert`/`transposing`. A file keeps that first clef as an element of its own, which the staff's default clef
//! (what an instrument change sets) doesn't change, so after a clef change only the first system kept the old clef.
//! Undoable: call inside a command. Returns how many clefs changed.
int setFirstClefs(mu::engraving::Part* part, int concert, int transposing);
//! Whether a sheet's instrument name ("Trumpet in B♭", "Alto Saxophone") is a brass instrument, the only ones with a mute
bool isBrassSheet(const QString& sheetName);
//! The mutes offered for an instrument (Joel, 13 Oct 2026), the most used first, as the sheet names them ("Harmon
//! Mute"); empty for an instrument that takes no mute. Trumpet, cornet and flugelhorn: straight, cup, Harmon,
//! plunger, bucket, solotone, pixie, derby (hat), practice. Trombone, bass trombone, euphonium, baritone:
//! the same (pixie and plunger together are the classic trombone "growl"). Tuba and sousaphone: straight, cup, practice.
//! French horn: straight, stopping (brass) mute, practice.
QStringList muteOptions(const QString& instrumentId);
//! The note a time signature's tempo marking counts in (Joel, 13 Oct 2026). Compound meters (a top number above 3
//! that divides by 3: 6/8, 9/8, 12/8, 6/4, 12/16) count the dotted note that holds three of the bottom note (6/8:
//! dotted quarter; 6/4: dotted half; 6/16: dotted eighth). Simple and odd meters count the bottom note (4/4, 3/4,
//! 5/4: quarter; 2/2, 3/2: half; 3/8, 5/8, 7/8: eighth; 7/16: 16th). `noteValue` is 1 (whole), 2, 4, 8 or 16.
void tempoBeatFor(int numerator, int denominator, int& noteValue, bool& dotted);
//! The lines under the instrument name for a part's mutes: "with Harmon Mute", "and Cup Mute" ("with A,", "B", "and C"
//! for three)
QString muteLines(const QStringList& mutes);
//! Whether every note of a part (grace notes too) sounds within [lowest, highest] (MIDI pitches)
bool pitchesWithin(const mu::engraving::Part* part, int lowest, int highest);
//! A C flute's range, C4 to C7: the Flexible Flute sheet is written an octave up when any note falls outside it
constexpr int FLUTE_LOWEST = 60;
constexpr int FLUTE_HIGHEST = 96;
//! Whether a text is only a mute or open marking: "mute", "(open)", "harmon", "(plunger)", "Harmon (stem out)",
//! "w/ plunger", "con sord.", "+ o"… ("Open solos" and "straight 8ths" are not)
bool isMuteMarking(const QString& text);
//! Whether an instrument is a woodwind (a saxophone, clarinet, flute, double reed…), which takes no mute
bool isWoodwindInstrument(const QString& instrumentId);
//! Hides the mute and open markings in a sheet for an instrument without a mute: the texts that are only such a
//! marking, and the brass mute symbols on notes. Undoable (inside a command). Returns how many were hidden; `hidden`
//! gets them (to show them again after printing).
int hideMuteMarkings(mu::engraving::Score* score, std::vector<mu::engraving::EngravingItem*>* hidden = nullptr);
//! Rhythm markings (Joel, 14 Oct 2026): the small, silent notes with slash (or diamond, or x) heads that show a
//! rhythm on the first space above the staff, or below it where the space above is taken (Drum Battle, letter L). As
//! notes they have a pitch, so a paste onto another instrument, a transposition or an instrument change moved them.
//! Pinned, they sit on their space whatever their pitch, as MuseScore's own slash notation does (FIXED, FIXED_LINE).
bool isRhythmMarking(const mu::engraving::Note* note);
//! Every rhythm marking not pinned yet pinned to the space above the staff (or below it, for one that sits in the
//! lower half of the staff). Undoable (inside a command) when apply; else only counts. Returns how many.
int pinRhythmMarkings(mu::engraving::Score* score, bool apply);
//! The rests beside rhythm markings are placed by the layout (centred 2 spaces beyond the staff on the markings' side,
//! restlayout.cpp); a rest moved up or down by hand before then (Drum Battle's were raised 2 spaces) would be moved
//! twice, so its vertical offset goes, in the full score and every part score. Undoable (inside a command) when apply;
//! else only counts. Returns how many.
int clearRhythmRestOffsets(mu::engraving::MasterScore* master, bool apply);
//! The selected notes made rhythm markings: small, silent, slash heads (a marking's own head kept), stems up and pinned
//! to the space above the staff, or stems down below it. Undoable (inside a command). Returns how many notes.
int makeRhythmMarkings(mu::engraving::Score* score, bool below);
//! The selected rhythm markings made normal notes again (Joel, 19 Oct 2026): normal size, played, normal heads (a slash
//! head goes back to a normal one; a diamond or x head stays), stems automatic, no longer pinned, so they sit at their
//! own pitch. In the full score and every part score. Undoable (inside a command). Returns how many notes.
int unmakeRhythmMarkings(mu::engraving::Score* score);
//! Whether the part has any note (a rest-only part is empty)
bool partHasNotes(const mu::engraving::Part* part);

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

//! The Keys sheet: every keyboard part's lower staves (bass clef) "Hide when empty: Always" in every part score, and
//! "Never" in the full score (Joel, 13 Oct 2026; the lead sheet's keyboards: "Always" there too); the keyboard's own
//! part book with "Automatically hide all empty staves" on and "Don't hide empty staves in first system" off.
//! Returns how many settings it changed (0 = already so).
int applyKeysStaffRules(mu::engraving::MasterScore* master, const std::vector<const mu::engraving::Part*>& leadSheetParts = {});
//! In a throwaway copy of the song printed from (the Scores, a part's sheet made fresh): the keyboards' lower staves
//! "Always hide" in the full score too, so those sheets come out as before
int hideEmptyKeysStavesInCopy(mu::engraving::MasterScore* master);

//! The band's name for a horn ("Trumpet", "Tenor Sax", "Bari Sax", "Bass Trombone"…), or empty when the instrument
//! is not a horn
QString bandHornName(const QString& instrumentId);
//! The 1-Horn sheets' names (Joel, 5 Oct 2026): "Eb Saxophone" and "Bb Saxophone" for the alto and tenor sax; every
//! other horn as bandHornName ("Trumpet", "Trombone")
QString oneHornName(const QString& instrumentId);

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

//! The part's name changed through the undo stack, so Undo puts the old name back with the rest of the command
//! (Part::setPartName on its own isn't undoable). Inside a command; outside one it just changes the name.
void undoSetPartName(mu::engraving::Part* part, const QString& name);

//! The staves of these parts (of the main score) bracketed together, in the main score and in every part score or
//! arrangement score that has all of them, where they sit together and none of them is in a bracket yet. With apply
//! false only counts. Undoable; call inside a command. Returns in how many scores a bracket was (or would be) added.
int bracketPartsTogether(mu::engraving::MasterScore* master, const std::vector<mu::engraving::Part*>& parts, bool apply);
}
