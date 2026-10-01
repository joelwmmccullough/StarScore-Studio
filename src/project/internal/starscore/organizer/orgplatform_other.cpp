/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: fallbacks where macOS frameworks aren't available
 * (measuring PDFs with poppler's pdftotext when it is installed; no rendering, tags or web access)
 */
#include "orgplatform.h"

#include <QProcess>
#include <QRegularExpression>

namespace mu::project::starscore::org {
static QString pdftotext(const QStringList& args)
{
    QProcess p;
    p.start("pdftotext", args);
    if (!p.waitForFinished(30000) || p.exitStatus() != QProcess::NormalExit) {
        return QString();
    }
    return QString::fromUtf8(p.readAllStandardOutput());
}

PdfMeasure measurePdf(const QString& path)
{
    PdfMeasure m;
    const QString t = pdftotext({ path, "-" });
    if (t.isEmpty()) {
        return m;
    }
    m.ok = true;
    m.pages = std::max<int>(1, int(t.count('\f')));
    for (const QChar c : t) {
        if (c.unicode() >= 0xE000 && c.unicode() <= 0xF8FF) {
            ++m.glyphs;
        }
    }
    for (const QString& w : t.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) {
        bool alnum = true;
        for (const QChar c : w) {
            if (!c.isLetterOrNumber()) {
                alnum = false;
                break;
            }
        }
        m.words += alnum ? 1 : 0;
    }
    return m;
}

QString pdfFirstPageText(const QString& path, int maxLines)
{
    QStringList lines;
    for (const QString& l : pdftotext({ "-f", "1", "-l", "1", path, "-" }).split('\n')) {
        if (!l.trimmed().isEmpty()) {
            lines << l.trimmed();
        }
        if (lines.size() >= maxLines) {
            break;
        }
    }
    return lines.join('|');
}

bool setFinderColour(const QString&, const QString&) { return false; }
QString finderColour(const QString&) { return QString(); }
bool canRenderPdf() { return false; }

void renderPdfs(const std::vector<RenderJob>& jobs, std::function<void(int, bool)> eachDone, std::function<void()> allDone)
{
    for (int i = 0; i < int(jobs.size()); ++i) {
        eachDone(i, false);
    }
    allDone();
}

void fetchPage(const QString&, bool, std::function<void(const QString&, int)> done)
{
    done(QString(), 0);
}
}
