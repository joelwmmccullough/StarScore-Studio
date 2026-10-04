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
#include "updateconfiguration.h"

#include <QNetworkRequest>

#include "global/configreader.h"

#include "settings.h"

#include "app_config.h"

#include "starscoregithubrelease.h"

// StarScore: the running StarScore version (version.cmake); appshell, project and imagesexport read it the same way.
#ifndef STARSCORE_VERSION_STR
#define STARSCORE_VERSION_STR "1.0.0"
#endif

using namespace muse;
using namespace muse::update;

static const std::string module_name("update");

static const Settings::Key CHECK_FOR_UPDATE_KEY(module_name, "application/checkForUpdate");
static const Settings::Key CHECK_FOR_UPDATE_TEST_MODE_KEY(module_name, "application/checkForUpdateTestMode");
static const Settings::Key ALLOW_UPDATE_ON_PRERELEASE(module_name, "application/allowUpdateOnPreRelease");
static const Settings::Key SKIPPED_VERSION_KEY(module_name, "application/skippedVersion");

static const std::string PRIVACY_POLICY_URL_PATH("/about/desktop-privacy-policy");

void UpdateConfiguration::init()
{
    m_config = ConfigReader::read(":/configs/update.cfg");

    settings()->setDefaultValue(CHECK_FOR_UPDATE_KEY, Val(isAppUpdatable()));
    settings()->valueChanged(CHECK_FOR_UPDATE_KEY).onReceive(this, [this](const Val&) {
        m_needCheckForUpdateChanged.notify();
    });

    settings()->setDefaultValue(CHECK_FOR_UPDATE_TEST_MODE_KEY, Val(false));

    bool allowUpdateOnPreRelease = false;
#ifdef MUSESCORE_ALLOW_UPDATE_ON_PRERELEASE
    allowUpdateOnPreRelease = true;
#else
    allowUpdateOnPreRelease = false;
#endif
    settings()->setDefaultValue(ALLOW_UPDATE_ON_PRERELEASE, Val(allowUpdateOnPreRelease));
}

bool UpdateConfiguration::isAppUpdatable() const
{
    // StarScore: builds are published for macOS only (the Linux build is a headless test build), so the Help menu
    // item, the preference and the startup check exist on macOS alone.
#ifdef Q_OS_MACOS
    return true;
#else
    return false;
#endif
}

bool UpdateConfiguration::allowUpdateOnPreRelease() const
{
    return settings()->value(ALLOW_UPDATE_ON_PRERELEASE).toBool();
}

void UpdateConfiguration::setAllowUpdateOnPreRelease(bool allow)
{
    settings()->setSharedValue(ALLOW_UPDATE_ON_PRERELEASE, Val(allow));
}

bool UpdateConfiguration::needCheckForUpdate() const
{
    return settings()->value(CHECK_FOR_UPDATE_KEY).toBool();
}

void UpdateConfiguration::setNeedCheckForUpdate(bool needCheck)
{
    settings()->setSharedValue(CHECK_FOR_UPDATE_KEY, Val(needCheck));
}

async::Notification UpdateConfiguration::needCheckForUpdateChanged() const
{
    return m_needCheckForUpdateChanged;
}

std::string UpdateConfiguration::skippedReleaseVersion() const
{
    return settings()->value(SKIPPED_VERSION_KEY).toString();
}

void UpdateConfiguration::setSkippedReleaseVersion(const std::string& version)
{
    settings()->setSharedValue(SKIPPED_VERSION_KEY, Val(version));
}

bool UpdateConfiguration::checkForUpdateTestMode() const
{
    return settings()->value(CHECK_FOR_UPDATE_TEST_MODE_KEY).toBool();
}

std::string UpdateConfiguration::checkForAppUpdateUrl() const
{
    // StarScore: updates come from the fork's GitHub releases. MuseScore's feed URLs are still read from
    // update.cfg into m_config (and AppUpdateService still knows how to parse that feed), but they are not used:
    // offering a MuseScore Studio installer to a StarScore Studio user would be wrong.
    return STARSCORE_RELEASES_API_URL;
}

std::string UpdateConfiguration::previousAppReleasesNotesUrl() const
{
    return !allowUpdateOnPreRelease()
           ? m_config.value("all").toString()
           : m_config.value("all.test").toString();
}

muse::network::RequestHeaders UpdateConfiguration::updateHeaders() const
{
    // StarScore: the GitHub API refuses requests without a User-Agent and asks for these two headers.
    // MuseScore's default headers (its own user agent) are not sent to GitHub.
    muse::network::RequestHeaders headers;
    headers.knownHeaders[QNetworkRequest::UserAgentHeader] = QString("StarScore-Studio/" STARSCORE_VERSION_STR);
    headers.rawHeaders["Accept"] = "application/vnd.github+json";
    headers.rawHeaders["X-GitHub-Api-Version"] = "2022-11-28";
    return headers;
}

std::string UpdateConfiguration::museScoreUrl() const
{
    return globalConfiguration()->museScoreUrl();
}

std::string UpdateConfiguration::museScorePrivacyPolicyUrl() const
{
    return globalConfiguration()->museScoreUrl() + PRIVACY_POLICY_URL_PATH;
}

muse::io::path_t UpdateConfiguration::updateDataPath() const
{
    // StarScore: the dmg is saved to the user's Downloads folder on every platform (the owner keeps his dmgs there),
    // not to a private folder under the app data. Because this is a folder full of the user's files, nothing may
    // ever delete it (MuseScore's AppUpdateService::clear() used to wipe updateDataPath on macOS and Windows).
    return globalConfiguration()->downloadsPath();
}

muse::io::path_t UpdateConfiguration::updateRequestHistoryJsonPath() const
{
    return globalConfiguration()->userAppDataPath() + "/update_request_history.json";
}
