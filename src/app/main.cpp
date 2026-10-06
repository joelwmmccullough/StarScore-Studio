/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
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

#include <csignal>

#include <QApplication>
#include <QStyleHints>
#include <QQuickWindow>
#include <QSslSocket>

#include "appfactory.h"
#include "internal/commandlineparser.h"
#include "global/iapplication.h"

#include "muse_framework_config.h"
#include "app_config.h"

#include "log.h"

#ifndef MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctime>
#include <cstring>
#endif

//! StarScore (Joel, 6 Oct 2026): a crash leaves a report with the call stack in ~/StarScore Studio/Logs
//! (crash-<date>-<pid>.txt) when that folder is there, and on stderr, and then the default handler runs so macOS writes
//! its own crash report too (exiting here used to stop it). Only async-signal-safe calls in the handler.
static char s_crashDir[1024] = { 0 };

static void crashWrite(int fd, const char* text)
{
    if (fd >= 0 && text) {
        ssize_t ignored = write(fd, text, strlen(text));
        (void)ignored;
    }
}

static void crashCallback(int signum)
{
    const char* signame = "UNKNOWN SIGNAL";
    switch (signum) {
    case SIGILL: signame = "SIGILL (illegal instruction)";
        break;
    case SIGSEGV: signame = "SIGSEGV (invalid memory reference)";
        break;
    case SIGFPE: signame = "SIGFPE (arithmetic error)";
        break;
    case SIGABRT: signame = "SIGABRT (abort)";
        break;
#ifdef SIGBUS
    case SIGBUS: signame = "SIGBUS (bus error)";
        break;
#endif
    }

#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    int fd = -1;
    if (s_crashDir[0]) {
        char path[1200];
        char stamp[64];
        const time_t now = time(nullptr);
        struct tm tmv;
        localtime_r(&now, &tmv);
        strftime(stamp, sizeof(stamp), "%y%m%d_%H%M%S", &tmv);
        snprintf(path, sizeof(path), "%s/crash-%s-%d.txt", s_crashDir, stamp, int(getpid()));
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    }
    void* frames[128];
    const int n = backtrace(frames, 128);
    for (int target : { fd, 2 }) {
        crashWrite(target, "StarScore Studio crashed: ");
        crashWrite(target, signame);
        crashWrite(target, "\nCall stack (most recent first):\n");
        backtrace_symbols_fd(frames, n, target);
        crashWrite(target, "The log with the same time in this folder shows what was happening just before.\n");
    }
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
#endif
    // and the system's own handling (macOS writes a crash report with the stack, symbols and threads)
    signal(signum, SIG_DFL);
    raise(signum);
}

static void installCrashCallback()
{
#if defined(Q_OS_MACOS) || defined(Q_OS_LINUX)
    if (const char* home = getenv("HOME")) {
        char dir[1024];
        snprintf(dir, sizeof(dir), "%s/StarScore Studio/Logs", home);
        if (access(dir, W_OK) == 0) {
            strncpy(s_crashDir, dir, sizeof(s_crashDir) - 1);
        }
    }
#endif
    signal(SIGSEGV, crashCallback);
    signal(SIGILL, crashCallback);
    signal(SIGFPE, crashCallback);
    signal(SIGABRT, crashCallback);
#ifdef SIGBUS
    signal(SIGBUS, crashCallback);
#endif
}

#endif

static void app_init_qrc()
{
    Q_INIT_RESOURCE(app);

#ifdef Q_OS_WIN
    Q_INIT_RESOURCE(app_win);
#endif
}

int main(int argc, char** argv)
{
#ifndef MUSE_MODULE_DIAGNOSTICS_CRASHPAD_CLIENT
    installCrashCallback();
#endif

    // ====================================================
    // Setup global Qt application variables
    // ====================================================

    app_init_qrc();

    qputenv("QT_STYLE_OVERRIDE", "Fusion");
    qputenv("QML_DISABLE_DISK_CACHE", "true");

    // HACK: Workaround for crash #28840. This disables the incremental GC
    if (!qEnvironmentVariableIsSet("MU_QV4_GC_TIMELIMIT")) {
        qputenv("QV4_GC_TIMELIMIT", "0");
    }

    if (!qEnvironmentVariableIsSet("QT_QUICK_FLICKABLE_WHEEL_DECELERATION")) {
        qputenv("QT_QUICK_FLICKABLE_WHEEL_DECELERATION", "5000");
    }

#ifdef Q_OS_LINUX
    if (qEnvironmentVariable("MU_QT_QPA_PLATFORM") != "offscreen") {
        qputenv("QT_QPA_PLATFORMTHEME", "gtk3");
    }

    //! NOTE Forced X11, with Wayland there are a number of problems now
    if (qEnvironmentVariable("MU_QT_QPA_PLATFORM") == "") {
        qputenv("QT_QPA_PLATFORM", "xcb");
    }
#endif

#ifdef Q_OS_WIN
    // NOTE: There are some problems with rendering the application window on some integrated graphics processors
    //       see https://github.com/musescore/MuseScore/issues/8270
    if (!qEnvironmentVariableIsSet("QT_OPENGL_BUGLIST")) {
        qputenv("QT_OPENGL_BUGLIST", ":/resources/win_opengl_buglist.json");
    }
#endif

    QGuiApplication::styleHints()->setMousePressAndHoldInterval(250);

// Can't use MUSE_APP_TITLE until next major release, because this "application name" is used to determine
// where user settings are stored. Changing it would result in all user settings being lost.
#ifdef MUSE_APP_UNSTABLE
    QCoreApplication::setApplicationName("MuseScore4Development");
#else
    QCoreApplication::setApplicationName("MuseScore4");
#endif
    QCoreApplication::setOrganizationName("MuseScore");
    QCoreApplication::setOrganizationDomain("musescore.org");
    QCoreApplication::setApplicationVersion(MUSE_APP_VERSION);

#if !defined(Q_OS_WIN) && !defined(Q_OS_DARWIN) && !defined(Q_OS_WASM)
    // Any OS that uses Freedesktop.org Desktop Entry Specification (e.g. Linux, BSD)
#ifndef MUSE_APP_INSTALL_SUFFIX
#define MUSE_APP_INSTALL_SUFFIX ""
#endif
    QGuiApplication::setDesktopFileName("org.musescore.MuseScore" MUSE_APP_INSTALL_SUFFIX);
#endif

    using namespace muse;
    using namespace mu::app;

    auto fixSslBackend = []() {
#ifdef Q_OS_WIN
        // NOTE: Force schannel backend. Qt prefers OpenSSL when qopensslbackend.dll
        //       is present, which can crash on ABI mismatch with bundled OpenSSL.
        //       see https://github.com/musescore/MuseScore/issues/33401
        QSslSocket::setActiveBackend("schannel");
#endif
    };

    // ====================================================
    // Parse command line options
    // ====================================================
#ifdef MUE_ENABLE_CONSOLEAPP
    CommandLineParser commandLineParser;
    commandLineParser.init();
    commandLineParser.parse(argc, argv);

    IApplication::RunMode runMode = commandLineParser.runMode();
    QCoreApplication* qapp = nullptr;

    if (runMode == IApplication::RunMode::AudioPluginRegistration) {
        qapp = new QCoreApplication(argc, argv);
    } else {
        qapp = new QApplication(argc, argv);
    }

    fixSslBackend();

    commandLineParser.processBuiltinArgs(*qapp);
    CmdOptions opt = commandLineParser.options();

#else
    QCoreApplication* qapp = new QApplication(argc, argv);

    fixSslBackend();

    CmdOptions opt;
    opt.runMode = IApplication::RunMode::GuiApp;
#endif

    AppFactory f;
    std::shared_ptr<muse::IApplication> app = f.newApp(opt);

    app->setup();

    app->setupNewContext();

    LOGI() << QString("SSL Info: supported: %1, build: %2, runtime: %3, active backend: %4, available backends: %5")
        .arg(QSslSocket::supportsSsl())
        .arg(QSslSocket::sslLibraryBuildVersionString())
        .arg(QSslSocket::sslLibraryVersionString())
        .arg(QSslSocket::activeBackend())
        .arg(QSslSocket::availableBackends().join(", "));

    // ====================================================
    // Run main loop
    // ====================================================
    int code = qapp->exec();

    // ====================================================
    // Quit
    // ====================================================

    app->finish();

    delete qapp;

    LOGI() << "Goodbye!! code: " << code;
    return code;
}
