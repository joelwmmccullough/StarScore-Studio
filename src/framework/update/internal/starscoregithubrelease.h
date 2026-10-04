/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
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

#pragma once

#include <QByteArray>
#include <QString>

// StarScore: StarScore Studio's builds are published as GitHub releases of the fork
// (https://github.com/joelwmmccullough/StarScore-Studio/releases), not through MuseScore's update feed.
// This file knows the shape of those releases and nothing else: it depends on Qt only, so it can be
// compiled and checked on its own (see the comment at the bottom of starscoregithubrelease.cpp).
//
// A mac release looks like this:
//   tag_name: "build-190"           (the GitHub Actions run number; "linux-N" releases are Linux test builds and are ignored)
//   name:     "StarScore Studio 1.16.0"   (the version is STARSCORE_VERSION from version.cmake; older releases
//                                          were named "StarScore Studio build 190" and carry no version, so they are skipped)
//   body:     the "## 1.16.0" block of starscore_changelog.md, used as the release notes
//   assets:   exactly one, "StarScore-Studio.dmg", whose browser_download_url is what we download

namespace muse::update {
//! The GitHub REST endpoint listing the fork's releases, newest first. 30 per page because every push
//! also publishes a "linux-N" test build, and those can outnumber the mac builds between two [dmg] commits.
inline constexpr const char* STARSCORE_RELEASES_API_URL
    = "https://api.github.com/repos/joelwmmccullough/StarScore-Studio/releases?per_page=30";

//! The releases page for humans (linked from the "you're up to date" message).
inline constexpr const char* STARSCORE_RELEASES_PAGE_URL = "https://github.com/joelwmmccullough/StarScore-Studio/releases";

inline constexpr const char* STARSCORE_MAC_RELEASE_TAG_PREFIX = "build-";
inline constexpr const char* STARSCORE_MAC_ASSET_NAME = "StarScore-Studio.dmg";

struct StarScoreGitHubRelease {
    QString version;        // "1.16.0"
    QString tagName;        // "build-190"
    int buildNumber = 0;    // 190
    QString assetUrl;       // browser_download_url of StarScore-Studio.dmg
    qint64 assetSize = 0;   // bytes, as reported by GitHub (used to decide whether a dmg already in Downloads is complete)
    QString notes;          // release body, markdown

    bool isValid() const { return !version.isEmpty() && !assetUrl.isEmpty(); }
};

//! "StarScore Studio 1.16.0" -> "1.16.0". Returns false when the name holds no major.minor.patch version
//! (e.g. the older "StarScore Studio build 190" names).
bool parseStarScoreReleaseVersion(const QString& releaseName, QString& versionOut);

//! Compares two major.minor.patch strings; < 0, 0, > 0 like strcmp. Suffixes after '-' are ignored.
int compareStarScoreVersions(const QString& a, const QString& b);

//! Parses the JSON array returned by STARSCORE_RELEASES_API_URL and returns the release to offer:
//! the highest version among releases tagged "build-N" that carry a StarScore-Studio.dmg asset and a
//! version in their name (ties broken by build number). The result is invalid when nothing qualifies,
//! and `error` (if given) says why.
StarScoreGitHubRelease pickNewestStarScoreRelease(const QByteArray& json, QString* error = nullptr);
}
