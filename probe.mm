// StarScore probe 2: WebKit HTML -> PDF inside a real app run loop (NSApp run), each job with a watchdog.
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

static void describePdf(NSString* label, NSString* path) {
    PDFDocument* doc = [[PDFDocument alloc] initWithURL:[NSURL fileURLWithPath:path]];
    if (!doc) { say(@"  %@: no PDF written", label); return; }
    int links = 0;
    for (NSInteger i = 0; i < doc.pageCount; ++i)
        for (PDFAnnotation* a in [doc pageAtIndex:i].annotations)
            if ([a.type isEqualToString:@"Link"]) ++links;
    PDFPage* p0 = [doc pageAtIndex:0];
    NSRect box = [p0 boundsForBox:kPDFDisplayBoxMediaBox];
    NSString* text = [[p0 string] stringByReplacingOccurrencesOfString:@"\n" withString:@" | "];
    if (text.length > 140) text = [text substringToIndex:140];
    NSDictionary* attrs = [[NSFileManager defaultManager] attributesOfItemAtPath:path error:nil];
    say(@"  %@: %ld pages, %.0fx%.0f, %d links, %llu bytes, p1: %@", label, (long)doc.pageCount,
        box.size.width, box.size.height, links, [attrs fileSize], text);
}

@interface Job : NSObject
@property NSString* html; @property NSString* variant; @property NSString* pdf;
@end
@implementation Job @end

@interface Runner : NSObject <NSApplicationDelegate, WKNavigationDelegate>
@property NSMutableArray<Job*>* jobs;
@property NSString* dir; @property NSString* out;
@property WKWebView* wv; @property NSWindow* win; @property Job* cur;
@property NSDate* t0; @property NSTimer* watchdog; @property int generation;
@end

@implementation Runner
- (void)applicationDidFinishLaunching:(NSNotification*)n {
    say(@"launched; app bundle %@", [[NSBundle mainBundle] bundleIdentifier]);
    [self next];
}
- (void)finishJob:(NSString*)why {
    [self.watchdog invalidate]; self.watchdog = nil;
    say(@"  %@ %@ in %.2fs", self.cur.variant, why, -self.t0.timeIntervalSinceNow);
    describePdf(self.cur.variant, self.cur.pdf);
    self.generation++;
    WKWebView* oldView = self.wv; NSWindow* oldWin = self.win;
    oldView.navigationDelegate = nil;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.5 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        [oldWin orderOut:nil];
        (void)oldView;
        [self next];
    });
}
- (void)next {
    if (self.jobs.count == 0) { say(@"done"); [NSApp terminate:nil]; return; }
    self.cur = self.jobs.firstObject; [self.jobs removeObjectAtIndex:0];
    say(@"== %@ %@", self.cur.html, self.cur.variant);
    WKWebViewConfiguration* cfg = [WKWebViewConfiguration new];
    if (@available(macOS 13.3, *)) { cfg.preferences.shouldPrintBackgrounds = YES; }
    double w = 816;
    NSRect r = NSMakeRect(0, 0, w, 1056);
    self.wv = [[WKWebView alloc] initWithFrame:r configuration:cfg];
    self.win = [[NSWindow alloc] initWithContentRect:r styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    self.win.releasedWhenClosed = NO;
    self.win.contentView = self.wv;
    if ([self.cur.variant hasSuffix:@"v"]) { [self.win makeKeyAndOrderFront:nil]; } else { [self.win setFrameOrigin:NSMakePoint(-20000, -20000)]; [self.win orderBack:nil]; }
    self.wv.navigationDelegate = self;
    self.t0 = [NSDate date];
    int gen = self.generation;
    self.watchdog = [NSTimer scheduledTimerWithTimeInterval:40 repeats:NO block:^(NSTimer* t) {
        if (gen == self.generation) [self finishJob:@"TIMED OUT"];
    }];
    [[NSRunLoop currentRunLoop] addTimer:self.watchdog forMode:NSModalPanelRunLoopMode];
    NSString* path = [[self.dir stringByAppendingPathComponent:@"html"] stringByAppendingPathComponent:self.cur.html];
    NSString* html = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    if ([self.cur.variant isEqualToString:@"BZ"]) {
        html = [html stringByReplacingOccurrencesOfString:@"<head>" withString:@"<head><style>html{zoom:0.9375}</style>"];
    }
    [self.wv loadHTMLString:html baseURL:nil];
}
- (void)webView:(WKWebView*)w didFailNavigation:(WKNavigation*)n withError:(NSError*)e { say(@"  load failed %@", e); [self finishJob:@"load failed"]; }
- (void)webView:(WKWebView*)w didFailProvisionalNavigation:(WKNavigation*)n withError:(NSError*)e { say(@"  provisional failed %@", e); [self finishJob:@"load failed"]; }
- (void)webViewWebContentProcessDidTerminate:(WKWebView*)w { say(@"  web content process terminated"); }
- (void)webView:(WKWebView*)w didFinishNavigation:(WKNavigation*)n {
    say(@"  loaded in %.2fs", -self.t0.timeIntervalSinceNow);
    int gen = self.generation;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (gen != self.generation) return;
        if ([self.cur.variant hasPrefix:@"C"]) {
            [self.wv createPDFWithConfiguration:[WKPDFConfiguration new] completionHandler:^(NSData* data, NSError* err) {
                if (gen != self.generation) return;
                if (data) [data writeToFile:self.cur.pdf atomically:YES]; else say(@"  createPDF error %@", err);
                [self finishJob:@"createPDF done"];
            }];
            return;
        }
        NSPrintInfo* pi = [[NSPrintInfo alloc] initWithDictionary:@{ NSPrintJobDisposition: NSPrintSaveJob,
                                                                    NSPrintJobSavingURL: [NSURL fileURLWithPath:self.cur.pdf] }];
        pi.paperSize = NSMakeSize(612, 792);
        pi.topMargin = 40; pi.bottomMargin = 37; pi.leftMargin = 42; pi.rightMargin = 42;
        pi.horizontalPagination = NSPrintingPaginationModeFit;
        pi.verticalPagination = NSPrintingPaginationModeAutomatic;
        pi.horizontallyCentered = NO; pi.verticallyCentered = NO;
        [pi.dictionary setObject:@NO forKey:NSPrintHeaderAndFooter];
        if ([self.cur.variant isEqualToString:@"BS"]) { [pi.dictionary setObject:@0.9375 forKey:NSPrintScalingFactor]; }
        NSPrintOperation* op = [self.wv printOperationWithPrintInfo:pi];
        op.showsPrintPanel = NO; op.showsProgressPanel = NO;
        op.view.frame = self.wv.bounds;
        [op runOperationModalForWindow:self.win delegate:self didRunSelector:@selector(printDone:success:contextInfo:) contextInfo:(void*)(intptr_t)gen];
    });
}
- (void)printDone:(NSPrintOperation*)op success:(BOOL)ok contextInfo:(void*)ctx {
    if ((int)(intptr_t)ctx != self.generation) return;
    [self finishJob:[NSString stringWithFormat:@"print finished ok=%d", ok]];
}
@end

int main(int argc, const char* argv[]) {
    @autoreleasepool {
        NSString* dir = [NSString stringWithUTF8String:argv[1]];
        NSString* out = [NSString stringWithUTF8String:argv[2]];
        [[NSFileManager defaultManager] createDirectoryAtPath:out withIntermediateDirectories:YES attributes:nil error:nil];
        gReport = fopen([[out stringByAppendingPathComponent:@"report.txt"] fileSystemRepresentation], "a");
        say(@"macOS %@", [[NSProcessInfo processInfo] operatingSystemVersionString]);
        Runner* r = [Runner new];
        r.dir = dir; r.out = out; r.jobs = [NSMutableArray new];
        for (NSString* f in @[ @"ruler.html", @"bandguide.html", @"progress.html", @"allrecordings.html", @"whatshere.html" ]) {
            for (NSString* v in @[ @"BZ", @"BS" ]) {
                Job* j = [Job new]; j.html = f; j.variant = v;
                j.pdf = [out stringByAppendingPathComponent:[NSString stringWithFormat:@"%@-%@.pdf", [f stringByDeletingPathExtension], v]];
                [r.jobs addObject:j];
            }
        }
        NSApplication* app = [NSApplication sharedApplication];
        [app setActivationPolicy:NSApplicationActivationPolicyAccessory];
        app.delegate = r;
        [app run];
    }
    return 0;
}
