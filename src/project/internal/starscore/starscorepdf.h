/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — reading reference PDFs (page count, one page as an image).
 * Uses macOS CoreGraphics; on other systems the functions return 0 / false.
 */
#pragma once

#include <QString>

namespace mu::project::starscore {
int pdfPageCount(const QString& pdfPath);
//! Renders page (0-based) at the given width in pixels, white background, and saves it as a PNG
bool renderPdfPage(const QString& pdfPath, int page, int widthPx, const QString& pngPath);
}
