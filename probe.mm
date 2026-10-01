// StarScore probe: can WebKit on macOS turn our HTML into paginated PDFs, and can PDFKit read music glyphs?
#import <Cocoa/Cocoa.h>
#import <WebKit/WebKit.h>
#import <PDFKit/PDFKit.h>

static FILE* gReport;
static void say(NSString* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    NSString* s = [[NSString alloc] initWithFormat:fmt arguments:ap];
    va_end(ap);
    fprintf(gReport, "%s\n", s.UTF8String); fflush(gReport);
    fprintf(stderr, "%s\n", s.UTF8String);
}

static void spin(BOOL (^done)(void), double seconds) {
    NSDate* end = [NSDate dateWithTimeIntervalSinceNow:seconds];
    while (!done() && end.timeIntervalSinceNow > 0) {
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
        [[NSRunLoop currentRunLoop] runMode:NSModalPanelRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }
}

@interface Waiter : NSObject <WKNavigationDelegate>
@property BOOL loaded;
@property BOOL printed;
@property BOOL printOk;
@end
@implementation Waiter
- (void)webView:(WKWebView*)w didFinishNavigation:(WKNavigation*)n { self.loaded = YES; }
- (void)webView:(WKWebView*)w didFailNavigation:(WKNavigation*)n withError:(NSError*)e { say(@"  load failed: %@", e); self.loaded = YES; }
- (void)printOperationDidRun:(NSPrintOperation*)op success:(BOOL)ok contextInfo:(void*)ctx { self.printOk = ok; self.printed = YES; }
@end

static void describePdf(NSString* label, NSString* path) {
    PDFDocument* doc = [[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:path]];
    if (!doc) { say(@"  %@: no PDF written", label); return; }
    int links = 0;
    for (NSInteger i = 0; i < doc.pageCount; ++i) {
        for (PDFAnnotation* a in [doc pageAtIndex:i].annotations) {
            if ([a.type isEqualToString:@"Link"]) { ++links; }
        }
    }
    PDFPage* p0 = [doc pageAtIndex:0];
    NSRect box = [p0 boundsForBox:kPDFDisplayBoxMediaBox];
    NSString* text = [[p0 string] stringByReplacingOccurrencesOfString:@"\n" withString:@" | "];
    if (text.length > 160) { text = [text substringToIndex:160]; }
    NSDictionary* attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
    say(@"  %@: %ld pages, page size %.0fx%.0f, %d links, %llu bytes, page 1 text: %@", label, (long)doc.pageCount,
        box.size.width, box.size.height, links, [attrs fileSize], text);
}

static WKWebView* loadHtml(NSString* htmlPath, Waiter* w, NSWindow** winOut) {
    WKWebViewConfiguration* cfg = [WKWebViewConfiguration new];
    if (@available(macOS 13.3, *)) { cfg.preferences.shouldPrintBackgrounds = YES; }
    NSRect r = NSMakeRect(0, 0, 816, 1056);
    WKWebView* wv = [[WKWebView alloc] initWithFrame:r configuration:cfg];
    NSWindow* win = [[NSWindow alloc] initWithContentRect:r styleMask:NSWindowStyleMaskBorderless
                                                  backing:NSBackingStoreBuffered defer:NO];
    win.contentView = wv;
    [win setFrameOrigin:NSMakePoint(-20000, -20000)];
    [win orderBack:nil];
    wv.navigationDelegate = w;
    NSString* html = [NSString stringWithContentsOfFile:htmlPath encoding:NSUTF8StringEncoding error:nil];
    [wv loadHTMLString:html baseURL:[NSURL fileURLWithPath:NSTemporaryDirectory()]];
    spin(^{ return w.loaded; }, 30);
    *winOut = win;
    return wv;
}

static NSPrintInfo* printInfo(NSString* outPath, CGFloat margin) {
    NSPrintInfo* pi = [[NSPrintInfo alloc] initWithDictionary:@{
        NSPrintJobDisposition: NSPrintSaveJob,
        NSPrintJobSavingURL: [NSURL fileURLWithPath:outPath] }];
    pi.paperSize = NSMakeSize(612, 792);
    pi.topMargin = margin; pi.bottomMargin = margin; pi.leftMargin = margin; pi.rightMargin = margin;
    pi.horizontalPagination = NSPrintingPaginationModeFit;
    pi.verticalPagination = NSPrintingPaginationModeAutomatic;
    pi.horizontallyCentered = NO;
    pi.verticallyCentered = NO;
    pi.orientation = NSPaperOrientationPortrait;
    [pi.dictionary setObject:@NO forKey:NSPrintHeaderAndFooter];
    return pi;
}

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        // probe <dir> <out> <file.html|music> <variant A0|A1|B|C>
        NSString* dir = [NSString stringWithUTF8String:argv[1]];
        NSString* out = [NSString stringWithUTF8String:argv[2]];
        NSString* f = [NSString stringWithUTF8String:argv[3]];
        NSString* variant = [NSString stringWithUTF8String:argv[4]];
        [[NSFileManager defaultManager] createDirectoryAtPath:out withIntermediateDirectories:YES attributes:nil error:nil];
        gReport = fopen([[out stringByAppendingPathComponent:@"report.txt"] fileSystemRepresentation], "a");
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        [NSApp finishLaunching];

        if ([f isEqualToString:@"music"]) {
            say(@"macOS %@", [[NSProcessInfo processInfo] operatingSystemVersionString]);
            NSString* music = [dir stringByAppendingPathComponent:@"music/demo.pdf"];
            PDFDocument* doc = [[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:music]];
            NSString* all = doc.string ?: @"";
            NSUInteger pua = 0, words = 0;
            for (NSUInteger i = 0; i < all.length; ++i) { unichar c = [all characterAtIndex:i]; if (c >= 0xE000 && c <= 0xF8FF) ++pua; }
            for (NSString* wd in [all componentsSeparatedByCharactersInSet:[NSCharacterSet whitespaceAndNewlineCharacterSet]]) {
                if (wd.length && [wd rangeOfCharacterFromSet:[[NSCharacterSet alphanumericCharacterSet] invertedSet]].location == NSNotFound) ++words;
            }
            say(@"== music PDF via PDFKit: %ld pages, %lu music glyphs, %lu words (pdftotext: 2 pages, 440 glyphs, 127 words)",
                (long)doc.pageCount, (unsigned long)pua, (unsigned long)words);
            return 0;
        }
        NSString* path = [[dir stringByAppendingPathComponent:@"html"] stringByAppendingPathComponent:f];
        NSString* base = [f stringByDeletingPathExtension];
        NSString* pdf = [out stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@.pdf", base, variant]];
        say(@"== %@ %@", f, variant);
        Waiter* w = [Waiter new]; NSWindow* win = nil;
        NSDate* tl = [NSDate date];
        WKWebView* wv = loadHtml(path, w, &win);
        say(@"  loaded %d in %.2fs", w.loaded, -tl.timeIntervalSinceNow);
        NSDate* t0 = [NSDate date];
        if ([variant hasPrefix:@"A"]) {
            NSPrintOperation* op = [wv printOperationWithPrintInfo:printInfo(pdf, [variant isEqualToString:@"A0"] ? 0 : 40)];
            op.showsPrintPanel = NO; op.showsProgressPanel = NO;
            op.view.frame = wv.bounds;
            BOOL ok = [op runOperation];
            say(@"  %@ runOperation -> %d in %.2fs", variant, ok, -t0.timeIntervalSinceNow);
        } else if ([variant isEqualToString:@"B"]) {
            NSPrintOperation* op = [wv printOperationWithPrintInfo:printInfo(pdf, 0)];
            op.showsPrintPanel = NO; op.showsProgressPanel = NO;
            op.view.frame = wv.bounds;
            [op runOperationModalForWindow:win delegate:w didRunSelector:@selector(printOperationDidRun:success:contextInfo:) contextInfo:nil];
            spin(^{ return w.printed; }, 60);
            say(@"  B modal -> printed %d ok %d in %.2fs", w.printed, w.printOk, -t0.timeIntervalSinceNow);
        } else {
            __block BOOL done = NO;
            [wv createPDFWithConfiguration:[WKPDFConfiguration new] completionHandler:^(NSData* data, NSError* err) {
                if (data) [data writeToFile:pdf atomically:YES]; else say(@"  C error %@", err);
                done = YES;
            }];
            spin(^{ return done; }, 30);
            say(@"  C createPDF done %d in %.2fs", done, -t0.timeIntervalSinceNow);
        }
        describePdf(variant, pdf);
        [win close];
    }
    return 0;
}
