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

static QString agoText(const QDateTime& t)
{
    const qint64 mins = t.secsTo(QDateTime::currentDateTime()) / 60;
    if (mins < 60) {
        return muse::qtrc("starscore", "%n minute(s) ago", nullptr, int(std::max<qint64>(mins, 0)));
    }
    if (mins < 48 * 60) {
        return muse::qtrc("starscore", "%n hour(s) ago", nullptr, int(mins / 60));
    }
    return muse::qtrc("starscore", "%n day(s) ago", nullptr, int(mins / (24 * 60)));
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
            { "info", muse::qtrc("starscore", "%n version(s)", nullptr, int(versions.size())) + " · " + sizeText(aa::archivedBytes(s)) } };
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
            { "label", locale.toString(v.time, "ddd d MMM yyyy, h:mm ap") },
            { "ago", agoText(v.time) },
            { "size", v.waiting ? muse::qtrc("starscore", "waiting to be packed") : sizeText(v.storedBytes) },
        };
    }
    emit currentSongChanged();
}

bool AutosaveArchiveModel::openVersion(const QString& time)
{
    QString error;
    const QString path = aa::extractVersion(m_currentSong, QDateTime::fromString(time, Qt::ISODate), QString(), &error);
    if (path.isEmpty()) {
        interactive()->error(muse::trc("starscore", "Autosave archive"), error.toStdString());
        return false;
    }
    dispatcher()->dispatch("file-open", muse::actions::ActionData::make_arg1<QUrl>(QUrl::fromLocalFile(path)));
    return true;
}

void AutosaveArchiveModel::saveAllVersions()
{
    const QString folder = aa::autosaveArchiveRoot() + "/Unpacked/" + m_currentSong;
    const int n = aa::extractAllVersions(m_currentSong, folder);
    if (n > 0) {
        interactive()->revealInFileBrowser(muse::io::path_t(folder + "/" + QDir(folder).entryList({ "*.starscore" }, QDir::Files,
                                                                                                    QDir::Name).value(0)));
    }
}

void AutosaveArchiveModel::showArchiveFolder()
{
    QDir().mkpath(aa::autosaveArchiveRoot());
    interactive()->revealInFileBrowser(muse::io::path_t(aa::autosaveArchiveRoot()));
}
