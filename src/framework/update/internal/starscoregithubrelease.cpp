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

#include "starscoregithubrelease.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QRegularExpression>
#include <QStringList>

using namespace muse::update;

bool muse::update::parseStarScoreReleaseVersion(const QString& releaseName, QString& versionOut)
{
    // The version is the first "digits.digits.digits" in the name; anything else in the name is ignored so a
    // future "StarScore Studio 1.16.0 (beta)" would still be read. "build 190" has no dots and does not match.
    static const QRegularExpression versionRe(QStringLiteral("(\\d+)\\.(\\d+)\\.(\\d+)"));
    const QRegularExpressionMatch match = versionRe.match(releaseName);
    if (!match.hasMatch()) {
        versionOut.clear();
        return false;
    }

    versionOut = match.captured(0);
    return true;
}

int muse::update::compareStarScoreVersions(const QString& a, const QString& b)
{
    const auto components = [](const QString& version) {
        QList<int> result;
        const QString base = version.section(QLatin1Char('-'), 0, 0);
        for (const QString& part : base.split(QLatin1Char('.'))) {
            result << part.toInt();
        }
        while (result.size() < 3) {
            result << 0;
        }
        return result;
    };

    const QList<int> ca = components(a);
    const QList<int> cb = components(b);
    for (int i = 0; i < 3; ++i) {
        if (ca[i] != cb[i]) {
            return ca[i] < cb[i] ? -1 : 1;
        }
    }
    return 0;
}

StarScoreGitHubRelease muse::update::pickNewestStarScoreRelease(const QByteArray& json, QString* error)
{
    if (error) {
        error->clear();
    }

    const auto fail = [error](const QString& why) {
        if (error) {
            *error = why;
        }
        return StarScoreGitHubRelease();
    };

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        return fail(QStringLiteral("GitHub releases: %1").arg(parseError.errorString()));
    }

    if (!doc.isArray()) {
        // GitHub answers rate limits and bad requests with an object {"message": ...}; show that message.
        const QString message = doc.isObject() ? doc.object().value(QStringLiteral("message")).toString() : QString();
        return fail(message.isEmpty() ? QStringLiteral("GitHub releases: expected a JSON array")
                    : QStringLiteral("GitHub releases: %1").arg(message));
    }

    const QString tagPrefix = QString::fromLatin1(STARSCORE_MAC_RELEASE_TAG_PREFIX);
    const QString assetName = QString::fromLatin1(STARSCORE_MAC_ASSET_NAME);

    StarScoreGitHubRelease best;
    int candidates = 0;

    for (const QJsonValue& value : doc.array()) {
        const QJsonObject release = value.toObject();

        const QString tagName = release.value(QStringLiteral("tag_name")).toString();
        if (!tagName.startsWith(tagPrefix)) {
            continue; // "linux-N" test builds and anything else that is not a mac build
        }

        if (release.value(QStringLiteral("draft")).toBool(false)) {
            continue; // not published yet; its asset may still be uploading
        }

        QJsonObject dmgAsset;
        for (const QJsonValue& assetValue : release.value(QStringLiteral("assets")).toArray()) {
            const QJsonObject asset = assetValue.toObject();
            if (asset.value(QStringLiteral("name")).toString() == assetName) {
                dmgAsset = asset;
                break;
            }
        }
        if (dmgAsset.isEmpty()) {
            continue; // a mac build whose packaging step did not run
        }

        QString version;
        if (!parseStarScoreReleaseVersion(release.value(QStringLiteral("name")).toString(), version)) {
            continue; // older releases were named "StarScore Studio build N" and cannot be compared with anything
        }

        ++candidates;

        StarScoreGitHubRelease candidate;
        candidate.version = version;
        candidate.tagName = tagName;
        candidate.buildNumber = tagName.mid(tagPrefix.size()).toInt();
        candidate.assetUrl = dmgAsset.value(QStringLiteral("browser_download_url")).toString();
        candidate.assetSize = static_cast<qint64>(dmgAsset.value(QStringLiteral("size")).toDouble());
        candidate.notes = release.value(QStringLiteral("body")).toString();

        if (candidate.assetUrl.isEmpty()) {
            continue;
        }

        const int cmp = best.isValid() ? compareStarScoreVersions(candidate.version, best.version) : 1;
        if (cmp > 0 || (cmp == 0 && candidate.buildNumber > best.buildNumber)) {
            best = candidate;
        }
    }

    if (!best.isValid()) {
        return fail(candidates == 0
                    ? QStringLiteral("GitHub releases: no mac build with a version in its name")
                    : QStringLiteral("GitHub releases: no mac build with a download URL"));
    }

    return best;
}

// Unit tests: tests/starscoregithubrelease_tests.cpp (built with MUSE_ENABLE_UNIT_TESTS). The file depends on Qt
// only, so it can also be compiled on its own against Qt6Core with a small main that feeds the saved output of
// STARSCORE_RELEASES_API_URL into pickNewestStarScoreRelease().
