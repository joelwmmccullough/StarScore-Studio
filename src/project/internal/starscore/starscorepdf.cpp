/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — reading reference PDFs with macOS CoreGraphics
 */
#include "starscorepdf.h"

#include <algorithm>

#include <QImage>

#ifdef Q_OS_MACOS
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#endif

namespace mu::project::starscore {
#ifdef Q_OS_MACOS
static CGPDFDocumentRef openPdf(const QString& pdfPath)
{
    const QByteArray utf8 = pdfPath.toUtf8();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault,
                                                           reinterpret_cast<const UInt8*>(utf8.constData()),
                                                           utf8.size(), false);
    if (!url) {
        return nullptr;
    }
    CGPDFDocumentRef doc = CGPDFDocumentCreateWithURL(url);
    CFRelease(url);
    return doc;
}

int pdfPageCount(const QString& pdfPath)
{
    CGPDFDocumentRef doc = openPdf(pdfPath);
    if (!doc) {
        return 0;
    }
    const int n = int(CGPDFDocumentGetNumberOfPages(doc));
    CGPDFDocumentRelease(doc);
    return n;
}

bool renderPdfPage(const QString& pdfPath, int page, int widthPx, const QString& pngPath)
{
    CGPDFDocumentRef doc = openPdf(pdfPath);
    if (!doc) {
        return false;
    }
    CGPDFPageRef pdfPage = CGPDFDocumentGetPage(doc, size_t(page + 1));   // pages are 1-based
    if (!pdfPage) {
        CGPDFDocumentRelease(doc);
        return false;
    }

    // Page size as displayed (rotation applied)
    const CGRect box = CGPDFPageGetBoxRect(pdfPage, kCGPDFCropBox);
    const int rotation = CGPDFPageGetRotationAngle(pdfPage);
    const bool sideways = rotation == 90 || rotation == 270;
    const double pageW = sideways ? box.size.height : box.size.width;
    const double pageH = sideways ? box.size.width : box.size.height;
    if (pageW <= 0 || pageH <= 0 || widthPx <= 0) {
        CGPDFDocumentRelease(doc);
        return false;
    }
    const int w = widthPx;
    const int h = std::max(1, int(pageH * widthPx / pageW + 0.5));

    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);

    CGColorSpaceRef space = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(image.bits(), size_t(w), size_t(h), 8, size_t(image.bytesPerLine()), space,
                                             uint32_t(kCGImageAlphaPremultipliedFirst) | uint32_t(kCGBitmapByteOrder32Little));
    CGColorSpaceRelease(space);
    if (!ctx) {
        CGPDFDocumentRelease(doc);
        return false;
    }

    CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
    CGContextSetRenderingIntent(ctx, kCGRenderingIntentDefault);
    // Scale the page to the image; CGPDFPageGetDrawingTransform only shrinks, so scale by hand
    const double scale = double(w) / pageW;
    CGContextScaleCTM(ctx, scale, scale);
    const CGRect target = CGRectMake(0, 0, pageW, pageH);
    CGContextConcatCTM(ctx, CGPDFPageGetDrawingTransform(pdfPage, kCGPDFCropBox, target, 0, true));
    CGContextDrawPDFPage(ctx, pdfPage);

    CGContextRelease(ctx);
    CGPDFDocumentRelease(doc);

    return image.save(pngPath, "PNG");
}
#else
int pdfPageCount(const QString&)
{
    return 0;
}

bool renderPdfPage(const QString&, int, int, const QString&)
{
    return false;
}
#endif
}
