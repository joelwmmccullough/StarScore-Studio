/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "qfontprovider.h"

#include <QFont>
#include <QPaintDevice>
#include <QFontDatabase>
#include <QFontMetricsF>
#include <QRawFont>
#include <QPainterPath>

#include <mutex>

using namespace muse;
using namespace muse::draw;

class FontPaintDevice : public QPaintDevice
{
public:
    QPaintEngine* paintEngine() const override
    {
        return nullptr;
    }

protected:
    static constexpr double MU_ENGRAVING_DPI = 1200; // Same as mu::engraving::DPI. TODO: pass as parameter
    int metric(PaintDeviceMetric m) const override
    {
        switch (m) {
        case QPaintDevice::PdmDpiY:
            return MU_ENGRAVING_DPI;
        default:
            return 1;
        }
    }
};

static FontPaintDevice device;

// StarScore: text measurements cached per font and text. Qt shapes the text (HarfBuzz) on every call, and a layout
// measures the same chord symbols, lyrics and texts again and again: a quarter of an export's time went here. A
// measurement depends only on the font's settings, the text and the installed fonts, so the result is the same each
// time; the cache is forgotten when a font is added (clearFontMetricsCache).
namespace {
struct MetricsCache {
    std::mutex mutex;
    QHash<QString, RectF> tightRects;
    QHash<QString, RectF> rects;
    QHash<QString, double> advances;
    QHash<QString, double> fontValues;   // "<font>\x1fcap" etc.
    static constexpr int LIMIT = 200000;   // entries per table; forgotten when reached (a few dozen MB at most)
};

MetricsCache& metricsCache()
{
    static MetricsCache cache;
    return cache;
}

QString fontKey(const Font& f)
{
    QString key = f.family().id().toQString();
    key += QChar(0x1f);
    key += QString::number(f.pointSizeF(), 'g', 17);
    key += QChar(0x1f);
    key += QString::number(f.pixelSize());
    key += QChar(0x1f);
    key += QString::number(int(f.weight()));
    key += QChar(0x1f);
    key += QChar(QLatin1Char('0' + (f.bold() ? 1 : 0) + (f.italic() ? 2 : 0) + (f.underline() ? 4 : 0)));
    key += QChar(QLatin1Char('0' + (f.strike() ? 1 : 0) + (f.noFontMerging() ? 2 : 0)));
    key += QChar(QLatin1Char('0' + int(f.hinting())));
    key += QChar(QLatin1Char('0' + int(f.type())));
    return key;
}

template<typename T, typename Compute>
T cached(QHash<QString, T>& table, const QString& key, Compute compute)
{
    MetricsCache& cache = metricsCache();
    {
        std::lock_guard<std::mutex> lock(cache.mutex);
        auto it = table.constFind(key);
        if (it != table.constEnd()) {
            return it.value();
        }
    }
    const T value = compute();
    std::lock_guard<std::mutex> lock(cache.mutex);
    if (table.size() >= MetricsCache::LIMIT) {
        table.clear();
    }
    table.insert(key, value);
    return value;
}

double cachedFontValue(const Font& f, const char* what, double (*compute)(const QFont&))
{
    const QString key = fontKey(f) + QChar(0x1f) + QLatin1String(what);
    return cached(metricsCache().fontValues, key, [&]() { return compute(f.toQFont()); });
}
}

void muse::draw::clearFontMetricsCache()
{
    MetricsCache& cache = metricsCache();
    std::lock_guard<std::mutex> lock(cache.mutex);
    cache.tightRects.clear();
    cache.rects.clear();
    cache.advances.clear();
    cache.fontValues.clear();
}

int QFontProvider::addSymbolFont(const String& family, const io::path_t& path)
{
    m_symbolsFonts[family] = path;
    const int id = QFontDatabase::addApplicationFont(path.toQString());
    clearFontMetricsCache();
    return id;
}

double QFontProvider::lineSpacing(const Font& f) const
{
    return cachedFontValue(f, "lineSpacing", [](const QFont& qf) -> double { return QFontMetricsF(qf, &device).lineSpacing(); });
}

double QFontProvider::xHeight(const Font& f) const
{
    return cachedFontValue(f, "xHeight", [](const QFont& qf) -> double { return QFontMetricsF(qf, &device).xHeight(); });
}

double QFontProvider::height(const Font& f) const
{
    return cachedFontValue(f, "height", [](const QFont& qf) -> double { return QFontMetricsF(qf, &device).height(); });
}

double QFontProvider::capHeight(const Font& f) const
{
    return cachedFontValue(f, "capHeight", [](const QFont& qf) -> double { return QFontMetrics(qf, &device).capHeight(); });
}

double QFontProvider::ascent(const Font& f) const
{
    return cachedFontValue(f, "ascent", [](const QFont& qf) -> double { return QFontMetricsF(qf, &device).ascent(); });
}

double QFontProvider::descent(const Font& f) const
{
    return cachedFontValue(f, "descent", [](const QFont& qf) -> double { return QFontMetricsF(qf, &device).descent(); });
}

bool QFontProvider::inFont(const Font& f, char32_t ucs4) const
{
    // NOTE: QFontMetricsF::inFontUcs4 is unreliable for our use case because it uses Qt's fallback
    // system even if the flag noFontMerging is set, and returns true if the character
    // is found in any of the fallbacks. We need to use instead QRawFont, which represents the
    // *actual* font, not Qt's interpretation of the query. From QRawFont we can query the glyph index
    // of a character (zero if not in font) and the bounding rectangle of the actual glyph at that index.
    QRawFont qRawFont = QRawFont::fromFont(f.toQFont());
    int glyphIndex = qRawFont.glyphIndexesForString(QString(QChar::fromUcs4(ucs4)))[0];
    if (glyphIndex == 0) {
        return false;
    }

    //! @NOTE some symbols in fonts dont have glyph. For example U+ee80
    //! exists in Bravura.otf but doesn't have glyph
    //! so QFontMetricsF returns true in that case
    return qRawFont.boundingRect(glyphIndex).isValid();
}

double QFontProvider::horizontalAdvance(const Font& f, const String& string) const
{
    return cached(metricsCache().advances, fontKey(f) + QChar(0x1f) + string.toQString(), [&]() {
        return QFontMetricsF(f.toQFont(), &device).horizontalAdvance(string);
    });
}

double QFontProvider::horizontalAdvance(const Font& f, char32_t ucs4) const
{
    if (Char::requiresSurrogates(ucs4)) {
        return cached(metricsCache().advances, fontKey(f) + QChar(0x1f) + String::fromUcs4(ucs4).toQString(), [&]() {
            return QFontMetricsF(f.toQFont(), &device).horizontalAdvance(String::fromUcs4(ucs4));
        });
    }

    // (its own key: Qt measures a lone character without shaping, so it can differ from the one-character string)
    return cached(metricsCache().advances, fontKey(f) + QChar(0x1e) + QChar(static_cast<char16_t>(ucs4)), [&]() {
        return QFontMetricsF(f.toQFont(), &device).horizontalAdvance(static_cast<char16_t>(ucs4));
    });
}

RectF QFontProvider::boundingRect(const Font& f, const String& string) const
{
    return cached(metricsCache().rects, fontKey(f) + QChar(0x1f) + string.toQString(), [&]() {
        return RectF::fromQRectF(QFontMetricsF(f.toQFont(), &device).boundingRect(string));
    });
}

RectF QFontProvider::boundingRect(const Font& f, char32_t ucs4) const
{
    if (Char::requiresSurrogates(ucs4)) {
        return RectF::fromQRectF(QFontMetrics(f.toQFont(), &device).boundingRect(String::fromUcs4(ucs4)));
    }

    if (f.type() == Font::Type::MusicSymbol || f.type() == Font::Type::MusicSymbolText) {
        // QFontMetrics::boundingRect returns pixel values obtained by rasterization of the font.
        // QPainterPath::boundingRect works from the actual vector shape so it's more accurate.
        // There is still some discretization going on so we can make it even more accurate by upscaling.
        // CAUTION: More expensive! Ok for music glyphs because they are cached.
        QFont qf = f.toQFont();
        qf = QFont(qf, &device);
        static constexpr double UPSCALING = 4.0;
        qf.setPointSizeF(qf.pointSizeF() * UPSCALING);
        QPainterPath path;
        path.addText(QPointF(), qf, QString(static_cast<char16_t>(ucs4)));
        return RectF::fromQRectF(path.boundingRect()).scaled(SizeF(1.0 / UPSCALING, 1.0 / UPSCALING));
    }

    return RectF::fromQRectF(QFontMetricsF(f.toQFont(), &device).boundingRect(static_cast<char16_t>(ucs4)));
}

RectF QFontProvider::tightBoundingRect(const Font& f, const String& string) const
{
    return cached(metricsCache().tightRects, fontKey(f) + QChar(0x1f) + string.toQString(), [&]() {
        auto boundingRect = QFontMetricsF(f.toQFont(), &device).tightBoundingRect(string);
        if (!boundingRect.isValid()) {
            // fix for https://github.com/musescore/MuseScore/issues/19503 - Qt can return garbage bounding rectangles that corrupt layout
            return RectF();
        }
        return RectF::fromQRectF(boundingRect);
    });
}
