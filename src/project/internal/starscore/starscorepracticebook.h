/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice: the practice book PDF (dark, in the style of Joel's course books: TT Modernoir headings,
 * Jost text, gold rules and "move on when" lines)
 */
#pragma once

#include <functional>
#include <map>

#include <QImage>
#include <QList>
#include <QString>

#include "starscorepractice.h"

namespace mu::project::practice {
//! Renders an item's exercise as an image on a transparent background, notation in light colours, chord symbols in
//! gold (an empty image when it can't)
using NotationRenderer = std::function<QList<QImage>(const Item& item, int tempo)>;   // one image per page

//! Writes the plan (or one day of it, dayIndex >= 0) as a PDF book. progress: called with (done, total) blocks.
bool writePracticeBook(const Plan& plan, const std::map<QString, Library>& libraries, const QString& pdfPath,
                       const NotationRenderer& renderer, int dayIndex = -1,
                       const std::function<void(int, int)>& progress = nullptr);
}
