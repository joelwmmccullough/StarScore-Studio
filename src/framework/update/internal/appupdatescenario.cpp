/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
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

#include "appupdatescenario.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>

#include "updateerrors.h"

#include "types/val.h"
#include "translation.h"
#include "defer.h"
#include "log.h"

#include "starscoregithubrelease.h"
#ifdef Q_OS_MACOS
#include "starscore_macos_install_script.h" // generated from internal/starscore_macos_install.sh, see CMakeLists.txt
#endif

using namespace muse;
using namespace muse::update;
using namespace muse::actions;
using namespace muse::async;

bool AppUpdateScenario::needCheckForUpdate() const
{
    // StarScore: no automatic check where there is nothing to update to (Linux test builds), and never in an
    // automatic test run (STARSCORE_AUTOTEST, see starscoreautotest.cpp), which would hit the GitHub API on
    // every run and could pop a dialog into a headless session.
    if (!configuration()->isAppUpdatable()) {
        return false;
    }

    if (qEnvironmentVariableIsSet("STARSCORE_AUTOTEST")) {
        return false;
    }

    return configuration()->needCheckForUpdate();
}

void AppUpdateScenario::checkForUpdate(bool manual)
{
    if (m_checkInProgress) {
        return;
    }

    m_checkInProgress = true;
    m_checkInProgressChanged.notify();

    service()->checkForUpdate().onResolve(this, [this, manual](const RetVal<ReleaseInfo>& res) {
        const bool noUpdate = res.ret.code() == static_cast<int>(Err::NoUpdate);

        if (manual) {
            if (noUpdate) {
                showNoUpdateMsg();
            } else if (!res.ret) {
                showServerErrorMsg();
            } else {
                showReleaseInfo(res.val);
            }
        } else if (!noUpdate) {
            LOGE() << res.ret.toString();
        }

        m_checkInProgress = false;
        m_checkInProgressChanged.notify();
    });
}

bool AppUpdateScenario::checkInProgress() const
{
    return m_checkInProgress;
}

async::Notification AppUpdateScenario::checkInProgressChanged() const
{
    return m_checkInProgressChanged;
}

bool AppUpdateScenario::hasUpdate() const
{
    if (m_checkInProgress) {
        return false;
    }

    const RetVal<ReleaseInfo>& lastCheckResult = service()->lastCheckResult();
    if (!lastCheckResult.ret) {
        return false;
    }

    if (lastCheckResult.ret.code() == static_cast<int>(Err::NoUpdate)) {
        return false;
    }

    return !shouldIgnoreUpdate(lastCheckResult.val);
}

Promise<Ret> AppUpdateScenario::showUpdate()
{
    const RetVal<ReleaseInfo>& lastCheckResult = service()->lastCheckResult();
    if (lastCheckResult.ret) {
        return showReleaseInfo(lastCheckResult.val);
    }
    return async::make_promise<Ret>([lastCheckResult](auto resolve, auto) {
        return resolve(lastCheckResult.ret);
    });
}

Promise<Ret> AppUpdateScenario::processUpdateError(int errorCode)
{
    const auto unknownError = async::make_promise<Ret>([](auto resolve, auto) {
        return resolve(muse::make_ret(Ret::Code::UnknownError));
    });

    IF_ASSERT_FAILED(errorCode >= static_cast<int>(Ret::Code::UpdateFirst)
                     && errorCode <= static_cast<int>(Ret::Code::UpdateLast)) {
        return unknownError;
    }

    const Err error = static_cast<Err>(errorCode);
    IF_ASSERT_FAILED(error != Err::NoError) {
        return unknownError;
    }

    auto message = error == Err::NoUpdate ? showNoUpdateMsg() : showServerErrorMsg();
    return message.then<Ret>(this, [errorCode](const IInteractive::Result&, auto resolve) {
        const Ret::Code code = static_cast<Ret::Code>(errorCode);
        return resolve(muse::make_ret(code));
    });
}

Promise<IInteractive::Result> AppUpdateScenario::showNoUpdateMsg()
{
    // StarScore: the message names StarScore Studio and links to the fork's releases page.
    const QString str = muse::qtrc("update", "You already have the latest version of StarScore Studio. "
                                             "All builds are listed on <a href=\"%1\">GitHub</a>.")
                        .arg(QString::fromLatin1(STARSCORE_RELEASES_PAGE_URL));

    const IInteractive::Text text(str.toStdString(), IInteractive::TextFormat::RichText);
    const IInteractive::ButtonData okBtn = interactive()->buttonData(IInteractive::Button::Ok);

    return interactive()->info(muse::trc("update", "You’re up to date!"), text, { okBtn }, okBtn.btn,
                               IInteractive::Option::WithIcon);
}

Promise<Ret> AppUpdateScenario::showReleaseInfo(const ReleaseInfo& info)
{
    UriQuery query("muse://update/appreleaseinfo");
    query.addParam("version", Val(info.version)); // StarScore: shown in the dialog's title
    query.addParam("notes", Val(info.notes));
    query.addParam("previousReleasesNotes", Val(releasesNotesToValList(info.previousReleasesNotes)));

    return interactive()->open(query).then<Ret>(this, [this, info](const Val& val, auto resolve) {
        const QString actionCode = val.toQString();
        if (actionCode == "remindLater") {
            return resolve(muse::make_ret(Ret::Code::Cancel));
        }

        if (actionCode == "skip") {
            configuration()->setSkippedReleaseVersion(info.version);
            return resolve(muse::make_ret(Ret::Code::Cancel));
        }

        //! NOTE: In test mode we skip the progress dialog and jump straight to the "needs to close" dialog...
        const bool testMode = configuration()->checkForUpdateTestMode();
        auto promise = testMode ? askToCloseAppAndCompleteInstall(/*installerPath*/ String()) : downloadRelease();
        promise.onResolve(this, [resolve](const Ret& ret) {
            (void)resolve(ret);
        });

        return Promise<Ret>::dummy_result();
    });
}

Promise<IInteractive::Result> AppUpdateScenario::showServerErrorMsg()
{
    return interactive()->error(muse::trc("update", "Cannot connect to server"),
                                muse::trc("update", "Sorry - please try again later"));
}

Promise<Ret> AppUpdateScenario::downloadRelease()
{
    RetVal<Val> rv = interactive()->openSync("muse://update/app?mode=download");
    if (!rv.ret) {
        return processUpdateError(rv.ret.code());
    }
    return askToCloseAppAndCompleteInstall(rv.val.toString());
}

Promise<Ret> AppUpdateScenario::askToCloseAppAndCompleteInstall(const io::path_t& installerPath)
{
    // StarScore: on macOS the installed app is replaced from the dmg once the app has quit, and relaunched.
    const std::string info = muse::trc("update", "StarScore Studio needs to close to complete the installation. "
                                                 "If you have any unsaved changes, you will be prompted to save them before StarScore Studio closes. "
                                                 "The new version opens by itself when the installation is done.");
    const int closeBtn = int(IInteractive::Button::CustomButton) + 1;
    const IInteractive::ButtonDatas buttons = {
        interactive()->buttonData(IInteractive::Button::Cancel),
        IInteractive::ButtonData(closeBtn, muse::trc("update", "Close"), true)
    };

    return interactive()->info("", info, buttons, closeBtn)
           .then<Ret>(this, [this, installerPath](const IInteractive::Result& res, auto resolve) {
        if (res.isButton(IInteractive::Button::Cancel)) {
            return resolve(muse::make_ret(Ret::Code::Cancel));
        }

        if (multiwindowsProvider()->windowCount() != 1) {
            multiwindowsProvider()->quitAllAndRunInstallation(installerPath);
        }

        dispatcher()->dispatch("quit", ActionData::make_arg2<bool, std::string>(false, installerPath.toStdString()));
        return resolve(muse::make_ok());
    });
}

bool AppUpdateScenario::shouldIgnoreUpdate(const ReleaseInfo& info) const
{
    return info.version == configuration()->skippedReleaseVersion() && !configuration()->checkForUpdateTestMode();
}

// StarScore: complete the installation after quitting.
//
// MuseScore's updater only opens the downloaded dmg on macOS, which mounts it and leaves the user to drag the app
// over the old one. StarScore Studio instead writes the embedded install script to a temporary file and starts it
// detached, passing the dmg, the running app bundle and our pid; the script waits for the pid to exit, copies the
// app out of the dmg over the installed bundle (only inside /Applications or ~/Applications), removes the
// quarantine flag and opens the new app. Returns false when the script could not be started so the caller can
// fall back to opening the dmg.
bool AppUpdateScenario::startInstallerOnQuit(const io::path_t& installerPath)
{
#ifdef Q_OS_MACOS
    const QString dmgPath = installerPath.toQString();
    if (!dmgPath.endsWith(".dmg", Qt::CaseInsensitive) || !QFile::exists(dmgPath)) {
        LOGW() << "not a dmg, or missing: " << installerPath;
        return false;
    }

    // applicationDirPath() is "<bundle>.app/Contents/MacOS"; two levels up is the bundle.
    QDir bundleDir(QCoreApplication::applicationDirPath());
    if (!bundleDir.cdUp() || !bundleDir.cdUp()) {
        LOGW() << "could not find the app bundle from " << QCoreApplication::applicationDirPath();
        return false;
    }

    const QString bundlePath = bundleDir.absolutePath();
    if (!bundlePath.endsWith(".app", Qt::CaseInsensitive)) {
        LOGW() << "not running from an app bundle: " << bundlePath;
        return false;
    }

    const QString scriptPath = QDir::temp().filePath(QString("starscore-update-%1.sh").arg(QCoreApplication::applicationPid()));
    QFile script(scriptPath);
    if (!script.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        LOGW() << "could not write " << scriptPath;
        return false;
    }
    script.write(STARSCORE_MACOS_INSTALL_SCRIPT);
    script.close();
    script.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    const QStringList args = {
        scriptPath,
        dmgPath,
        bundlePath,
        QString::number(QCoreApplication::applicationPid())
    };

    qint64 pid = 0;
    if (!QProcess::startDetached("/bin/bash", args, QDir::homePath(), &pid)) {
        LOGE() << "could not start the install script " << scriptPath;
        QFile::remove(scriptPath);
        return false;
    }

    LOGI() << "install script started (pid " << pid << "): " << dmgPath << " -> " << bundlePath;
    return true;
#else
    UNUSED(installerPath);
    return false;
#endif
}
