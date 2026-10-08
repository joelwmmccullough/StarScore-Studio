/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Autosave archive (Joel, 7 Oct 2026): before a save replaces a .starscore file, the version on disk is copied to
 * ~/StarScore Studio/Autosave Archive/<file name>/<file name> yyyy-MM-dd HHmm.starscore, at most once every 10
 * minutes per song and only when it differs from the newest copy there. Each copy is a whole song that opens on its
 * own.
 *
 * The folder is on the Mac, not in Google Drive: no upload every 10 minutes, no Drive storage used. Copying a file
 * takes no noticeable time, so saving doesn't slow down; nothing happens while StarScore is idle or a song isn't saved.
 *
 * Thinning, run after each new copy (newest copy kept per period):
 *   less than 2 hours old    every copy (one per 10 minutes)
 *   less than 1 day old      one per hour
 *   less than 30 days old    one per day
 *   less than 26 weeks old   one per week
 *   older                    one per month, kept for good
 * Only copies in that folder named this way are ever removed; nothing else is touched.
 */
#include "starscoreautoarchive.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSettings>

#include <map>
#include <set>

#include "log.h"

namespace mu::project::starscore {
static const char* TIME_FORMAT = "yyyy-MM-dd HHmm";

QString autosaveArchiveRoot()
{
    return QDir::homePath() + "/StarScore Studio/Autosave Archive";
}

static QByteArray fileHash(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return h.result();
}

//! The copies in a song's archive folder, newest first, with their times (from their names)
static std::vector<std::pair<QDateTime, QString> > archivedCopies(const QString& dir, const QString& base)
{
    static const QRegularExpression stamp(" (\\d{4}-\\d\\d-\\d\\d \\d{4})\\.starscore$");
    std::vector<std::pair<QDateTime, QString> > copies;
    for (const QFileInfo& fi : QDir(dir).entryInfoList({ "*.starscore" }, QDir::Files)) {
        const QString name = fi.fileName();
        const QRegularExpressionMatch m = stamp.match(name);
        if (!m.hasMatch() || name.left(m.capturedStart()) != base) {
            continue;
        }
        const QDateTime t = QDateTime::fromString(m.captured(1), TIME_FORMAT);
        if (t.isValid()) {
            copies.emplace_back(t, fi.absoluteFilePath());
        }
    }
    std::sort(copies.begin(), copies.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    return copies;
}

//! Which period a copy of this age belongs to; the newest copy in each period is kept
static QString periodOf(const QDateTime& t, const QDateTime& now)
{
    const qint64 mins = t.secsTo(now) / 60;
    if (mins < 120) {
        return "all " + t.toString(TIME_FORMAT);
    }
    if (mins < 24 * 60) {
        return "hour " + t.toString("yyyy-MM-dd HH");
    }
    if (mins < 30 * 24 * 60) {
        return "day " + t.toString("yyyy-MM-dd");
    }
    if (mins < 26 * 7 * 24 * 60) {
        int year = 0;
        const int week = t.date().weekNumber(&year);
        return QString("week %1-%2").arg(year).arg(week);
    }
    return "month " + t.toString("yyyy-MM");
}

int thinAutosaveArchive(const QString& dir, const QString& base, const QDateTime& now)
{
    std::set<QString> periodsKept;
    int removed = 0;
    for (const auto& [t, path] : archivedCopies(dir, base)) {
        if (periodsKept.insert(periodOf(t, now)).second) {
            continue;   // the newest copy of its period
        }
        if (QFile::remove(path)) {
            ++removed;
        }
    }
    return removed;
}

bool autosaveArchiveEnabled()
{
    return QSettings().value("StarScore/autosaveArchive", true).toBool();
}

QString archiveBeforeSave(const QString& songPath, const QDateTime& now)
{
    if (!autosaveArchiveEnabled() || !songPath.endsWith(".starscore", Qt::CaseInsensitive)) {
        return QString();
    }
    const QFileInfo song(songPath);
    if (!song.exists() || song.size() == 0) {
        return QString();
    }
    const QString root = autosaveArchiveRoot();
    if (song.absoluteFilePath().startsWith(root + "/")) {
        return QString();   // an archived copy opened and saved: not archived again
    }

    const QString base = song.completeBaseName();
    const QString dir = root + "/" + base;
    const auto copies = archivedCopies(dir, base);
    if (!copies.empty()) {
        if (copies.front().first.secsTo(now) < 10 * 60) {
            return QString();   // one copy per 10 minutes at most
        }
        if (QFileInfo(copies.front().second).size() == song.size() && fileHash(copies.front().second) == fileHash(songPath)) {
            return QString();   // nothing changed since the newest copy
        }
    }

    if (!QDir().mkpath(dir)) {
        LOGW() << "autosave archive: couldn't make " << dir.toStdString();
        return QString();
    }
    const QString target = dir + "/" + base + " " + now.toString(TIME_FORMAT) + ".starscore";
    if (QFile::exists(target) || !QFile::copy(songPath, target)) {
        LOGW() << "autosave archive: couldn't copy to " << target.toStdString();
        return QString();
    }
    const int removed = thinAutosaveArchive(dir, base, now);
    LOGI() << "autosave archive: " << target.toStdString() << " (" << removed << " older copies thinned)";
    return target;
}
}
