/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — don't start while an update is being installed (macOS).
 *
 * The install script (src/framework/update/internal/starscore_macos_install.sh) writes
 * "~/Library/Application Support/StarScore Studio/update-in-progress" holding "<its pid> <start time, Unix seconds>"
 * while it replaces the app, and removes it before it opens the new app. When StarScore is opened in the meantime it
 * shows the "Installation in progress" window instead and quits at once (Joel, 15 Oct 2026); the script opens the new
 * StarScore when it's done.
 *
 * Opening StarScore must never be blocked by mistake, so the lock is only believed when all of these hold, and is
 * deleted otherwise:
 *   - it reads as two numbers;
 *   - it was written less than 20 minutes ago (an install takes a minute or two; the script gives up waiting for the
 *     old app after two minutes);
 *   - that pid is running, and is the install script ("starscore-update-<pid>.sh" in its command line).
 * Anything unexpected (no HOME, unreadable file, ps failing) counts as "no install running".
 *
 * Plain POSIX and C++ only: this runs first thing in main(), before Qt is set up and before anything is loaded from
 * the app bundle the script may be replacing.
 */

#pragma once

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include <sys/types.h>
#include <unistd.h>

namespace starscore::updatelock {
inline constexpr long MAX_AGE_SECONDS = 20 * 60;

inline std::string lockDir()
{
    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        return std::string();
    }
    return std::string(home) + "/Library/Application Support/StarScore Studio";
}

inline bool isRunning(long pid)
{
    if (pid <= 1) {
        return false;
    }
    return ::kill(pid_t(pid), 0) == 0 || errno == EPERM;
}

//! The command line of a running process ("" when there is none or ps fails)
inline std::string commandOf(long pid)
{
    if (pid <= 1) {
        return std::string();
    }
    const std::string cmd = "/bin/ps -p " + std::to_string(pid) + " -o command= 2>/dev/null";
    FILE* p = ::popen(cmd.c_str(), "r");
    if (!p) {
        return std::string();
    }
    std::string out;
    char buf[512];
    while (std::fgets(buf, sizeof(buf), p)) {
        out += buf;
    }
    ::pclose(p);
    return out;
}

inline long readPidFile(const std::string& path, long* time = nullptr)
{
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) {
        return 0;
    }
    long pid = 0;
    long when = 0;
    const int n = std::fscanf(f, "%ld %ld", &pid, &when);
    std::fclose(f);
    if (n < 1) {
        return 0;
    }
    if (time) {
        *time = n >= 2 ? when : 0;
    }
    return pid;
}

//! The pid of the install script running now, or 0. A lock that doesn't hold up is deleted.
inline long runningInstaller()
{
    const std::string dir = lockDir();
    if (dir.empty()) {
        return 0;
    }
    const std::string lock = dir + "/update-in-progress";
    FILE* f = std::fopen(lock.c_str(), "r");
    if (!f) {
        return 0;   // the usual case: no update being installed
    }
    std::fclose(f);

    long written = 0;
    const long pid = readPidFile(lock, &written);
    const long now = long(std::time(nullptr));
    const bool fresh = written > 0 && now - written >= -60 && now - written < MAX_AGE_SECONDS;
    const bool installer = pid > 1 && isRunning(pid) && commandOf(pid).find("starscore-update-") != std::string::npos;
    if (!fresh || !installer) {
        std::remove(lock.c_str());   // left behind by a script that didn't finish: never let it block StarScore
        return 0;
    }
    return pid;
}

//! Shows the "Installation in progress" window. It closes by itself when the install script ends (the script then
//! opens StarScore), or after 15 minutes at most. The window the script itself showed is closed, so there's one.
inline void showInstallingWindow(long installerPid)
{
    const std::string dir = lockDir();
    const long scriptWindow = readPidFile(dir + "/update-window");
    if (scriptWindow > 1 && isRunning(scriptWindow) && commandOf(scriptWindow).find("osascript") != std::string::npos) {
        ::kill(pid_t(scriptWindow), SIGTERM);
    }
    const std::string pid = std::to_string(installerPid);
    const std::string cmd
        = "( /usr/bin/osascript -e 'activate' -e 'display dialog \"Installation in progress.\" & return & return & "
          "\"StarScore Studio will open by itself once the installation is finished (usually within a minute).\" "
          "with title \"Installing StarScore Studio\" buttons {\"OK\"} default button \"OK\" with icon note "
          "giving up after 900' >/dev/null 2>&1 & d=$!; "
          "while kill -0 " + pid + " 2>/dev/null && kill -0 $d 2>/dev/null; do sleep 0.5; done; "
                                   "kill $d 2>/dev/null ) </dev/null >/dev/null 2>&1 &";
    const int rc = std::system(cmd.c_str());
    (void)rc;
}

//! Call first thing in main(): true when an update is being installed (the window is then showing and StarScore
//! should quit without starting).
inline bool installInProgress()
{
    const long pid = runningInstaller();
    if (pid == 0) {
        return false;
    }
    showInstallingWindow(pid);
    return true;
}
}
