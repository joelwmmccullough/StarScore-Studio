/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Update all sheets
 */
#include "updateallsheetsmodel.h"

#include <QDir>
#include <QFileInfo>

#include "translation.h"

using namespace mu::project;

UpdateAllSheetsModel::UpdateAllSheetsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void UpdateAllSheetsModel::load()
{
    if (m_mode == "styles") {
        // every .starscore in the library ("AMPL - Amplitudes.starscore": code and title from the file's name)
        m_songs.clear();
        const QString library = starScore()->auditLibraryFolder();
        for (const QString& path : library.isEmpty() ? QStringList() : starScore()->auditLibraryFiles(library)) {
            StarScoreOutdatedSong s;
            s.path = path;
            const QString base = QFileInfo(path).completeBaseName();
            s.code = base.contains(" - ") ? base.section(" - ", 0, 0) : QString();
            s.title = QFileInfo(path).absoluteDir().dirName();
            m_songs.push_back(s);
        }
    } else {
        m_songs = starScore()->outdatedSongs();
    }
    emit loaded();
}

void UpdateAllSheetsModel::start()
{
    QStringList paths;
    for (const StarScoreOutdatedSong& s : m_songs) {
        paths << s.path;
    }
    if (paths.isEmpty()) {
        return;
    }
    if (m_mode == "styles") {
        starScore()->startApplyStylesToAll(paths);
    } else {
        starScore()->startUpdateAllSheets(paths);
    }
}

void UpdateAllSheetsModel::cancel()
{
    starScore()->cancelUpdateAllSheets();
}

QVariantMap UpdateAllSheetsModel::status() const
{
    const StarScoreUpdateAllStatus s = starScore()->updateAllStatus();
    return { { "running", s.running }, { "finished", s.finished }, { "songIndex", s.songIndex }, { "songCount", s.songCount },
             { "song", s.song }, { "phase", s.phase }, { "results", s.results.join("\n") } };
}

QVariantList UpdateAllSheetsModel::songs() const
{
    QVariantList out;
    for (const StarScoreOutdatedSong& s : m_songs) {
        out << QVariantMap {
            { "title", s.title }, { "code", s.code }, { "sheets", s.finishedSheets },
            { "made", s.exportedWith.isEmpty() ? muse::qtrc("starscore", "an older StarScore")
              : muse::qtrc("starscore", "StarScore %1").arg(s.exportedWith) },
        };
    }
    return out;
}

int UpdateAllSheetsModel::sheetCount() const
{
    int n = 0;
    for (const StarScoreOutdatedSong& s : m_songs) {
        n += s.finishedSheets;
    }
    return n;
}
