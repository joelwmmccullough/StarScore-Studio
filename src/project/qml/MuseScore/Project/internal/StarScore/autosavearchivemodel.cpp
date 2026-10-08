/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "autosavearchivemodel.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QUrl>
#include <QPointer>
#include <QThreadPool>
#include <QCoreApplication>

#include "project/internal/starscore/starscoreautoarchive.h"
#include "translation.h"

using namespace mu::project;
namespace aa = mu::project::starscore;

static QString sizeText(qint64 bytes)
{
    if (bytes < 1024 * 1024) {
        return QString("%1 KB").arg(std::max<qint64>(1, bytes / 1024));
    }
    return QString("%1 MB").arg(double(bytes) / (1024.0 * 1024.0), 0, 'f', 1);
}

static QString countText(qint64 n, const char* one, const char* many)
{
    return QString("%1 %2").arg(n).arg(QString::fromUtf8(n == 1 ? one : many));
}

static QString agoText(const QDateTime& t)
{
    const qint64 mins = std::max<qint64>(0, t.secsTo(QDateTime::currentDateTime()) / 60);
    if (mins < 60) {
        return countText(mins, "minute ago", "minutes ago");
    }
    if (mins < 48 * 60) {
        return countText(mins / 60, "hour ago", "hours ago");
    }
    return countText(mins / (24 * 60), "day ago", "days ago");
}

AutosaveArchiveModel::AutosaveArchiveModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void AutosaveArchiveModel::load()
{
    m_songs.clear();
    const QStringList songs = aa::archivedSongs();
    for (const QString& s : songs) {
        const auto versions = aa::archivedVersions(s);
        m_songs << QVariantMap { { "name", s },
            { "info", countText(qint64(versions.size()), "version", "versions") + " · " + sizeText(aa::archivedBytes(s)) } };
    }
    emit songsChanged();

    QString pick = songs.value(0);
    if (INotationProjectPtr p = globalContext()->currentProject()) {
        const QString open = QFileInfo(p->path().toQString()).completeBaseName();
        if (songs.contains(open)) {
            pick = open;
        }
    }
    selectSong(pick);
}

void AutosaveArchiveModel::selectSong(const QString& song)
{
    m_currentSong = song;
    m_versions.clear();
    const QLocale locale;
    for (const aa::ArchivedVersion& v : aa::archivedVersions(song)) {
        m_versions << QVariantMap {
            { "time", v.time.toString(Qt::ISODate) },
            { "label", locale.toString(v.time, "d MMM yyyy, h:mm ap") },
            { "ago", agoText(v.time) },
            { "size", v.waiting ? muse::qtrc("starscore", "waiting to be packed") : sizeText(v.storedBytes) },
        };
    }
    emit currentSongChanged();
}

void AutosaveArchiveModel::setBusy(const QString& text)
{
    if (m_busyText != text) {
        m_busyText = text;
        emit busyChanged();
    }
}

// Unpacking a version of a big song takes a few seconds: done in the background, so the window doesn't freeze
void AutosaveArchiveModel::openVersion(const QString& time)
{
    if (!m_busyText.isEmpty()) {
        return;
    }
    setBusy(muse::qtrc("starscore", "Unpacking the version…"));
    const QString song = m_currentSong;
    QPointer<AutosaveArchiveModel> self(this);
    QThreadPool::globalInstance()->start([self, song, time]() {
        QString error;
        const QString path = aa::extractVersion(song, QDateTime::fromString(time, Qt::ISODate), QString(), &error);
        QMetaObject::invokeMethod(qApp, [self, path, error]() {
            if (!self) {
                return;
            }
            self->setBusy(QString());
            if (path.isEmpty()) {
                self->interactive()->error(muse::trc("starscore", "Autosave archive"), error.toStdString());
                return;
            }
            self->dispatcher()->dispatch("file-open", muse::actions::ActionData::make_arg1<QUrl>(QUrl::fromLocalFile(path)));
            emit self->versionOpened();
        }, Qt::QueuedConnection);
    });
}

void AutosaveArchiveModel::saveAllVersions()
{
    if (!m_busyText.isEmpty()) {
        return;
    }
    setBusy(muse::qtrc("starscore", "Unpacking every version…"));
    const QString song = m_currentSong;
    const QString folder = aa::autosaveArchiveRoot() + "/Unpacked/" + song;
    QPointer<AutosaveArchiveModel> self(this);
    QThreadPool::globalInstance()->start([self, song, folder]() {
        const int n = aa::extractAllVersions(song, folder);
        QMetaObject::invokeMethod(qApp, [self, n, folder]() {
            if (!self) {
                return;
            }
            self->setBusy(QString());
            if (n > 0) {
                self->interactive()->revealInFileBrowser(muse::io::path_t(folder + "/" + QDir(folder).entryList(
                                                                              { "*.starscore" }, QDir::Files, QDir::Name).value(0)));
            }
        }, Qt::QueuedConnection);
    });
}

void AutosaveArchiveModel::showArchiveFolder()
{
    QDir().mkpath(aa::autosaveArchiveRoot());
    interactive()->revealInFileBrowser(muse::io::path_t(aa::autosaveArchiveRoot()));
}
