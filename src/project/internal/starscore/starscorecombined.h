/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Combined parts (Joel, 13 Oct 2026): one sheet per horn chair with the chair's line from every standard horn
 * arrangement (1H to 7H; not the Flexible ones, not the stand-in versions) on one staff. Where arrangements play the
 * same thing they share one line; where they differ, each different line gets a voice of its own (MuseScore's voices
 * 1 to 4: 1 and 3 stems up, 2 and 4 stems down), the higher line on top, and staff text says which arrangements read
 * which line ("3H–5H" above, "6H, 7H" below). The labels come where the grouping changes; "All" where the lines join
 * again. Bars where every arrangement rests don't change the grouping. A passage with more than four different lines
 * puts the rest on a second staff that only shows where it's needed.
 *
 * Chairs are matched by part name: "Trumpet" (1H-4H) and "Trumpet 1" (5H-7H) are one chair, "Trumpet 2" another.
 */
#pragma once

#include <vector>

#include <QString>
#include <QStringList>

#include "notation/imasternotation.h"
#include "project/istarscoreservice.h"

namespace mu::engraving {
class MasterScore;
}

namespace mu::project::starscore {
struct CombinedSource {
    int horns = 0;          // the arrangement: 1 to 7
    QString partId;
};

struct CombinedChair {
    QString name;           // "Alto Sax", "Trumpet 1", "Tenor Sax 2"
    std::vector<CombinedSource> sources;   // by arrangement, smallest first; only parts with notes
};

//! The song's chairs that are in two or more standard horn arrangements with notes
std::vector<CombinedChair> combinedChairs(const mu::engraving::MasterScore* ms, const std::vector<StarScoreSection>& sections);

struct CombinedResult {
    bool ok = false;
    QString targetPartId;   // the part (in the copy) that now holds the combined line
    bool secondStaff = false;
    int passages = 0;       // stretches with one grouping of the arrangements
    int splitPassages = 0;  // of those, with more than one line
    QString problem;
};

//! Writes the chair's combined line into a throwaway copy of the song: the largest arrangement's part is emptied and
//! gets every arrangement's line in its voices, with the labels. One undo step in the copy.
CombinedResult buildCombinedPart(const mu::notation::IMasterNotationPtr& copy, const CombinedChair& chair);

//! One arrangement's part, for the combined part's layout: its part score, and its part in the song (to measure how
//! busy it is)
struct CombinedLayoutSource {
    int horns = 0;
    const mu::engraving::Score* book = nullptr;
    const mu::engraving::Part* part = nullptr;
};

//! The combined part's system breaks, "keep together" marks and system locks, section by section (a section starts
//! at each rehearsal mark): each section's from the part score of the arrangement busiest there (the most notes; the
//! larger arrangement when even). Page breaks become system breaks: the combined systems are taller, so fewer fit on
//! a page (Joel, 13 Oct 2026). A section whose busiest arrangement has no part score keeps MuseScore's own layout.
//! Must be called inside a command on the target. Returns a line per section for the log.
QStringList copySectionLayouts(const std::vector<CombinedLayoutSource>& sources, mu::engraving::Score* target);

//! "3H–5H", "6H, 7H", "2H, 4H–6H"
QString arrangementLabel(std::vector<int> horns);
}
