/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "iinteractive.h"
#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"

namespace mu::project {
//! File › Autosave archive…: the songs with archived versions, and each song's versions, newest first
class AutosaveArchiveModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList songs READ songs NOTIFY songsChanged)
    Q_PROPERTY(QString currentSong READ currentSong NOTIFY currentSongChanged)
    Q_PROPERTY(QVariantList versions READ versions NOTIFY currentSongChanged)
    Q_PROPERTY(QString busyText READ busyText NOTIFY busyChanged)

    QML_ELEMENT

    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit AutosaveArchiveModel(QObject* parent = nullptr);

    QVariantList songs() const { return m_songs; }
    QString currentSong() const { return m_currentSong; }
    QVariantList versions() const { return m_versions; }
    //! what is being unpacked in the background, empty when nothing is
    QString busyText() const { return m_busyText; }

    Q_INVOKABLE void load();
    Q_INVOKABLE void selectSong(const QString& song);
    //! opens the version as its own file (Autosave Archive/Opened), unpacked in the background; versionOpened()
    //! when it opens, or a message saying why it can't
    Q_INVOKABLE void openVersion(const QString& time);
    //! every version of the current song as .starscore files, shown in Finder
    Q_INVOKABLE void saveAllVersions();
    Q_INVOKABLE void showArchiveFolder();

signals:
    void songsChanged();
    void currentSongChanged();
    void busyChanged();
    void versionOpened();

private:
    QVariantList m_songs;
    QString m_currentSong;
    QVariantList m_versions;
    QString m_busyText;
    void setBusy(const QString& text);
};
}
