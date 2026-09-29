/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — reading reference PDFs (page count, one page as an image).
 * Uses macOS CoreGraphics; on other systems the functions return 0 / false.
 */
#pragma once

#include <vector>

#include <QString>

namespace mu::project::starscore {
int pdfPageCount(const QString& pdfPath);
//! Renders page (0-based) at the given width in pixels, white background, and saves it as a PNG
bool renderPdfPage(const QString& pdfPath, int page, int widthPx, const QString& pngPath);

//! Joins PDFs into one. Pages are numbered from 1 across the whole result; the number is printed at the bottom
//! right of the pages of the inputs whose numberPages entry is true.
bool mergePdfs(const std::vector<QString>& inputs, const std::vector<bool>& numberPages, const QString& outPath,
               const QString& title);
}
