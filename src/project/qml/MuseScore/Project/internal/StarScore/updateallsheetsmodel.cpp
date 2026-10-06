/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Update all sheets
 */
#include "updateallsheetsmodel.h"

#include "translation.h"

using namespace mu::project;

UpdateAllSheetsModel::UpdateAllSheetsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void UpdateAllSheetsModel::load()
{
    m_songs = starScore()->outdatedSongs();
    emit loaded();
}

void UpdateAllSheetsModel::start()
{
    QStringList paths;
    for (const StarScoreOutdatedSong& s : m_songs) {
        paths << s.path;
    }
    if (!paths.isEmpty()) {
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
