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
#include "macosmainwindowbridge.h"

#include <Cocoa/Cocoa.h>
#include <QTimer>
#include <QWindow>

using namespace muse::ui;

static NSWindow* nsWindowForQWindow(QWindow* qWindow)
{
    if (!qWindow) {
        return nullptr;
    }

    NSView* nsView = (__bridge NSView*)reinterpret_cast<void*>(qWindow->winId());
    NSWindow* nsWindow = [nsView window];
    return nsWindow;
}

//! StarScore: the title bar's document icon for .starscore files is the StarScore document icon, whatever macOS
//! has cached for the file type (an older registration can still point at the MuseScore icon)
static void starscoreApplyDocumentIcon(QWindow* qWindow)
{
    NSWindow* nsWindow = nsWindowForQWindow(qWindow);
    if (!nsWindow) {
        return;
    }
    NSString* path = qWindow->filePath().toNSString();
    if (![[[path pathExtension] lowercaseString] isEqualToString:@"starscore"]) {
        return;
    }
    NSButton* button = [nsWindow standardWindowButton:NSWindowDocumentIconButton];
    NSString* iconPath = [[NSBundle mainBundle] pathForResource:@"StarScoreIcon" ofType:@"icns"];
    if (!button || !iconPath) {
        return;
    }
    NSImage* image = [[NSImage alloc] initWithContentsOfFile:iconPath];
    if (!image) {
        return;
    }
    [image setSize:NSMakeSize(16, 16)];
    [button setImage:image];
}

MacOSMainWindowBridge::MacOSMainWindowBridge(QObject* parent)
    : MainWindowBridge(parent)
{
}

void MacOSMainWindowBridge::init()
{
    MainWindowBridge::init();

    uiConfiguration()->applyPlatformStyle(m_window);

    uiConfiguration()->currentThemeChanged().onNotify(this, [this]() {
        uiConfiguration()->applyPlatformStyle(m_window);
    });

    // after Qt has set the window's file (and its own icon for it)
    connect(this, &MainWindowBridge::filePathChanged, this, [this]() {
        QTimer::singleShot(0, this, [this]() { starscoreApplyDocumentIcon(m_window); });
        QTimer::singleShot(300, this, [this]() { starscoreApplyDocumentIcon(m_window); });
    });
}

bool MacOSMainWindowBridge::fileModified() const
{
    //! NOTE QWindow misses an API for this, so we'll do it ourselves.
    NSWindow* nsWindow = nsWindowForQWindow(m_window);
    if (!nsWindow) {
        return false;
    }

    return [nsWindow isDocumentEdited];
}

void MacOSMainWindowBridge::setFileModified(bool modified)
{
    NSWindow* nsWindow = nsWindowForQWindow(m_window);
    if (!nsWindow) {
        return;
    }

    if ([nsWindow isDocumentEdited] == modified) {
        return;
    }

    [nsWindow setDocumentEdited:modified];
    emit fileModifiedChanged();
}
