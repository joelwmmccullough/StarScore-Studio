/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Update all sheets
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "project/istarscoreservice.h"

namespace mu::project {
//! The songs whose sheets were made before the current sheet format, and the run that updates them (Joel, 6 Oct 2026)
class UpdateAllSheetsModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList songs READ songs NOTIFY loaded)
    Q_PROPERTY(int sheetCount READ sheetCount NOTIFY loaded)

    QML_ELEMENT

public:
    explicit UpdateAllSheetsModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
    //! running, finished, songIndex, songCount, song, phase, results (the window polls it while running)
    Q_INVOKABLE QVariantMap status() const;

    QVariantList songs() const;
    int sheetCount() const;

signals:
    void loaded();

private:
    muse::ContextInject<IStarScoreService> starScore = { this };
    std::vector<StarScoreOutdatedSong> m_songs;
};
}
