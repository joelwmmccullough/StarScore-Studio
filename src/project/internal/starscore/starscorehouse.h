/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — house settings that a .mss style file can't hold, applied after the style:
 * first-page frame height, staff size by kind of score, chord symbol design, and the version footer.
 */
#pragma once

#include <QString>

namespace mu::engraving {
class Score;
}

namespace mu::project::starscore {
//! Staff height in mm for this score (after Gould, Behind Bars, and MOLA part guidelines):
//!  - part book of one instrument on one visible staff: 7.5 mm
//!  - part book of a keyboard (two or more visible staves): 6.5 mm
//!  - scores, by number of visible staves: up to 4: 6.5; 5–6: 6.0; 7–10: 5.5; 11–16: 5.0; 17–24: 4.5; more: 4.0
double houseStaffHeightMm(const mu::engraving::Score* score, bool partBook);

//! Applies all house settings. Must be called inside a command (prepareChanges/commitChanges).
void applyHouseStyle(mu::engraving::Score* score, bool partBook);

//! Prints the copyright text (where the version goes) at the bottom of every page, not just the first.
//! Must be called inside a command.
void applyVersionFooter(mu::engraving::Score* score);

//! "Version 4.0.1" <-> "4.0.1"
QString versionFromCopyright(const QString& copyright);   // empty when there is none
QString copyrightWithVersion(const QString& copyright, const QString& version);
}
