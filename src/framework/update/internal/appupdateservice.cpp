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

#include "appupdateservice.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QJsonObject>
#include <QJsonValue>
#include <QJsonArray>
#include <QJsonDocument>

#include "update/updateerrors.h"

#include "async/async.h"
#include "defer.h"
#include "translation.h"
#include "log.h"

#include "starscoregithubrelease.h"

// StarScore: the running StarScore version (version.cmake), the one compared with the GitHub release name.
#ifndef STARSCORE_VERSION_STR
#define STARSCORE_VERSION_STR "1.0.0"
#endif

using namespace muse;
using namespace muse::update;
using namespace muse::network;
using namespace muse::async;
using namespace muse::io;

const QString INSTALLED_WEEK_BEGINNING_KEY("Installed-Week-Beginning");
const QString PREVIOUS_REQUEST_DAY_KEY("Previous-Request-Day");

static QDate calculateWeekBeginForDate(const QDate& date)
{
    // 1 (Monday) + 6 mod 7 = 0
    // 2 (Tuesday) + 6 mod 7 = 1
    // etc...
    int diffToWeekBegin = (date.dayOfWeek() + 6) % 7;
    return QDate::currentDate().addDays(-diffToWeekBegin);
}

static ISystemInfo::CpuArchitecture assetArch(const QString& asset)
{
    if (asset.contains("aarch64")) {
        return ISystemInfo::CpuArchitecture::Arm64;
    } else if (asset.contains("arm")) {
        return ISystemInfo::CpuArchitecture::Arm;
    }

    return ISystemInfo::CpuArchitecture::x86_64;
}

static PrevReleasesNotesList parsePreviousReleasesNotes(const QByteArray& json)
{
    PrevReleasesNotesList result;

    QJsonParseError err;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
        LOGE() << "failed parse, err: " << err.errorString();
        return result;
    }

    QJsonObject jsonObject = jsonDoc.object();

    if (jsonObject.empty()) {
        LOGE() << "failed parse, no jsonObject";
        return result;
    }

    QJsonArray releases = jsonObject.value("releases").toArray();
    for (const QJsonValue& release : releases) {
        QJsonObject releaseObj = release.toObject();
        std::string version = releaseObj.value("version").toString().toStdString();

        result.push_back({ std::move(version), releaseObj.value("notes").toString().toStdString() });
    }

    return result;
}

void AppUpdateService::init()
{
    TRACEFUNC;

    io::path_t historyPath = configuration()->updateRequestHistoryJsonPath();
    if (fileSystem()->exists(historyPath)) {
        return;
    }

    // If the request history file doesn't exist, perform a first time setup using today's date...
    UpdateRequestHistory updateRequestHistory;
    updateRequestHistory.installedWeekBeginning = calculateWeekBeginForDate(QDate::currentDate());
    updateRequestHistory.previousRequestDay = QDate::currentDate();

    Ret ret = writeUpdateRequestHistory(historyPath, updateRequestHistory);
    if (!ret) {
        LOGE() << ret.toString();
    }
}

Promise<RetVal<ReleaseInfo> > AppUpdateService::checkForUpdate()
{
    return Promise<RetVal<ReleaseInfo> >([this](auto resolve, auto) {
        clear();

        if (configuration()->checkForUpdateTestMode()) {
            return resolve(m_lastCheckResult);
        }

        if (!m_networkManager) {
            m_networkManager = networkManagerCreator()->makeNetworkManager();
        }

        // Read request history file...
        io::path_t historyPath = configuration()->updateRequestHistoryJsonPath();
        RetVal<UpdateRequestHistory> historyRv = readUpdateRequestHistory(historyPath);
        if (!historyRv.ret) {
            LOGE() << historyRv.ret.toString();
        }

        QUrl url = QString::fromStdString(configuration()->checkForAppUpdateUrl());
        RequestHeaders headers = prepareHeaders(historyRv.val);
        auto buff = std::make_shared<QBuffer>();

        RetVal<Progress> progress = m_networkManager->get(url, buff, headers);
        if (!progress.ret) {
            m_lastCheckResult.ret = progress.ret;
            return resolve(m_lastCheckResult);
        }

        progress.val.finished().onReceive(this, [this, historyPath, historyRv, buff, resolve](const ProgressResult& res) {
            if (!res.ret) {
                m_lastCheckResult.ret = res.ret;
                (void)resolve(m_lastCheckResult);
                return;
            }

            // Successfully performed request, update history file...
            UpdateRequestHistory updateRequestHistory = historyRv.val;
            updateRequestHistory.previousRequestDay = QDate::currentDate();
            Ret writeHistoryRet = writeUpdateRequestHistory(historyPath, updateRequestHistory);
            if (!writeHistoryRet) {
                LOGE() << writeHistoryRet.toString();
            }

            QByteArray json = buff->data();
            RetVal<ReleaseInfo> releaseInfo = parseRelease(json);

            if (!releaseInfo.ret) {
                m_lastCheckResult.ret = releaseInfo.ret;
                (void)resolve(m_lastCheckResult);
                return;
            }

            // StarScore: compares StarScore versions (1.15.10 vs 1.16.0), not MuseScore's 4.7.5, when the
            // release came from GitHub.
            Version current(currentAppVersion());
            Version update(releaseInfo.val.version);

            bool allowUpdateOnPreRelease = configuration()->allowUpdateOnPreRelease();
            bool isPreRelease = update.preRelease();

            if (!allowUpdateOnPreRelease && isPreRelease) {
                m_lastCheckResult.ret = make_ret(Err::NoUpdate);
                (void)resolve(m_lastCheckResult);
                return;
            }

            if (update <= current) {
                m_lastCheckResult.ret = make_ret(Err::NoUpdate);
                (void)resolve(m_lastCheckResult);
                return;
            }

            m_lastCheckResult = releaseInfo;

            if (isStarScoreGitHubSource()) {
                // StarScore: the release body already is the changelog block for this version; there is no
                // separate "all releases" feed to fetch, so previousReleasesNotes stays empty.
                (void)resolve(m_lastCheckResult);
                return;
            }

            downloadPreviousReleasesNotes(update, [this, resolve](const PrevReleasesNotesList& notes) {
                m_lastCheckResult.val.previousReleasesNotes = notes;
                (void)resolve(m_lastCheckResult);
            });
        });

        return Promise<RetVal<ReleaseInfo> >::dummy_result();
    });
}

const RetVal<ReleaseInfo>& AppUpdateService::lastCheckResult() const
{
    return m_lastCheckResult;
}

RetVal<Progress> AppUpdateService::downloadRelease()
{
    if (!m_networkManager) {
        m_networkManager = networkManagerCreator()->makeNetworkManager();
    }

    const ReleaseInfo info = m_lastCheckResult.val;

    if (isStarScoreGitHubSource()) {
        return downloadStarScoreRelease(info);
    }

    const QUrl fileUrl = QUrl::fromUserInput(QString::fromStdString(info.fileUrl));
    auto buff = std::make_shared<QBuffer>();

    RetVal<Progress> downloadProgress = m_networkManager->get(fileUrl, buff);
    if (!downloadProgress.ret) {
        return RetVal<Progress>::make_ret(downloadProgress.ret);
    }

    m_updateProgress.start();

    m_updateProgress.canceled().onNotify(this, [this, downloadProgress]() {
        Progress mutProgress = downloadProgress.val;
        mutProgress.cancel();
        m_updateProgress.canceled().disconnect(this);
    });

    downloadProgress.val.progressChanged().onReceive(this, [this](int64_t current, int64_t total, const std::string& msg) {
        m_updateProgress.progress(current, total, msg);
    });

    downloadProgress.val.finished().onReceive(this, [this, info, buff](const ProgressResult& res) {
        if (!res.ret) {
            m_updateProgress.finish(ProgressResult::make_ret(res.ret));
            return;
        }

        const path_t installerPath = configuration()->updateDataPath() + "/" + info.fileName;
        fileSystem()->makePath(muse::io::absoluteDirpath(installerPath));

        const Ret ret = fileSystem()->writeFile(installerPath, ByteArray::fromQByteArrayNoCopy(buff->data()));
        if (!ret) {
            m_updateProgress.finish(ProgressResult::make_ret(ret));
            return;
        }

        m_updateProgress.finish(ProgressResult::make_ok(Val(installerPath)));
    });

    return RetVal<Progress>::make_ok(m_updateProgress);
}

RequestHeaders AppUpdateService::prepareHeaders(const UpdateRequestHistory& history) const
{
    RequestHeaders headers = configuration()->updateHeaders();

    // StarScore: the "installed week" / "previous request day" headers are MuseScore's usage statistics;
    // they mean nothing to GitHub and are not sent there.
    if (isStarScoreGitHubSource()) {
        return headers;
    }

    if (history.isValid()) {
        std::string iwbString = history.installedWeekBeginning.toString(Qt::ISODate).toStdString();
        QByteArray iwbKey = QByteArray::fromStdString(INSTALLED_WEEK_BEGINNING_KEY.toStdString());
        headers.rawHeaders[iwbKey] = QByteArray::fromStdString(iwbString);

        std::string prdString = history.previousRequestDay.toString(Qt::ISODate).toStdString();
        QByteArray prdKey = QByteArray::fromStdString(PREVIOUS_REQUEST_DAY_KEY.toStdString());
        headers.rawHeaders[prdKey] = QByteArray::fromStdString(prdString);
    }

    return headers;
}

RetVal<AppUpdateService::UpdateRequestHistory> AppUpdateService::readUpdateRequestHistory(const io::path_t& path) const
{
    RetVal<ByteArray> rv = fileSystem()->readFile(path);
    if (!rv.ret) {
        return RetVal<UpdateRequestHistory>(rv.ret);
    }

    QJsonDocument historyDoc = QJsonDocument::fromJson(rv.val.toQByteArrayNoCopy());
    QJsonObject historyObject = historyDoc.object();

    UpdateRequestHistory updateRequestHistory;

    QString installedWeekBeginning = historyObject.value(INSTALLED_WEEK_BEGINNING_KEY).toString();
    updateRequestHistory.installedWeekBeginning = QDate::fromString(installedWeekBeginning, Qt::ISODate);

    QString previousRequestDay = historyObject.value(PREVIOUS_REQUEST_DAY_KEY).toString();
    updateRequestHistory.previousRequestDay = QDate::fromString(previousRequestDay, Qt::ISODate);

    return RetVal<UpdateRequestHistory>::make_ok(updateRequestHistory);
}

Ret AppUpdateService::writeUpdateRequestHistory(const io::path_t& path, const UpdateRequestHistory& updateRequestHistory)
{
    if (!updateRequestHistory.isValid()) {
        return make_ret(Ret::Code::UnknownError);
    }

    QJsonObject historyObject;

    const QDate& iwb = updateRequestHistory.installedWeekBeginning;
    const QDate& prd = updateRequestHistory.previousRequestDay;

    historyObject[INSTALLED_WEEK_BEGINNING_KEY] = iwb.toString(Qt::ISODate);
    historyObject[PREVIOUS_REQUEST_DAY_KEY] = prd.toString(Qt::ISODate);

    QByteArray byteArray = QJsonDocument(historyObject).toJson();

    return fileSystem()->writeFile(path, ByteArray::fromQByteArrayNoCopy(byteArray));
}

RetVal<ReleaseInfo> AppUpdateService::parseRelease(const QByteArray& json) const
{
    if (isStarScoreGitHubSource()) {
        return parseStarScoreRelease(json);
    }

    // MuseScore's own feed (updates.musescore.org): kept so the module still works against it, but unused by
    // StarScore Studio, see UpdateConfiguration::checkForAppUpdateUrl().
    QJsonParseError err;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !jsonDoc.isObject()) {
        return make_ret(Err::ReleaseInfoParseError, err.errorString().toStdString());
    }

    QJsonObject release = jsonDoc.object();
    QJsonObject assetObj = resolveReleaseAsset(release);
    if (assetObj.empty()) {
        return make_ret(Err::ReleaseInfoParseError, "No release asset");
    }

    QString tagName = release.value("tag_name").toString();
    QString version = tagName.replace("v", "");
    if (version.isEmpty()) {
        return make_ret(Err::ReleaseInfoParseError, "No release version");
    }

    RetVal<ReleaseInfo> result;
    result.ret = muse::make_ok();
    result.val.fileName = assetObj.value("name").toString().toStdString();
    result.val.fileUrl = assetObj.value("browser_download_url").toString().toStdString();
    result.val.version = version.toStdString();
    result.val.notes = release.value("bodyMarkdown").toString().toStdString();

    return result;
}

std::string AppUpdateService::platformFileSuffix() const
{
    switch (systemInfo()->productType()) {
    case ISystemInfo::ProductType::Windows: return "msi";
    case ISystemInfo::ProductType::MacOS: return "dmg";
    case ISystemInfo::ProductType::Linux: return "appimage";
    case ISystemInfo::ProductType::Unknown: break;
    }

    return "";
}

QJsonObject AppUpdateService::resolveReleaseAsset(const QJsonObject& release) const
{
    std::string fileSuffix = platformFileSuffix();
    ISystemInfo::ProductType productType = systemInfo()->productType();
    ISystemInfo::CpuArchitecture arch = systemInfo()->cpuArchitecture();

    QJsonArray assets = release.value("assets").toArray();
    for (const QJsonValue& asset : release.value("assetsNew").toArray()) {
        assets.push_back(asset);
    }

    for (const QJsonValue asset : assets) {
        QJsonObject assetObj = asset.toObject();

        QString name = assetObj.value("name").toString();
        if (io::suffix(name) != fileSuffix) {
            continue;
        }

        if (productType == ISystemInfo::ProductType::Linux) {
            if (arch != ISystemInfo::CpuArchitecture::Unknown && arch != assetArch(name)) {
                continue;
            }
        }

        return assetObj;
    }

    return QJsonObject();
}

void AppUpdateService::downloadPreviousReleasesNotes(const Version& updateVersion, const PrevReleaseNotesCallback& finished)
{
    QUrl url = QString::fromStdString(configuration()->previousAppReleasesNotesUrl());
    auto buff = std::make_shared<QBuffer>();
    RetVal<Progress> progress = m_networkManager->get(url, buff);

    if (!progress.ret) {
        LOGE() << "Failed to get previous release notes: " << progress.ret.toString();
        finished({});
        return;
    }

    progress.val.finished().onReceive(this, [this, updateVersion, buff, finished](const ProgressResult& res) {
        PrevReleasesNotesList result;

        DEFER {
            finished(result);
            return;
        };

        if (!res.ret) {
            LOGE() << "Failed to get previous release notes: " << res.ret.toString();
            return;
        }

        QByteArray json = buff->data();
        PrevReleasesNotesList previousReleasesNotes = parsePreviousReleasesNotes(json);
        if (previousReleasesNotes.empty()) {
            return;
        }

        Version currentVersion = Version(application()->fullVersion());

        for (const PrevReleaseNotes& releaseNotes : previousReleasesNotes) {
            Version previousVersion = Version(releaseNotes.version);
            if (updateVersion == previousVersion) {
                continue;
            }

            if (currentVersion < previousVersion) {
                String notesStr = String::fromStdString(releaseNotes.notes);
                if (notesStr.startsWith(u"###")) {
                    //! Release notes may be in the format of: ### MuseScore x.y.z is now available!\r\n...notes...\r\n
                    //! We need to remove the title of the release notes to get the actual notes.
                    static const std::regex titleRegex(R"(^###.*?\r\n)");
                    std::string notesWithoutTitle = notesStr.remove(titleRegex).toStdString();
                    result.emplace_back(releaseNotes.version, notesWithoutTitle);
                } else {
                    result.emplace_back(releaseNotes.version, releaseNotes.notes);
                }
            }
        }

        std::sort(result.begin(), result.end(), [](const PrevReleaseNotes& a,
                                                   const PrevReleaseNotes& b) {
            return Version(a.version) < Version(b.version);
        });
    });
}

void AppUpdateService::clear()
{
    m_lastCheckResult = RetVal<ReleaseInfo>::make_ok(ReleaseInfo());

    // StarScore: updateDataPath() is the user's Downloads folder now, so MuseScore's wipe of that folder on
    // macOS/Windows must not run (it would delete the user's downloads). The MuseScore feed path keeps it.
    if (isStarScoreGitHubSource()) {
        return;
    }

#if !defined(Q_OS_LINUX)
    fileSystem()->remove(configuration()->updateDataPath());
#endif
}

// ---------------------------------------------------------------------------------------------------------------
// StarScore: GitHub releases as the update source
// ---------------------------------------------------------------------------------------------------------------

bool AppUpdateService::isStarScoreGitHubSource() const
{
    const QUrl url(QString::fromStdString(configuration()->checkForAppUpdateUrl()));
    return url.host() == "api.github.com";
}

Version AppUpdateService::currentAppVersion() const
{
    return isStarScoreGitHubSource() ? Version(std::string(STARSCORE_VERSION_STR)) : application()->fullVersion();
}

RetVal<ReleaseInfo> AppUpdateService::parseStarScoreRelease(const QByteArray& json) const
{
    QString error;
    const StarScoreGitHubRelease release = pickNewestStarScoreRelease(json, &error);
    if (!release.isValid()) {
        // No comparable mac build at all (for example only "build N" releases without a version) is not an error
        // worth a dialog: the user simply has nothing newer to install.
        LOGI() << error;
        return make_ret(Err::NoUpdate);
    }

    RetVal<ReleaseInfo> result;
    result.ret = muse::make_ok();
    result.val.version = release.version.toStdString();
    result.val.fileName = QString("StarScore Studio %1.dmg").arg(release.version).toStdString();
    result.val.fileUrl = release.assetUrl.toStdString();
    // GitHub may hand the body back with Windows line endings; the dialog renders it as markdown.
    result.val.notes = QString(release.notes).replace("\r\n", "\n").toStdString();
    result.val.additionInfo["fileSize"] = Val(static_cast<int64_t>(release.assetSize));
    result.val.additionInfo["tagName"] = Val(release.tagName.toStdString());

    return result;
}

RetVal<Progress> AppUpdateService::downloadStarScoreRelease(const ReleaseInfo& info)
{
    const path_t downloadsDir = configuration()->updateDataPath();
    path_t installerPath = downloadsDir + "/" + info.fileName;

    const auto fileSizeIt = info.additionInfo.find("fileSize");
    const int64_t expectedSize = fileSizeIt != info.additionInfo.end() ? fileSizeIt->second.toInt64() : 0;

    m_updateProgress.start();

    // The dmg may already be in Downloads (the owner keeps them there, or a previous attempt got this far).
    // Reuse it when its size matches what GitHub reports. A same-named file of another size is somebody's file
    // (the rule here: never delete the user's files), so the download goes to "… (2).dmg" instead.
    const QFileInfo existing(installerPath.toQString());
    if (existing.exists() && expectedSize > 0 && existing.size() != expectedSize) {
        const QString base = QFileInfo(info.fileName.c_str()).completeBaseName();
        for (int i = 2;; ++i) {
            const path_t candidate = downloadsDir + "/" + QString("%1 (%2).dmg").arg(base).arg(i).toStdString();
            const QFileInfo c(candidate.toQString());
            if (!c.exists() || c.size() == expectedSize) {
                installerPath = candidate;
                break;
            }
        }
    }
    const QString installerQPath = installerPath.toQString();
    const QString partQPath = installerQPath + ".part";
    const QFileInfo chosen(installerQPath);
    if (chosen.exists() && expectedSize > 0 && chosen.size() == expectedSize) {
        LOGI() << "using the already downloaded " << installerPath;
        // The caller connects to finished() only after this returns, so finish on the next event loop turn.
        async::Async::call(this, [this, installerPath]() {
            m_updateProgress.finish(ProgressResult::make_ok(Val(installerPath)));
        });
        return RetVal<Progress>::make_ok(m_updateProgress);
    }

    fileSystem()->makePath(downloadsDir);

    // Stream straight into "<name>.dmg.part" instead of holding the whole 170 MB in memory; the finished file is
    // renamed into place, so a half-downloaded dmg never sits in Downloads under the real name.
    auto partFile = std::make_shared<QFile>(partQPath);
    const QUrl fileUrl = QUrl::fromUserInput(QString::fromStdString(info.fileUrl));

    // GitHub redirects browser_download_url to its object store; the network manager follows that. Only the
    // User-Agent goes along: the API-specific Accept header is for api.github.com, not for the file server.
    RequestHeaders downloadHeaders;
    downloadHeaders.knownHeaders = configuration()->updateHeaders().knownHeaders;

    RetVal<Progress> downloadProgress = m_networkManager->get(fileUrl, partFile, downloadHeaders);
    if (!downloadProgress.ret) {
        m_updateProgress.finish(ProgressResult::make_ret(downloadProgress.ret));
        return RetVal<Progress>::make_ret(downloadProgress.ret);
    }

    m_updateProgress.canceled().onNotify(this, [this, downloadProgress]() {
        Progress mutProgress = downloadProgress.val;
        mutProgress.cancel();
        m_updateProgress.canceled().disconnect(this);
    });

    downloadProgress.val.progressChanged().onReceive(this, [this](int64_t current, int64_t total, const std::string& msg) {
        m_updateProgress.progress(current, total, msg);
    });

    downloadProgress.val.finished().onReceive(this, [this, installerPath, installerQPath, partQPath, partFile,
                                                     expectedSize](const ProgressResult& res) {
        partFile->close();

        if (!res.ret) {
            QFile::remove(partQPath);
            m_updateProgress.finish(ProgressResult::make_ret(res.ret));
            return;
        }

        const int64_t downloadedSize = QFileInfo(partQPath).size();
        if (expectedSize > 0 && downloadedSize != expectedSize) {
            QFile::remove(partQPath);
            LOGE() << "downloaded " << downloadedSize << " bytes, GitHub reports " << expectedSize;
            m_updateProgress.finish(ProgressResult::make_ret(make_ret(Err::NetworkError, "Incomplete download")));
            return;
        }

        if (!QFile::rename(partQPath, installerQPath)) {
            QFile::remove(partQPath);
            m_updateProgress.finish(ProgressResult::make_ret(make_ret(Err::UnknownError,
                                                                      "Could not write " + installerPath.toStdString())));
            return;
        }

        m_updateProgress.finish(ProgressResult::make_ok(Val(installerPath)));
    });

    return RetVal<Progress>::make_ok(m_updateProgress);
}
