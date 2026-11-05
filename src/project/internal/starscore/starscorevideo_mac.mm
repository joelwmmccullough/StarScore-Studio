/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice › Listen's video window on the Mac (see starscorevideo.h)
 */
#include "starscorevideo.h"

#import <AppKit/AppKit.h>
#import <AVFoundation/AVFoundation.h>
#import <AVKit/AVKit.h>
#import <CoreMedia/CoreMedia.h>

#include <cmath>

namespace {
NSWindow* s_window = nil;
AVPlayer* s_player = nil;
double s_lastRate = -1;
}

namespace mu::project::starscore::video {
bool available()
{
    return true;
}

bool open(const QString& path, const QString& title)
{
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:path.toNSString()];
        AVPlayerItem* item = [AVPlayerItem playerItemWithURL:url];
        if (!item) {
            return false;
        }
        if (!s_player) {
            s_player = [AVPlayer playerWithPlayerItem:item];
        } else {
            [s_player replaceCurrentItemWithPlayerItem:item];
        }
        s_player.muted = YES;   // (the recording is heard through StarScore; Joel: videos always muted)
        s_player.automaticallyWaitsToMinimizeStalling = NO;
        s_lastRate = -1;
        if (!s_window) {
            NSRect frame = NSMakeRect(120, 120, 640, 360);
            s_window = [[NSWindow alloc] initWithContentRect:frame
                                                   styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                                             | NSWindowStyleMaskResizable | NSWindowStyleMaskMiniaturizable
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
            s_window.releasedWhenClosed = NO;
            s_window.level = NSFloatingWindowLevel;
            s_window.contentAspectRatio = NSMakeSize(16, 9);
            AVPlayerView* view = [[AVPlayerView alloc] initWithFrame:frame];
            view.controlsStyle = AVPlayerViewControlsStyleNone;
            view.player = s_player;
            s_window.contentView = view;
        } else {
            ((AVPlayerView*)s_window.contentView).player = s_player;
        }
        s_window.title = title.toNSString();
        [s_window makeKeyAndOrderFront:nil];
        return true;
    }
}

void close()
{
    if (s_player) {
        [s_player pause];
    }
    if (s_window) {
        [s_window orderOut:nil];
    }
}

bool isOpen()
{
    return s_window && s_window.visible;
}

void follow(double seconds, double speed, bool playing, double offset)
{
    if (!s_player || !isOpen()) {
        return;
    }
    const double target = std::max(0.0, seconds + offset);
    const double now = CMTimeGetSeconds(s_player.currentTime);
    const double drift = std::isfinite(now) ? std::abs(now - target) : 1e9;
    if (!playing) {
        if (s_player.rate != 0) {
            [s_player pause];
        }
        s_lastRate = 0;
        if (drift > 0.02) {
            [s_player seekToTime:CMTimeMakeWithSeconds(target, 600) toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
        }
        return;
    }
    if (drift > 0.12) {
        [s_player seekToTime:CMTimeMakeWithSeconds(target, 600) toleranceBefore:kCMTimeZero toleranceAfter:kCMTimeZero];
    }
    if (std::abs(s_lastRate - speed) > 1e-6 || s_player.rate == 0) {
        s_player.rate = float(speed);
        s_lastRate = speed;
    }
}
}
