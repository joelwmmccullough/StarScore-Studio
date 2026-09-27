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
}
