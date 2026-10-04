/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer on macOS: PDFKit (measuring sheets), WebKit (HTML -> PDF, and web pages
 * that need a browser), NSURLSession (web pages), Finder tags (folder colours).
 *
 * Printing a WKWebView: a printOperation run modally for an off-screen window; the synchronous
 * runOperation deadlocks with WebKit, and createPDF makes one tall page. WebKit's print path honours
 * @page size and margins and draws 1 CSS px as 0.8 pt (Chromium: 0.75 pt), so our pages say zoom: 0.9375
 * to come out the same size as the old Chromium-made PDFs.
 */
#include "orgplatform.h"

#import <Cocoa/Cocoa.h>
#import <PDFKit/PDFKit.h>
#import <WebKit/WebKit.h>

#include <algorithm>

#include <QFileInfo>

namespace mu::project::starscore::org {
static NSString* ns(const QString& s)
{
    return s.toNSString();
}

static QString qs(NSString* s)
{
    return QString::fromNSString(s ? s : @"");
}

// ------------------------------------------------------------------ PDFKit
PdfMeasure measurePdf(const QString& path)
{
    PdfMeasure m;
    @autoreleasepool {
        PDFDocument* doc = [[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:ns(path)]];
        if (!doc) {
            return m;
        }
        m.ok = true;
        m.pages = int(doc.pageCount);
        NSMutableString* all = [NSMutableString string];
        for (NSInteger i = 0; i < doc.pageCount; ++i) {
            NSString* t = [[doc pageAtIndex:i] string];
            if (t) {
                [all appendString:t];
                [all appendString:@"\n"];
            }
        }
        const NSUInteger n = all.length;
        for (NSUInteger i = 0; i < n; ++i) {
            const unichar c = [all characterAtIndex:i];
            if (c >= 0xE000 && c <= 0xF8FF) {
                ++m.glyphs;
            }
        }
        NSCharacterSet* notAlnum = [[NSCharacterSet alphanumericCharacterSet] invertedSet];
        for (NSString* w in [all componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]]) {
            if (w.length && [w rangeOfCharacterFromSet:notAlnum].location == NSNotFound) {
                ++m.words;
            }
        }
        if (m.pages < 1) {
            m.pages = 1;
        }
    }
    return m;
}

QString pdfFirstPageText(const QString& path, int maxLines)
{
    @autoreleasepool {
        PDFDocument* doc = [[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:ns(path)]];
        if (!doc || doc.pageCount == 0) {
            return QString();
        }
        QStringList lines;
        for (const QString& l : qs([[doc pageAtIndex:0] string]).split('\n')) {
            if (!l.trimmed().isEmpty()) {
                lines << l.trimmed();
            }
            if (lines.size() >= maxLines) {
                break;
            }
        }
        return lines.join('|');
    }
}

// ------------------------------------------------------------------ Finder tags
static const QStringList OUR_COLOURS { "Gray", "Green", "Purple", "Blue", "Yellow", "Red", "Orange" };

bool setFinderColour(const QString& folder, const QString& colour)
{
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:ns(folder) isDirectory:YES];
        NSArray* tags = nil;
        [url getResourceValue:&tags forKey:NSURLTagNamesKey error:nil];
        NSMutableArray* keep = [NSMutableArray array];
        for (NSString* t in tags ?: @[]) {
            if (!OUR_COLOURS.contains(qs(t))) {
                [keep addObject:t];
            }
        }
        if (!colour.isEmpty()) {
            [keep addObject:ns(colour)];
        }
        NSError* err = nil;
        return [url setResourceValue:keep forKey:NSURLTagNamesKey error:&err];
    }
}

QString finderColour(const QString& folder)
{
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:ns(folder) isDirectory:YES];
        NSArray* tags = nil;
        [url getResourceValue:&tags forKey:NSURLTagNamesKey error:nil];
        for (NSString* t in tags ?: @[]) {
            if (OUR_COLOURS.contains(qs(t))) {
                return qs(t);
            }
        }
        return QString();
    }
}
}

// ------------------------------------------------------------------ WebKit printing
using mu::project::starscore::org::RenderJob;

@interface StarScoreOrgPrinter : NSObject <WKNavigationDelegate>
{
@public
    std::vector<RenderJob> _jobs;
    std::function<void(int, bool)> _eachDone;
    std::function<void()> _allDone;
}
@property (nonatomic) int index;
@property (nonatomic) int generation;
@property (nonatomic, strong) WKWebView* view;
@property (nonatomic, strong) NSWindow* window;
@property (nonatomic, strong) NSTimer* watchdog;
@property (nonatomic, strong) StarScoreOrgPrinter* keepAlive;
@end

@implementation StarScoreOrgPrinter
- (void)start
{
    self.keepAlive = self;   // lives until the last job is done
    self.index = -1;
    [self next];
}

- (void)next
{
    self.index += 1;
    if (self.index >= int(_jobs.size())) {
        auto done = _allDone;
        self.keepAlive = nil;
        if (done) {
            done();
        }
        return;
    }
    const RenderJob& job = _jobs[self.index];
    WKWebViewConfiguration* cfg = [WKWebViewConfiguration new];
    if (@available(macOS 13.3, *)) {
        cfg.preferences.shouldPrintBackgrounds = YES;
    }
    const NSRect r = NSMakeRect(0, 0, 816, 1056);
    self.view = [[WKWebView alloc] initWithFrame:r configuration:cfg];
    self.window = [[NSWindow alloc] initWithContentRect:r styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered defer:NO];
    self.window.releasedWhenClosed = NO;
    self.window.contentView = self.view;
    [self.window setFrameOrigin:NSMakePoint(-30000, -30000)];
    [self.window orderBack:nil];
    self.view.navigationDelegate = self;

    const int gen = self.generation;
    __weak StarScoreOrgPrinter* weakSelf = self;
    self.watchdog = [NSTimer timerWithTimeInterval:90 repeats:NO block:^(NSTimer*) {
        StarScoreOrgPrinter* s = weakSelf;
        if (s && gen == s.generation) {
            [s finish:NO];
        }
    }];
    [[NSRunLoop mainRunLoop] addTimer:self.watchdog forMode:NSRunLoopCommonModes];
    [self.view loadHTMLString:job.html.toNSString() baseURL:nil];
}

- (void)finish:(BOOL)ok
{
    [self.watchdog invalidate];
    self.watchdog = nil;
    const int i = self.index;
    const QString path = _jobs[i].pdfPath;
    self.generation += 1;
    WKWebView* oldView = self.view;
    NSWindow* oldWindow = self.window;
    oldView.navigationDelegate = nil;
    self.view = nil;
    self.window = nil;
    if (_eachDone) {
        _eachDone(i, ok && QFileInfo(path).size() > 0);
    }
    // the print operation may still hold the view for a moment
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [oldWindow orderOut:nil];
        (void)oldView;
        [self next];
    });
}

- (void)webView:(WKWebView*)w didFailNavigation:(WKNavigation*)n withError:(NSError*)e { [self finish:NO]; }
- (void)webView:(WKWebView*)w didFailProvisionalNavigation:(WKNavigation*)n withError:(NSError*)e { [self finish:NO]; }

- (void)webView:(WKWebView*)w didFinishNavigation:(WKNavigation*)n
{
    const int gen = self.generation;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.2 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (gen != self.generation) {
            return;
        }
        const RenderJob& job = self->_jobs[self.index];
        if (!job.pageHeightFromHtml) {
            [self printJob:gen paperHeight:job.pageHeight];
            return;
        }
        // StarScore chord charts: one page as tall as the page's script says (data-height, in points). The page is
        // 480 CSS px wide and the paper 360 pt, so WebKit's print path scales it by 0.75 pt per px — the same scale
        // Chromium printed the prototype at, which is what data-height was measured for.
        [self.view evaluateJavaScript:@"document.body.dataset.height" completionHandler:^(id result, NSError* err) {
            if (gen != self.generation) {
                return;
            }
            double h = 0;
            if ([result respondsToSelector:@selector(doubleValue)]) {
                h = [result doubleValue];
            }
            const RenderJob& j = self->_jobs[self.index];
            [self printJob:gen paperHeight:std::max(j.pageHeight, h > 0 ? h + 2 : 0.0)];
        }];
    });
}

- (void)printJob:(int)gen paperHeight:(double)paperHeight
{
    const RenderJob& job = self->_jobs[self.index];
    NSPrintInfo* pi = [[NSPrintInfo alloc] initWithDictionary:@{
        NSPrintJobDisposition: NSPrintSaveJob,
        NSPrintJobSavingURL: [NSURL fileURLWithPath:job.pdfPath.toNSString()] }];
    pi.paperSize = NSMakeSize(job.pageWidth, paperHeight);
    pi.topMargin = job.marginTop;
    pi.rightMargin = job.marginRight;
    pi.bottomMargin = job.marginBottom;
    pi.leftMargin = job.marginLeft;
    pi.horizontalPagination = NSPrintingPaginationModeFit;
    pi.verticalPagination = NSPrintingPaginationModeAutomatic;
    pi.horizontallyCentered = NO;
    pi.verticallyCentered = NO;
    pi.orientation = NSPaperOrientationPortrait;
    [pi.dictionary setObject:@NO forKey:NSPrintHeaderAndFooter];
    NSPrintOperation* op = [self.view printOperationWithPrintInfo:pi];
    op.showsPrintPanel = NO;
    op.showsProgressPanel = NO;
    op.view.frame = self.view.bounds;
    [op runOperationModalForWindow:self.window delegate:self
                    didRunSelector:@selector(printOperationDidRun:success:contextInfo:)
                       contextInfo:(void*)(intptr_t)gen];
}

- (void)printOperationDidRun:(NSPrintOperation*)op success:(BOOL)ok contextInfo:(void*)ctx
{
    if (int(intptr_t(ctx)) == self.generation) {
        [self finish:ok];
    }
}
@end

// ------------------------------------------------------------------ web pages through a browser view
@interface StarScoreOrgPageLoader : NSObject <WKNavigationDelegate>
{
@public
    std::function<void(const QString&, int)> _done;
}
@property (nonatomic, strong) WKWebView* view;
@property (nonatomic, strong) NSTimer* watchdog;
@property (nonatomic, strong) StarScoreOrgPageLoader* keepAlive;
@property (nonatomic) BOOL finished;
@end

@implementation StarScoreOrgPageLoader
- (void)load:(NSString*)url
{
    self.keepAlive = self;
    // never play anything: a YouTube page would otherwise start its video, with sound
    WKWebViewConfiguration* cfg = [WKWebViewConfiguration new];
    cfg.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeAll;
    self.view = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 1200, 900) configuration:cfg];
    self.view.navigationDelegate = self;
    __weak StarScoreOrgPageLoader* weakSelf = self;
    self.watchdog = [NSTimer timerWithTimeInterval:45 repeats:NO block:^(NSTimer*) {
        [weakSelf complete:@"" status:0];
    }];
    [[NSRunLoop mainRunLoop] addTimer:self.watchdog forMode:NSRunLoopCommonModes];
    [self.view loadRequest:[NSURLRequest requestWithURL:[NSURL URLWithString:url]]];
}

- (void)complete:(NSString*)html status:(int)status
{
    if (self.finished) {
        return;
    }
    self.finished = YES;
    [self.watchdog invalidate];
    self.view.navigationDelegate = nil;
    auto done = _done;
    WKWebView* old = self.view;
    self.view = nil;
    dispatch_async(dispatch_get_main_queue(), ^{
        (void)old;
        self.keepAlive = nil;
    });
    if (done) {
        done(QString::fromNSString(html), status);
    }
}

- (void)webView:(WKWebView*)w didFailNavigation:(WKNavigation*)n withError:(NSError*)e { [self complete:@"" status:0]; }
- (void)webView:(WKWebView*)w didFailProvisionalNavigation:(WKNavigation*)n withError:(NSError*)e { [self complete:@"" status:0]; }
- (void)webView:(WKWebView*)w didFinishNavigation:(WKNavigation*)n
{
    // give the page's scripts a moment, then take its HTML
    __weak StarScoreOrgPageLoader* weakSelf = self;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(1.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        StarScoreOrgPageLoader* s = weakSelf;
        if (!s || s.finished) {
            return;
        }
        [s.view evaluateJavaScript:@"document.documentElement.outerHTML" completionHandler:^(id result, NSError* err) {
            [s complete:[result isKindOfClass:[NSString class]] ? (NSString*)result : @"" status:result ? 200 : 0];
        }];
    });
}
@end

namespace mu::project::starscore::org {
bool canRenderPdf()
{
    return true;
}

void renderPdfs(const std::vector<RenderJob>& jobs, std::function<void(int, bool)> eachDone, std::function<void()> allDone)
{
    StarScoreOrgPrinter* p = [StarScoreOrgPrinter new];
    p->_jobs = jobs;
    p->_eachDone = eachDone;
    p->_allDone = allDone;
    [p start];
}

void fetchPage(const QString& url, bool useBrowser, std::function<void(const QString&, int)> done)
{
    if (useBrowser) {
        StarScoreOrgPageLoader* l = [StarScoreOrgPageLoader new];
        l->_done = done;
        [l load:url.toNSString()];
        return;
    }
    NSMutableURLRequest* req = [NSMutableURLRequest requestWithURL:[NSURL URLWithString:url.toNSString()]
                                                       cachePolicy:NSURLRequestReloadIgnoringLocalCacheData
                                                   timeoutInterval:30];
    [req setValue:@"Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/18.0 Safari/605.1.15"
     forHTTPHeaderField:@"User-Agent"];
    [req setValue:@"en-US,en;q=0.9" forHTTPHeaderField:@"Accept-Language"];
    [req setValue:@"text/html,application/xhtml+xml" forHTTPHeaderField:@"Accept"];
    auto cb = done;
    NSURLSessionDataTask* task = [[NSURLSession sharedSession] dataTaskWithRequest:req
                                                                 completionHandler:^(NSData* data, NSURLResponse* resp, NSError* err) {
        const int status = [resp isKindOfClass:[NSHTTPURLResponse class]] ? int(((NSHTTPURLResponse*)resp).statusCode) : 0;
        const QString body = data && !err ? QString::fromUtf8(static_cast<const char*>(data.bytes), int(data.length)) : QString();
        dispatch_async(dispatch_get_main_queue(), ^{
            cb(body, status);
        });
    }];
    [task resume];
}
}
