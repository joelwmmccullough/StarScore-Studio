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

#include <gtest/gtest.h>

#include "update/internal/starscoregithubrelease.h"

using namespace muse::update;

// StarScore: the GitHub "list releases" shape as published by .github/workflows/starscore_macos.yml and
// starscore_linux.yml, cut down to the fields the parser reads. Newest first, as GitHub returns them.
static const char* SAMPLE_RELEASES = R"JSON([
  { "tag_name": "linux-23", "name": "StarScore Studio Linux test build 23", "draft": false, "prerelease": true,
    "body": "Linux test build", "assets": [ { "name": "StarScore-Studio-x86_64.AppImage", "size": 161106424,
    "browser_download_url": "https://github.com/joelwmmccullough/StarScore-Studio/releases/download/linux-23/StarScore-Studio-x86_64.AppImage" } ] },
  { "tag_name": "build-192", "name": "StarScore Studio 1.16.1", "draft": true, "prerelease": true,
    "body": "- draft, asset still uploading", "assets": [ { "name": "StarScore-Studio.dmg", "size": 1,
    "browser_download_url": "https://github.com/joelwmmccullough/StarScore-Studio/releases/download/build-192/StarScore-Studio.dmg" } ] },
  { "tag_name": "build-191", "name": "StarScore Studio 1.16.0", "draft": false, "prerelease": true,
    "body": "- **Checks GitHub for updates.**\n\n_Build 191, commit 1234567._", "assets": [ { "name": "StarScore-Studio.dmg", "size": 174129060,
    "browser_download_url": "https://github.com/joelwmmccullough/StarScore-Studio/releases/download/build-191/StarScore-Studio.dmg" } ] },
  { "tag_name": "build-190", "name": "StarScore Studio 1.16.0", "draft": false, "prerelease": true,
    "body": "- same version, older build", "assets": [ { "name": "StarScore-Studio.dmg", "size": 174100000,
    "browser_download_url": "https://github.com/joelwmmccullough/StarScore-Studio/releases/download/build-190/StarScore-Studio.dmg" } ] },
  { "tag_name": "build-189", "name": "StarScore Studio build 189", "draft": false, "prerelease": true,
    "body": "Built from 00dd0d7007bc1121d70b03ac202b11a377aef6ce", "assets": [ { "name": "StarScore-Studio.dmg", "size": 174129060,
    "browser_download_url": "https://github.com/joelwmmccullough/StarScore-Studio/releases/download/build-189/StarScore-Studio.dmg" } ] },
  { "tag_name": "build-188", "name": "StarScore Studio 1.15.9", "draft": false, "prerelease": true,
    "body": "- no dmg: packaging did not run", "assets": [] }
])JSON";

TEST(StarScoreGitHubReleaseTests, ParsesVersionFromReleaseName)
{
    QString version;
    EXPECT_TRUE(parseStarScoreReleaseVersion("StarScore Studio 1.16.0", version));
    EXPECT_EQ(version, "1.16.0");

    EXPECT_FALSE(parseStarScoreReleaseVersion("StarScore Studio build 189", version));
    EXPECT_TRUE(version.isEmpty());
}

TEST(StarScoreGitHubReleaseTests, ComparesVersionsNumerically)
{
    EXPECT_LT(compareStarScoreVersions("1.15.10", "1.16.0"), 0);
    EXPECT_GT(compareStarScoreVersions("1.15.10", "1.15.9"), 0); // not a string comparison
    EXPECT_EQ(compareStarScoreVersions("1.16.0", "1.16.0-beta"), 0);
    EXPECT_LT(compareStarScoreVersions("1.16", "1.16.1"), 0);
}

TEST(StarScoreGitHubReleaseTests, PicksHighestVersionedMacBuildWithDmg)
{
    QString error;
    const StarScoreGitHubRelease release = pickNewestStarScoreRelease(SAMPLE_RELEASES, &error);

    ASSERT_TRUE(release.isValid()) << error.toStdString();
    EXPECT_EQ(release.version, "1.16.0");
    EXPECT_EQ(release.tagName, "build-191");  // the draft 1.16.1 and the "build 189" name are skipped; 191 beats 190
    EXPECT_EQ(release.buildNumber, 191);
    EXPECT_EQ(release.assetSize, 174129060);
    EXPECT_EQ(release.assetUrl, "https://github.com/joelwmmccullough/StarScore-Studio/releases/download/build-191/StarScore-Studio.dmg");
    EXPECT_TRUE(release.notes.startsWith("- **Checks GitHub for updates.**"));
}

TEST(StarScoreGitHubReleaseTests, NothingToOfferWhenNoReleaseCarriesAVersion)
{
    QString error;
    const StarScoreGitHubRelease release = pickNewestStarScoreRelease(
        R"([{ "tag_name": "build-189", "name": "StarScore Studio build 189", "assets": [ { "name": "StarScore-Studio.dmg", "size": 1, "browser_download_url": "x" } ] }])",
        &error);
    EXPECT_FALSE(release.isValid());
    EXPECT_FALSE(error.isEmpty());
}

TEST(StarScoreGitHubReleaseTests, ReportsGitHubErrorObjects)
{
    QString error;
    const StarScoreGitHubRelease release = pickNewestStarScoreRelease(R"({ "message": "API rate limit exceeded" })", &error);
    EXPECT_FALSE(release.isValid());
    EXPECT_TRUE(error.contains("API rate limit exceeded"));

    const StarScoreGitHubRelease broken = pickNewestStarScoreRelease("not json", &error);
    EXPECT_FALSE(broken.isValid());
}
