/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice: rendering and opening a practice item's exercise (see starscorepractice.h)
 */
#include "starscoreservice.h"

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include "actions/actiontypes.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/measure.h"
#include "draw/painter.h"
#include "notation/inotationpainting.h"
#include "project/types/projecttypes.h"

#include "log.h"

using namespace mu::project;
using namespace muse;

namespace {
//! the exercise without its backing staves and title (for printing)
QString printableXml(const QString& xml)
{
    QString x = xml;
    static const QRegularExpression backingPart("<score-part id=\"(P[23]|B\\d+)\">.*?</score-part>", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression backing("<part id=\"(P[23]|B\\d+)\">.*?</part>", QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression work("<work>.*?</work>|<movement-title>.*?</movement-title>|<identification>.*?</identification>",
                                         QRegularExpression::DotMatchesEverythingOption);
    x.remove(backingPart);
    x.remove(backing);
    x.remove(work);
    return x;
}

QString withTempo(const QString& xml, int tempo)
{
    if (tempo <= 0) {
        return xml;
    }
    QString x = xml;
    static const QRegularExpression sound("<sound tempo=\"[0-9.]+\"/>");
    static const QRegularExpression perMinute("<per-minute>[0-9.]+</per-minute>");
    x.replace(sound, QString("<sound tempo=\"%1\"/>").arg(tempo));
    x.replace(perMinute, QString("<per-minute>%1</per-minute>").arg(tempo));
    return x;
}

void recolor(QImage& img)
{
    static const QColor CREAM("#EDE6D6");
    for (int y = 0; y < img.height(); ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = line[x];
            const int a = qAlpha(px);
            if (a == 0) {
                continue;
            }
            // premultiplied: un-premultiply to judge the colour
            const int r = qRed(px) * 255 / a;
            const int g = qGreen(px) * 255 / a;
            const int b = qBlue(px) * 255 / a;
            const bool gold = r - b > 50 && r > 90 && g > b;
            if (gold) {
                continue;
            }
            line[x] = qPremultiply(qRgba(CREAM.red(), CREAM.green(), CREAM.blue(), a));
        }
    }
}

QString practiceTempDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/StarScore Practice";
    QDir().mkpath(dir);
    return dir;
}
}

QList<QImage> StarScoreService::renderPracticeExercise(const QString& musicXml, int dpi, double scale, int* printableWidthPx)
{
    QList<QImage> out;
    if (musicXml.isEmpty()) {
        return out;
    }
    const QString path = practiceTempDir() + "/render.musicxml";
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            return out;
        }
        f.write(printableXml(musicXml).toUtf8());
    }
    INotationProjectPtr p = projectCreator()->newProject(iocContext());
    OpenParams params;
    params.skipPlaybackModel = true;
    if (!p->load(io::path_t(path), params) || !p->masterNotation() || !p->masterNotation()->masterScore()) {
        LOGW() << "[starscore] practice: couldn't load an exercise for rendering";
        return out;
    }
    engraving::MasterScore* ms = p->masterNotation()->masterScore();
    // systems close together, as in the course books (no spreading them down the page)
    ms->style().set(engraving::Sid::enableVerticalSpread, false);
    ms->style().set(engraving::Sid::minSystemDistance, engraving::Spatium(7.0));
    ms->style().set(engraving::Sid::maxSystemDistance, engraving::Spatium(9.0));
    ms->style().set(engraving::Sid::minStaffSpread, engraving::Spatium(5.0));
    // no page numbers: an exercise's pages are cut into the book or the runner, where they'd be stray numbers
    ms->style().set(engraving::Sid::showPageNumber, false);
    ms->style().set(engraving::Sid::showHeader, false);
    ms->style().set(engraving::Sid::showFooter, false);
    // an excerpt of a song's own bars asks for no bar numbers (they'd count the excerpt, not the song)
    if (musicXml.contains("<measure-numbering>none</measure-numbering>")) {
        ms->style().set(engraving::Sid::showMeasureNumber, false);
    }
    // bigger or smaller notation: a page narrower (or wider) by the scale, drawn at a dpi higher (or lower) by it, so a
    // full system is the same number of pixels wide and the bars reflow to fit
    scale = std::clamp(scale, 0.5, 2.5);
    const double printable = ms->style().styleD(engraving::Sid::pagePrintableWidth);
    if (printableWidthPx) {
        *printableWidthPx = int(std::lrint(printable * dpi));
    }
    if (std::abs(scale - 1.0) > 0.01) {
        const double margins = ms->style().styleD(engraving::Sid::pageWidth) - printable;
        ms->style().set(engraving::Sid::pagePrintableWidth, printable / scale);
        ms->style().set(engraving::Sid::pageWidth, printable / scale + margins);
        dpi = std::max(20, int(std::lrint(dpi * scale)));
    }
    // chord symbols in gold
    for (engraving::Segment* s = ms->firstSegment(engraving::SegmentType::ChordRest); s; s = s->next1(engraving::SegmentType::ChordRest)) {
        for (engraving::EngravingItem* e : s->annotations()) {
            if (e && e->isHarmony()) {
                e->setColor(muse::draw::Color(207, 168, 94));
            }
        }
    }
    ms->doLayout();
    notation::INotationPtr notation = p->masterNotation()->notation();
    const int pages = notation->painting()->pageCount();
    for (int i = 0; i < pages; ++i) {
        notation::INotationPainting::Options opt;
        opt.fromPage = i;
        opt.toPage = i;
        opt.trimMarginPixelSize = std::max(4, dpi / 20);
        opt.deviceDpi = dpi;
        opt.printPageBackground = false;
        const SizeF inch = notation->painting()->pageSizeInch(opt);
        const int w = std::lrint(inch.width() * dpi);
        const int h = std::lrint(inch.height() * dpi);
        if (w <= 0 || h <= 0) {
            continue;
        }
        QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            muse::draw::Painter painter(&image, "practice");
            notation->painting()->paintPng(&painter, opt);
        }
        recolor(image);
        out << image;
    }
    return out;
}

Ret StarScoreService::openPracticeExercise(const QString& musicXml, const QString& title, int tempo, bool play)
{
    if (musicXml.isEmpty()) {
        return make_ret(Ret::Code::UnknownError);
    }
    QString safe = title;
    static const QRegularExpression bad("[^A-Za-z0-9 ,.()+-]");
    safe.replace(bad, "-");
    safe = safe.left(80).trimmed();
    if (safe.isEmpty()) {
        safe = "Exercise";
    }
    const QString path = practiceTempDir() + "/" + safe + ".musicxml";
    {
        QFile f(path);
        if (!f.open(QIODevice::WriteOnly)) {
            return make_ret(Ret::Code::UnknownError);
        }
        f.write(withTempo(musicXml, tempo).toUtf8());
    }
    dispatcher()->dispatch("starscore-practice-open", actions::ActionData::make_arg2<QUrl, bool>(QUrl::fromLocalFile(path), play));
    return make_ok();
}
