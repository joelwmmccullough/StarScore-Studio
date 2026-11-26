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
#include "notationscene/iselectinstrumentscenario.h"
#include "dockwindow/idockwindowprovider.h"
#include "dockwindow/idockwindow.h"

#include "project/istarscoreservice.h"

namespace mu::project {
//! Model for the Arrangements / Sections bar above the score
class StarScoreBarModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool hasScore READ hasScore NOTIFY changed)
    Q_PROPERTY(bool panelVisible READ panelVisible NOTIFY changed)
    //! (1.35.0) the panel's rows Joel hid: "arrangements", "sections", "progress", "audio", "solos" (app-wide)
    Q_PROPERTY(QStringList hiddenRows READ hiddenRows NOTIFY rowsChanged)
    Q_PROPERTY(bool isStarScoreFile READ isStarScoreFile NOTIFY changed)
    // Each chip list has its own signal, fired only when that list is different: one changed() used to rebuild
    // every chip (and close its menu) on any undo-stack change
    Q_PROPERTY(QVariantList arrangements READ arrangements NOTIFY arrangementsChanged)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY sectionsChanged)
    Q_PROPERTY(QVariantList solos READ solos NOTIFY solosChanged)
    Q_PROPERTY(bool isSoloView READ isSoloView NOTIFY changed)
    Q_PROPERTY(bool canAddSolos READ canAddSolos NOTIFY changed)
    Q_PROPERTY(bool decoOn READ decoOn NOTIFY changed)
    Q_PROPERTY(bool progressShown READ progressShown NOTIFY changed)
    //! (1.24.0) The open score is a Solo Transcription: its song, player, instrument and the song it's tied to
    Q_PROPERTY(bool isSoloTranscription READ isSoloTranscription NOTIFY changed)
    Q_PROPERTY(QVariantMap soloInfo READ soloInfo NOTIFY folderInfoChanged)
    //! A song's Solo Transcriptions (tied to it): name and file
    Q_PROPERTY(QVariantList tiedSolos READ tiedSolos NOTIFY folderInfoChanged)
    //! (1.25.0) The score's audio and Transcribe! files (the Audio folder beside the .starscore): [{ name, path, kind }]
    Q_PROPERTY(QVariantList audioFiles READ audioFiles NOTIFY folderInfoChanged)
    Q_PROPERTY(int referenceCount READ referenceCount NOTIFY changed)
    Q_PROPERTY(bool sectionScoresShown READ sectionScoresShown NOTIFY changed)
    Q_PROPERTY(bool percussionScoreShown READ percussionScoreShown NOTIFY changed)
    Q_PROPERTY(bool canAddAudio READ canAddAudio NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<notation::ISelectInstrumentsScenario> selectInstrumentsScenario = { this };
    muse::ContextInject<muse::dock::IDockWindowProvider> dockWindowProvider = { this };

public:
    explicit StarScoreBarModel(QObject* parent = nullptr);

    bool hasScore() const;
    bool isStarScoreFile() const;
    QVariantList arrangements() const;
    QVariantList sections() const;
    QVariantList solos() const;
    bool isSoloView() const;
    bool canAddSolos() const;
    bool isSoloTranscription() const;
    QVariantMap soloInfo() const { return m_soloInfo; }
    QVariantList tiedSolos() const { return m_tiedSolos; }
    QVariantList audioFiles() const { return m_audioFiles; }
    QVariantMap readSoloInfo() const;
    QVariantList readTiedSolos() const;
    QVariantList readAudioFiles() const;
    bool canAddAudio() const;
    //! One audio or Transcribe! file's menu: Play (Open in Transcribe!) / Show in Finder / Remove
    Q_INVOKABLE QVariantList audioFileMenu(const QVariantMap& item) const;
    //! A solo transcription's Keys menu: named major or minor; the keys with another spelling (D♭ / C♯ major)
    Q_INVOKABLE QVariantList soloKeysMenu() const;

    Q_INVOKABLE void showSolo(const QString& id);
    Q_INVOKABLE void showMainScore();
    Q_INVOKABLE QVariantList soloMenu(const QString& id) const;

    Q_INVOKABLE void load();
    bool decoOn() const;
    //! While a chip's menu is open, panel updates wait until it closes (rebuilding the chips would close the menu)
    Q_INVOKABLE void setHoldUpdates(bool hold);
    Q_INVOKABLE void hidePanel();
    Q_INVOKABLE void toggleDeco();
    bool progressShown() const;
    //! a progress-color action code (starscore-progress-…)
    Q_INVOKABLE void runAction(const QString& code);
    QStringList sectionIds() const;
    bool panelVisible() const;

    Q_INVOKABLE void showArrangement(const QString& id);
    Q_INVOKABLE void toggleSection(const QString& id);
    Q_INVOKABLE void soloSection(const QString& id);

    Q_INVOKABLE QVariantList arrangementMenu(const QString& id) const;
    Q_INVOKABLE QVariantList sectionMenu(const QString& id) const;
    Q_INVOKABLE QVariantList sectionStatusMenu(const QString& id) const;   // (1.40.0) the chip's dot
    Q_INVOKABLE void moveArrangementTo(const QString& id, int index);      // (1.40.0) a chip dragged
    Q_INVOKABLE void moveSectionTo(const QString& id, int index);
    int referenceCount() const { return int(starScore()->references().size()); }
    bool sectionScoresShown() const { return starScore()->sectionScoresShown(); }
    bool percussionScoreShown() const { return starScore()->percussionScoreShown(); }
    Q_INVOKABLE QVariantList addArrangementMenu() const;
    Q_INVOKABLE QVariantList addSectionMenu() const;
    Q_INVOKABLE QVariantList moreMenu() const;
    //! which rows show: a tick each (the right-click menu of a row's name and of the panel, and in "…")
    QStringList hiddenRows() const;
    Q_INVOKABLE void handleMenuItem(const QString& itemId);

    //! Colour for a status value (0 = empty … 4 = finished)
    Q_INVOKABLE QString statusColor(int status) const;
    Q_INVOKABLE QString statusName(int status) const;

signals:
    void changed();
    void rowsChanged();
    void arrangementsChanged();
    void sectionsChanged();
    void solosChanged();
    void folderInfoChanged();

private:
    QString askTwoTrumpetSources();
    //! Re-reads the service and fires changed(), plus the list signals whose list is different
    void refresh();
    QVariantList buildArrangements() const;
    QVariantList buildSections() const;
    QVariantList buildSolos() const;
    QVariantList m_arrangements;
    QVariantList m_sections;
    QVariantList m_solos;
    // (1.38.0) the parts of the panel read from folders (the song's solo transcriptions, its recordings, a solo's tied
    // song): kept here and read again only when the song changes, after a panel action, or every 30 seconds. They were
    // read from Google Drive folders on every edit, twice.
    void refreshFolderInfo(bool force);
    QVariantMap m_soloInfo;
    QVariantList m_tiedSolos;
    QVariantList m_audioFiles;
    QString m_folderInfoKey;
    qint64 m_folderInfoAt = 0;
    bool m_folderInfoDirty = true;

    QString leadSheetText(const QStringList& kinds) const;
    QVariantList statusSubmenu(const QString& prefix, int current, bool rhythm = false) const;
    void openEditDialog(const QString& mode, const QString& id, const QString& slot = QString());
    void createCustomSection();
    void askRename(bool isSection, const QString& id);
    void exportArrangement(const QString& id);

    bool m_holdUpdates = false;
    bool m_pendingUpdate = false;
};
}
