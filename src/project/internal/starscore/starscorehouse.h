/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — house settings that a .mss style file can't hold, applied after the style:
 * first-page frame height, subtitle offset, staff size by kind of score, and the version footer.
 */
#pragma once

#include <utility>

#include <QString>
#include <QStringList>

#include "global/types/string.h"

namespace mu::engraving {
class Score;
class MasterScore;
class Text;
enum class TextStyleType : unsigned char;
}

namespace mu::project::starscore {
//! Staff height in mm for this score (after Gould, Behind Bars, and MOLA part guidelines):
//!  - part book of one instrument on one visible staff: 7.5 mm
//!  - part book of a keyboard (two or more visible staves): 6.5 mm
//!  - scores, by number of visible staves: up to 4: 6.5; 5–6: 6.0; 7–10: 5.5; 11–16: 5.0; 17–24: 4.5; more: 4.0
double houseStaffHeightMm(const mu::engraving::Score* score, bool partBook);

//! Applies all house settings. Must be called inside a command (prepareChanges/commitChanges).
void applyHouseStyle(mu::engraving::Score* score, bool partBook, const QString& version);

//! Moves a long composer credit down, clear of the title, the subtitle and the arrangement label (laid out first)
void clearComposerCredit(mu::engraving::Score* score);
//! The part scores' composer and lyricist texts (title frame) made the main score's (Joel, 19 Oct 2026: Ciao Ferrari's
//! composers changed in the main score stayed as they were in the part scores, whose texts weren't linked to it). A
//! part score without that text, or a main score without it, is left alone. Undoable when apply; else only counts.
//! Returns how many texts differ (were changed).
int syncCreditTexts(mu::engraving::MasterScore* master, bool apply);
//! The main score's composer and lyricist texts, as one string (to see whether they changed)
QString creditTexts(const mu::engraving::Score* score);

//! The composers the title frame's composer text names, in its order: one per line, or several on a line split at
//! " / ", " & ", ", ", " and " or a wide space; arrangers left out ("arr. Joel McCullough", "(arr. …)", "Horns arr. …"),
//! and the template's "Composer / arranger" too
QStringList composerNames(const mu::engraving::Score* score);
//! The first text of that style in the score's title frames
const mu::engraving::Text* titleFrameText(const mu::engraving::Score* score, mu::engraving::TextStyleType type);
//! The title frame's title (else the work title)
QString songTitle(const mu::engraving::Score* score);
//! "A", "A and B", "A, B and C"
QString joinNames(const QStringList& names);
//! The copyright line printed at the foot of a sheet's first page (Joel, 21 Oct 2026). An original: "© <year> <owners>.
//! All rights reserved. …", the owners its composers, Joel McCullough after the others, and Starsign too when someone
//! besides Joel wrote it. A cover: "“<title>” by <composers>. All rights in the composition belong to its copyright
//! owners. Arranged by Joel McCullough …". Empty when the composer text names nobody.
QString copyrightNotice(const QString& title, const QStringList& composers, bool cover, int year);

//! Levels the arrangement label with the instrument name (laid out first; undoable). Returns whether it moved.
bool levelArrangementLabel(mu::engraving::Score* score);
//! The arrangement label and its shorter forms, in the order tried: "Flexible 2-Horn Arrangement", "Flexible 2H
//! Arrangement", "Flexible\n2H Arrangement", "Flexible 2H\nArrangement" (no duplicates).
QStringList arrangementLabelVariants(const QString& label);
//! An arrangement label as the export gives it: its first line, and a second, smaller line under it when there is one
//! ("4-Horn Arrangement\nwith 2 trumpets": Joel, 16 Oct 2026)
std::pair<QString, QString> splitArrangementLabel(const QString& label);
//! The label's text as XML: `main` (one of its forms) and, when given, `note` on a line of its own at about three
//! quarters of `size` (the label's font size)
muse::String arrangementLabelXml(const QString& main, const QString& note, double size);
//! The instrument name's text as XML (top left of a sheet): the name, and its mute lines ("with Harmon Mute", "and Cup
//! Mute") at the size of the arrangement label's second line (Joel, 17 Oct 2026). `plain` is the text with line breaks.
muse::String instrumentNameXml(const QString& plain, double size);
//! Whether a sheet's instrument name shows these words, its mute lines in the smaller size
bool instrumentNameShows(const mu::engraving::Text* name, const QString& plain);

//! The smaller second line of an arrangement label in a score ("" when it has none)
QString arrangementLabelNote(const mu::engraving::Text* label);
//! The arrangement label (top right) running into the title: replaced by its next shorter form until it clears the
//! title (and subtitle), or the forms run out (the last one stays). Lays out after each change. Undoable. Returns
//! whether it changed the label.
bool fitArrangementLabel(mu::engraving::Score* score);
//! The instrument name's first line and its shorter forms, in the order tried (Joel, 17 Oct 2026): "Soprano
//! Saxophone", "Soprano Sax", "Sop Sax", "Sop"; "Trumpet 1 in B♭", "B♭ Trumpet 1", "Trumpet 1", "Tpt 1". A name with
//! no shorter form is broken over two lines instead, before the last word, the last two… (13 Oct 2026)
QStringList instrumentNameVariants(const QString& firstLine);
//! The instrument name (top left) running into the title: its first line shortened (instrumentNameVariants) until it
//! clears the title and subtitle, or the forms run out. The lines under it (the mutes) stay. Lays out after each
//! change. Undoable. Returns whether it changed the name.
//! With `plain` (the name as the sheet gives it, with its mute lines): a shorter form shown that isn't needed is put
//! back to the full name first, so the name is always the longest form that clears the title.
bool fitInstrumentName(mu::engraving::Score* score, const QString& plain = QString());
//! Whether the instrument name (top left) runs into the title or subtitle as last laid out
bool instrumentNameOverlapsTitle(mu::engraving::Score* score);

//! Writes "Version x.y.z" into the footer text (Starsign 2.3 has "Version 1.0.0" in the right-hand box),
//! replacing the version already there, or adding it to the right-hand box. Must be called inside a command.
//! true when a footer changed. direct: the style set without the undo stack and without Score::styleChanged (which
//! walks every element of the score, the stale ones of a part score not laid out since too: Drum Battle's export
//! crashed there, 14 Oct 2026); the caller lays the score out.
bool applyVersionFooter(mu::engraving::Score* score, const QString& version, bool direct = false);

//! "Version 4.0.1" <-> "4.0.1"
QString versionFromCopyright(const QString& copyright);   // empty when there is none
QString copyrightWithVersion(const QString& copyright, const QString& version);
}
