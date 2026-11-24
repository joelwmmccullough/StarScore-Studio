/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * (1.37.0) Saving into a Google Drive folder safely (see starscoredrivesafety.h).
 */
#include "starscoredrivesafety.h"

#include "starscoreautoarchive.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

#include "log.h"

namespace mu::project::starscore {
static QByteArray sha1Of(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha1);
}

static QByteArray fileSha1(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return h.result();
}

bool replaceFileContents(const QString& src, const QString& dst, QString* error)
{
    auto fail = [error](const QString& why) {
        if (error) {
            *error = why;
        }
        LOGE() << "[starscore] " << why.toStdString();
        return false;
    };

    QFile in(src);
    if (!in.open(QIODevice::ReadOnly)) {
        return fail(QString("couldn't read %1: %2").arg(src, in.errorString()));
    }
    const QByteArray bytes = in.readAll();
    in.close();

    // ReadWrite: an existing file is opened as it is (not emptied first), written from the start and then cut to the
    // new length, so it's the same file all along
    QFile out(dst);
    if (!out.open(QIODevice::ReadWrite)) {
        return fail(QString("couldn't open %1 for writing: %2").arg(dst, out.errorString()));
    }
    if (out.write(bytes) != bytes.size()) {
        return fail(QString("couldn't write %1: %2").arg(dst, out.errorString()));
    }
    if (out.size() != bytes.size() && !out.resize(bytes.size())) {
        return fail(QString("couldn't set the length of %1: %2").arg(dst, out.errorString()));
    }
    out.flush();
#ifdef Q_OS_UNIX
    ::fsync(out.handle());
#endif
    out.close();

    if (fileSha1(dst) != sha1Of(bytes)) {
        return fail(QString("%1 doesn't read back as what was written").arg(dst));
    }
    return true;
}

QString localSavingPath(const QString& target)
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if (base.isEmpty()) {
        base = QDir::tempPath();
    }
    const QString dir = base + "/StarScore Saving";
    QDir().mkpath(dir);
    const QString key = QString::fromLatin1(QCryptographicHash::hash(target.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
    return dir + "/" + key + " " + QFileInfo(target).fileName() + "_saving";
}

QString lastSavedCopyPath(const QString& songPath)
{
    const QFileInfo fi(songPath);
    const QString folder = fi.absoluteDir().dirName();
    const QString suffix = fi.suffix().isEmpty() ? QString() : "." + fi.suffix();
    return autosaveArchiveRoot() + "/Last Saved/" + fi.completeBaseName() + " (" + folder + ")" + suffix;
}

static SaveWatchHooks& hooks()
{
    static SaveWatchHooks h;
    return h;
}

void setSaveWatchHooks(SaveWatchHooks h)
{
    hooks() = std::move(h);
}

static QString driveFsRoot()
{
    const QString test = qEnvironmentVariable("STARSCORE_DRIVEFS_DIR");
    if (!test.isEmpty()) {
        return test;
    }
    return QDir::homePath() + "/Library/Application Support/Google/DriveFS";
}

QStringList driveRemovedFolders()
{
    QStringList out;
    const QString root = driveFsRoot();
    if (!QFileInfo(root).isDir()) {
        return out;
    }
    auto isRemovedFolder = [](const QString& name) {
        return name.endsWith("canceled_uploads") || name == "lost_and_found";
    };
    // in DriveFS itself and in each account's folder (not deeper: the account folders hold the big content cache)
    const QFileInfoList top = QDir(root).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& d : top) {
        if (isRemovedFolder(d.fileName())) {
            out << d.absoluteFilePath();
            continue;
        }
        for (const QFileInfo& sub : QDir(d.absoluteFilePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            if (isRemovedFolder(sub.fileName())) {
                out << sub.absoluteFilePath();
            }
        }
    }
    return out;
}

std::vector<DriveRemovedCopy> driveRemovedCopies()
{
    std::vector<DriveRemovedCopy> out;
    for (const QString& folder : driveRemovedFolders()) {
        QDirIterator it(folder, { "*.starscore", "*.mscz" }, QDir::Files, QDirIterator::Subdirectories);
        int seen = 0;
        while (it.hasNext() && seen < 5000) {
            ++seen;
            const QFileInfo fi(it.next());
            out.push_back({ fi.absoluteFilePath(), fi.lastModified(), fi.size() });
        }
    }
    std::sort(out.begin(), out.end(), [](const DriveRemovedCopy& a, const DriveRemovedCopy& b) { return a.time > b.time; });
    return out;
}

static QString answerKey(const DriveRemovedCopy& c)
{
    return c.path + "|" + QString::number(c.time.toSecsSinceEpoch()) + "|" + QString::number(c.bytes);
}

static bool answered(const DriveRemovedCopy& c)
{
    return QSettings().value("StarScore/driveRemoved/answered").toStringList().contains(answerKey(c));
}

void markDriveCopyAnswered(const DriveRemovedCopy& c)
{
    QSettings s;
    QStringList list = s.value("StarScore/driveRemoved/answered").toStringList();
    if (!list.contains(answerKey(c))) {
        list << answerKey(c);
        while (list.size() > 500) {
            list.removeFirst();
        }
        s.setValue("StarScore/driveRemoved/answered", list);
    }
}

std::vector<DriveRemovedCopy> unansweredDriveRemovedCopies()
{
    std::vector<DriveRemovedCopy> out;
    for (const DriveRemovedCopy& c : driveRemovedCopies()) {
        if (!answered(c)) {
            out.push_back(c);
        }
    }
    return out;
}

std::optional<DriveRemovedCopy> driveRemovedCopyFor(const QString& songPath)
{
    const QFileInfo song(songPath);
    const QString name = song.fileName();
    for (const DriveRemovedCopy& c : driveRemovedCopies()) {   // newest first
        if (QFileInfo(c.path).fileName() != name || answered(c)) {
            continue;
        }
        if (song.exists()) {
            if (c.time <= song.lastModified().addSecs(2)) {
                continue;
            }
            if (fileSha1(c.path) == fileSha1(songPath)) {
                continue;
            }
        }
        return c;
    }
    return std::nullopt;
}

bool putBackDriveCopy(const DriveRemovedCopy& copy, const QString& songPath, QString* error)
{
    const QFileInfo song(songPath);
    if (song.exists()) {
        const QString dir = autosaveArchiveRoot() + "/Replaced";
        QDir().mkpath(dir);
        const QString suffix = song.suffix().isEmpty() ? QString() : "." + song.suffix();
        QString kept = dir + "/" + song.completeBaseName() + " (replaced "
                       + QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm") + ")" + suffix;
        for (int n = 2; QFileInfo::exists(kept); ++n) {
            kept = dir + "/" + song.completeBaseName() + " (replaced "
                   + QDateTime::currentDateTime().toString("yyyy-MM-dd HHmm") + " " + QString::number(n) + ")" + suffix;
        }
        if (!QFile::copy(songPath, kept)) {
            if (error) {
                *error = QString("The file there now couldn't be kept in %1, so nothing was changed.").arg(dir);
            }
            return false;
        }
        LOGI() << "[starscore] put back from Google Drive: the file there now kept as " << kept.toStdString();
    } else {
        QDir().mkpath(song.absolutePath());
    }
    if (!replaceFileContents(copy.path, songPath, error)) {
        return false;
    }
    markDriveCopyAnswered(copy);
    LOGI() << "[starscore] put back from Google Drive: " << copy.path.toStdString() << " -> " << songPath.toStdString();
    return true;
}

void checkSavedFile(const QString& songPath, const QDateTime& savedAt)
{
    if (QFileInfo::exists(songPath)) {
        return;
    }
    const QString copy = lastSavedCopyPath(songPath);
    const QFileInfo song(songPath);
    if (!QFileInfo::exists(copy) || !song.absoluteDir().exists()) {
        LOGW() << "[starscore] the saved file is gone and can't be written back: " << songPath.toStdString();
        return;
    }

    // Written back only when Google Drive took it (a copy of it in Drive's canceled uploads since the save) or when the
    // song open now is still saved there; not when the file was moved or renamed on purpose
    bool tookByDrive = false;
    QString driveCopy;
    for (const DriveRemovedCopy& c : driveRemovedCopies()) {
        if (QFileInfo(c.path).fileName() == song.fileName() && c.time >= savedAt.addSecs(-5)) {
            tookByDrive = true;
            driveCopy = c.path;
            break;
        }
    }
    const bool stillOpenThere = hooks().isOpenAt && hooks().isOpenAt(songPath);
    if (!tookByDrive && !stillOpenThere) {
        LOGI() << "[starscore] the saved file was moved or removed (not by Google Drive): " << songPath.toStdString();
        return;
    }

    QString error;
    const bool ok = replaceFileContents(copy, songPath, &error);
    LOGW() << "[starscore] the saved file disappeared after the save" << (tookByDrive ? " (Google Drive took it)" : "")
           << "; written back: " << (ok ? "yes" : "no") << " " << songPath.toStdString();
    if (ok && !driveCopy.isEmpty()) {
        // it's the same save: don't offer it back later
        for (const DriveRemovedCopy& c : driveRemovedCopies()) {
            if (c.path == driveCopy) {
                markDriveCopyAnswered(c);
            }
        }
    }
    if (hooks().tell) {
        const QString where = tookByDrive
                              ? QCoreApplication::translate("starscore", "Google Drive took “%1” out of its folder after you saved it "
                                                                         "(it moved it to its canceled uploads).").arg(song.fileName())
                              : QCoreApplication::translate("starscore", "“%1” disappeared from its folder after you saved it.")
                              .arg(song.fileName());
        const QString text = ok
                             ? where + "\n\n" + QCoreApplication::translate("starscore", "StarScore wrote the saved song back to "
                                                                                         "%1. Nothing was lost.").arg(song.absolutePath())
                             : where + "\n\n" + QCoreApplication::translate("starscore", "StarScore couldn't write it back (%1). "
                                                                                         "Your save is in %2 — save the song again "
                                                                                         "now (File › Save).").arg(error, copy);
        hooks().tell(QCoreApplication::translate("starscore", "The song file disappeared after saving"), text);
    }
}

void afterSave(const QString& songPath)
{
    const QDateTime savedAt = QDateTime::currentDateTime();
    const QString copy = lastSavedCopyPath(songPath);
    QDir().mkpath(QFileInfo(copy).absolutePath());
    const QString part = copy + ".part";
    QFile::remove(part);
    if (QFile::copy(songPath, part)) {
        QFile::remove(copy);
        QFile::rename(part, copy);
    } else {
        LOGW() << "[starscore] couldn't keep the Last Saved copy of " << songPath.toStdString();
    }

    if (!qApp) {
        return;
    }
    for (int ms : { 5000, 60000, 300000 }) {
        QTimer::singleShot(ms, qApp, [songPath, savedAt]() { checkSavedFile(songPath, savedAt); });
    }
}
}
