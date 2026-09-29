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
#include <CoreText/CoreText.h>
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

static CFStringRef toCF(const QString& s)
{
    const QByteArray utf8 = s.toUtf8();
    return CFStringCreateWithBytes(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(utf8.constData()), utf8.size(),
                                   kCFStringEncodingUTF8, false);
}

static void drawPageNumber(CGContextRef ctx, const CGRect& page, int number)
{
    CFStringRef text = toCF(QString::number(number));
    CTFontRef font = CTFontCreateWithName(CFSTR("Helvetica"), 9.0, nullptr);
    CGColorSpaceRef space = CGColorSpaceCreateDeviceGray();
    const CGFloat grey[] = { 0.4, 1.0 };
    CGColorRef color = CGColorCreate(space, grey);
    const void* keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    const void* values[] = { font, color };
    CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2, &kCFTypeDictionaryKeyCallBacks,
                                               &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef str = CFAttributedStringCreate(kCFAllocatorDefault, text, attrs);
    CTLineRef line = CTLineCreateWithAttributedString(str);
    const double width = CTLineGetTypographicBounds(line, nullptr, nullptr, nullptr);
    CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
    CGContextSetTextPosition(ctx, page.origin.x + page.size.width - 0.6 * 72 - width, page.origin.y + 0.45 * 72);
    CTLineDraw(line, ctx);
    CFRelease(line);
    CFRelease(str);
    CFRelease(attrs);
    CGColorRelease(color);
    CGColorSpaceRelease(space);
    CFRelease(font);
    CFRelease(text);
}

bool mergePdfs(const std::vector<QString>& inputs, const std::vector<bool>& numberPages, const QString& outPath,
               const QString& title)
{
    const QByteArray utf8 = outPath.toUtf8();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(utf8.constData()),
                                                           utf8.size(), false);
    if (!url) {
        return false;
    }
    CFStringRef cfTitle = toCF(title);
    const void* infoKeys[] = { kCGPDFContextTitle };
    const void* infoValues[] = { cfTitle };
    CFDictionaryRef info = CFDictionaryCreate(kCFAllocatorDefault, infoKeys, infoValues, 1, &kCFTypeDictionaryKeyCallBacks,
                                              &kCFTypeDictionaryValueCallBacks);
    CGRect letter = CGRectMake(0, 0, 612, 792);
    CGContextRef ctx = CGPDFContextCreateWithURL(url, &letter, info);
    CFRelease(info);
    CFRelease(cfTitle);
    CFRelease(url);
    if (!ctx) {
        return false;
    }

    int number = 0;
    for (size_t i = 0; i < inputs.size(); ++i) {
        CGPDFDocumentRef doc = openPdf(inputs[i]);
        if (!doc) {
            continue;
        }
        const size_t n = CGPDFDocumentGetNumberOfPages(doc);
        for (size_t k = 1; k <= n; ++k) {
            CGPDFPageRef page = CGPDFDocumentGetPage(doc, k);
            if (!page) {
                continue;
            }
            ++number;
            CGRect box = CGPDFPageGetBoxRect(page, kCGPDFMediaBox);
            CFDataRef boxData = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(&box), sizeof(box));
            const void* pageKeys[] = { kCGPDFContextMediaBox };
            const void* pageValues[] = { boxData };
            CFDictionaryRef pageInfo = CFDictionaryCreate(kCFAllocatorDefault, pageKeys, pageValues, 1,
                                                          &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
            CGPDFContextBeginPage(ctx, pageInfo);
            CGContextDrawPDFPage(ctx, page);
            if (i < numberPages.size() && numberPages[i]) {
                drawPageNumber(ctx, box, number);
            }
            CGPDFContextEndPage(ctx);
            CFRelease(pageInfo);
            CFRelease(boxData);
        }
        CGPDFDocumentRelease(doc);
    }
    CGPDFContextClose(ctx);
    CGContextRelease(ctx);
    return number > 0;
}
#else
bool mergePdfs(const std::vector<QString>&, const std::vector<bool>&, const QString&, const QString&)
{
    return false;
}

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
