/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: what needs the operating system (macOS: PDFKit, WebKit, Finder tags).
 * Other systems get a fallback (pdftotext for measuring; no rendering, no tags).
 */
#pragma once

#include <functional>

#include <QString>
#include <QStringList>

namespace mu::project::starscore::org {
struct PdfMeasure {
    bool ok = false;
    int pages = 0;
    int glyphs = 0;        // SMuFL music symbols (private-use characters) in the text
    int words = 0;         // tokens made only of letters and digits
};

//! Count pages, music symbols and words. Thread-safe.
PdfMeasure measurePdf(const QString& path);
//! The first page's text, lines joined with '|', at most maxLines non-empty lines. Thread-safe.
QString pdfFirstPageText(const QString& path, int maxLines = 8);

//! Finder colour tag on a folder ("Green", "Blue", "Yellow", "Red", or "" to clear ours).
//! Other tags on the folder are kept. Returns false where tags aren't supported.
bool setFinderColour(const QString& folder, const QString& colour);
QString finderColour(const QString& folder);

//! HTML -> PDF. Must be called on the main (GUI) thread; done() is called on the main thread.
//! Pages are US Letter unless the job says otherwise; margins in points (top, right, bottom, left) come from the
//! caller, because WebKit's print path ignores @page margins.
struct RenderJob {
    QString html;
    QString pdfPath;
    double marginTop = 40, marginRight = 42, marginBottom = 37, marginLeft = 42;
    //! Paper size in points. The chord charts print one tall phone-width page: with pageHeightFromHtml the page's
    //! own script reports the height it needs in document.body.dataset.height (points, as Chromium printed the
    //! prototype: CSS px × 0.75) after it has laid itself out, and the paper becomes max(pageHeight, that + 2).
    double pageWidth = 612, pageHeight = 792;
    bool pageHeightFromHtml = false;
};
bool canRenderPdf();
void renderPdfs(const std::vector<RenderJob>& jobs, std::function<void(int index, bool ok)> eachDone,
                std::function<void()> allDone);

//! GET a web page the way Safari would. Main thread; done() on the main thread with the body ("" on failure).
//! useBrowser: load it in a hidden web view instead and return the page's HTML after its scripts ran.
void fetchPage(const QString& url, bool useBrowser, std::function<void(const QString& html, int status)> done);
}
