/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Song page (1.28.0, Joel 5 Nov 2026: "every .starscore file, when opened, could lead me to a
 * sort of dashboard/hub page for that specific song, telling me all the different materials i've built for it so that
 * i can navigate where i want to, rather than going straight to editing the sheet music … an all-in-one song
 * management app that includes a full notation software section").
 *
 * Everything the open song has, in one place: its arrangements and part scores, its solos (or a solo
 * transcription's keys), the recordings and reference PDFs stored in it, Practice, the sheets exported from it (in
 * Sheets and Demos and beside the file), its to-do list and its history. Each item opens where it lives: the score
 * (the notation page) on that arrangement, part score or solo; the Reference PDF or Practice panel; a folder or file
 * in the Finder or its own app.
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"
#include "iinteractive.h"
#include "dockwindow/idockwindowprovider.h"
#include "project/istarscoreservice.h"

namespace mu::project {
class SongHubModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool hasSong READ hasSong NOTIFY changed)
    Q_PROPERTY(bool isStarScore READ isStarScore NOTIFY changed)
    Q_PROPERTY(bool isSoloFile READ isSoloFile NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString facts READ facts NOTIFY changed)
    Q_PROPERTY(QString filePath READ filePath NOTIFY changed)
    Q_PROPERTY(QString songFolder READ songFolder NOTIFY changed)
    //! (1.28.1) notes about the song, kept in the file
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QVariantList arrangements READ arrangements NOTIFY changed)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY changed)
    Q_PROPERTY(QVariantList partScores READ partScores NOTIFY changed)
    Q_PROPERTY(QVariantList solos READ solos NOTIFY changed)
    Q_PROPERTY(QVariantList soloKeys READ soloKeys NOTIFY changed)
    Q_PROPERTY(QVariantList references READ references NOTIFY changed)
    Q_PROPERTY(QVariantList media READ media NOTIFY changed)
    Q_PROPERTY(QVariantList bandFolders READ bandFolders NOTIFY changed)
    Q_PROPERTY(QVariantList localFolders READ localFolders NOTIFY changed)
    Q_PROPERTY(bool foldersReady READ foldersReady NOTIFY changed)
    Q_PROPERTY(QVariantList todo READ todo NOTIFY changed)
    Q_PROPERTY(QVariantList statusColors READ statusColors CONSTANT)
    //! when a song is opened: this page first (true, the default) or straight to the score
    Q_PROPERTY(bool openHere READ openHere WRITE setOpenHere NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<mu::context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::dock::IDockWindowProvider> dockWindowProvider = { this };

public:
    explicit SongHubModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void refresh();

    //! to the score: the whole song, an arrangement (its sections shown), a part score (by name), a solo
    Q_INVOKABLE void openScore();
    Q_INVOKABLE void openArrangement(const QString& arrangementId);
    Q_INVOKABLE void openPartScore(const QString& name);
    Q_INVOKABLE void openSolo(const QString& soloId);
    //! a reference PDF in the panel beside the score, or in its own app
    Q_INVOKABLE void openReference(const QString& referenceId);
    Q_INVOKABLE void openReferenceElsewhere(const QString& referenceId);
    //! a recording, video or Transcribe! file in its own app (Transcribe! pointed at the stored recording)
    Q_INVOKABLE void openMedia(const QString& mediaId);
    Q_INVOKABLE void addMedia();
    Q_INVOKABLE void addReference();
    //! Practice beside the score, in "watch", "jam" or "listen"
    Q_INVOKABLE void practice(const QString& mode);
    //! a file or folder in its own app or the Finder
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void showInFolder(const QString& path);
    //! the PDFs in a folder (for a folder's list on the page)
    Q_INVOKABLE QVariantList filesIn(const QString& folder) const;
    Q_INVOKABLE void dispatch(const QString& actionCode);
    //! the score's page with one of its panels open ("todo", "reference", "practice", "audit")
    Q_INVOKABLE void openPanel(const QString& which);
    Q_INVOKABLE void setNotes(const QString& text);

    bool hasSong() const { return m_hasSong; }
    bool isStarScore() const { return m_isStarScore; }
    bool isSoloFile() const { return m_isSoloFile; }
    QString title() const { return m_title; }
    QString facts() const { return m_facts; }
    QString filePath() const { return m_filePath; }
    QString songFolder() const { return m_songFolder; }
    QString notes() const { return m_notes; }
    QVariantList arrangements() const { return m_arrangements; }
    QVariantList sections() const { return m_sections; }
    QVariantList partScores() const { return m_partScores; }
    QVariantList solos() const { return m_solos; }
    QVariantList soloKeys() const { return m_soloKeys; }
    QVariantList references() const { return m_references; }
    QVariantList media() const { return m_media; }
    QVariantList bandFolders() const { return m_bandFolders; }
    QVariantList localFolders() const { return m_localFolders; }
    bool foldersReady() const { return m_foldersReady; }
    QVariantList todo() const { return m_todo; }
    QVariantList statusColors() const;
    bool openHere() const;
    void setOpenHere(bool on);

    //! the setting (read by the open flow too)
    static bool openSongsHere();

signals:
    void changed();

private:
    void toScore();
    static QVariantMap folderEntry(const QString& name, const QString& path);

    bool m_hasSong = false;
    bool m_isStarScore = false;
    bool m_isSoloFile = false;
    QString m_title;
    QString m_facts;
    QString m_filePath;
    QString m_songFolder;
    QString m_notes;
    QVariantList m_arrangements;
    QVariantList m_sections;
    QVariantList m_partScores;
    QVariantList m_solos;
    QVariantList m_soloKeys;
    QVariantList m_references;
    QVariantList m_media;
    QVariantList m_bandFolders;
    QVariantList m_localFolders;
    QVariantList m_todo;
    bool m_loaded = false;
    int m_generation = 0;
    bool m_foldersReady = false;
    QString m_foldersFor;   // which refresh the folders being read are for
};
}
