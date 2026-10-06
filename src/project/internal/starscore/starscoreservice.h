/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <map>
#include <optional>
#include <set>

#include <QJsonObject>
#include <QObject>

#include "../../istarscoreservice.h"
#include "global/types/string.h"

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "notation/iinstrumentsrepository.h"
#include "iprojectcreator.h"
#include "inotationproject.h"
#include "inotationwritersregister.h"
#include "playback/iplaybackcontroller.h"
#include "importexport/audioexport/iaudioexportconfiguration.h"
#include "global/iglobalconfiguration.h"
#include "dockwindow/idockwindowprovider.h"
#include "dockwindow/idockwindow.h"
#include "actions/iactionsdispatcher.h"
#include "iinteractive.h"
#include "../../iprojectfilescontroller.h"

namespace mu::engraving {
class MasterScore;
class Part;
class Excerpt;
class Measure;
}

namespace mu::project {
class StarScoreService : public IStarScoreService, public muse::Contextable, public muse::async::Asyncable
{
    muse::GlobalInject<IProjectCreator> projectCreator;
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;
    muse::GlobalInject<iex::audioexport::IAudioExportConfiguration> audioExportConfiguration;
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<notation::IInstrumentsRepository> instrumentsRepository = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };
    muse::ContextInject<INotationWritersRegister> writers = { this };
    muse::ContextInject<muse::dock::IDockWindowProvider> dockWindowProvider = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<IProjectFilesController> projectFilesController = { this };

public:
    explicit StarScoreService(const muse::modularity::ContextPtr& iocCtx);

    void init();

    bool hasScore() const override;
    bool isStarScoreFile() const override;

    std::vector<StarScoreSection> sections() const override;
    std::vector<StarScoreArrangement> arrangements() const override;
    QString activeArrangementId() const override;
    StarScoreStatus arrangementStatus(const QString& arrangementId) const override;
    muse::async::Notification changed() const override;
    std::vector<StarScoreTodoItem> todoList() const override;   // starscoretodo.cpp

    std::vector<StarScorePartInfo> parts() const override;

    void showArrangement(const QString& arrangementId) override;
    void setSectionOn(const QString& sectionId, bool on) override;
    void soloSection(const QString& sectionId) override;
    void setAllSectionsOn(bool on) override;
    bool decoOn() const override;
    void toggleDeco() override;

    std::vector<StarScoreSectionTemplate> sectionTemplates() const override;
    std::vector<StarScoreArrangementTemplate> arrangementTemplates() const override;
    muse::Ret newStarScore(const StarScoreNewOptions& options) override;
    std::vector<StarScoreHornChoice> doublerChoices() const override;
    std::vector<StarScoreHornChoice> lowHornChoices() const override;
    QString rosterDoublerName() const override;
    QString songDoublerInstrumentId() const override;

    muse::RetVal<QString> createSectionFromTemplate(const QString& templateKey, const QString& doublerInstrumentId = {},
                                                    const QString& lowHornInstrumentId = {}) override;
    muse::RetVal<QString> createSection(const QString& templateKey, const QString& name,
                                        const std::vector<StarScoreInstrument>& instruments) override;
    muse::RetVal<QString> createSectionFromParts(const QString& name, const QStringList& partIds) override;
    void setSectionStatus(const QString& sectionId, StarScoreStatus status) override;
    void setSectionAutoStatus(const QString& sectionId) override;
    std::map<QString, StarScoreStatus> partStatuses() const override;
    void setPartStatus(const QString& partId, int status) override;
    int partScoreStatus(const mu::engraving::Score* score) const override;
    void setPartScoreStatus(const mu::engraving::Score* score, int status) override;
    void setSectionSkipSheet(const QString& sectionId, const QString& which, bool skip) override;
    void renameSection(const QString& sectionId, const QString& name) override;
    void setSectionParts(const QString& sectionId, const QStringList& partIds) override;
    void moveSection(const QString& sectionId, int newIndex) override;
    void removeSection(const QString& sectionId, bool deleteInstruments) override;

    muse::RetVal<QString> createArrangementFromTemplate(const QString& templateKey, const QString& doublerInstrumentId = {},
                                                        const QString& lowHornInstrumentId = {}) override;
    muse::RetVal<QString> createArrangement(const QString& name, const QStringList& sectionIds) override;
    void renameArrangement(const QString& arrangementId, const QString& name) override;
    void setArrangementSections(const QString& arrangementId, const QStringList& sectionIds) override;
    void moveArrangement(const QString& arrangementId, int newIndex) override;
    void removeArrangement(const QString& arrangementId) override;

    int detectSections() override;

    QString defaultStylePath() const override;
    void setDefaultStylePath(const QString& path) override;
    std::vector<StarScoreStyleRule> styleRules() const override;
    void setStyleRules(const std::vector<StarScoreStyleRule>& rules) override;
    int applyStyles(const QStringList& partIds = {}) override;

    std::vector<StarScoreSolo> solos() const override;
    QString currentSoloId() const override;
    bool canAddSolos() const override;
    bool isSoloProject(const INotationProject* project) const override;
    bool hasUnsavedSolos() const override;
    muse::io::path_t mainProjectPath() const override;
    muse::RetVal<StarScoreSoloPlan> planSolo(const muse::io::path_t& soloFile, int startBar, int endBar) const override;
    muse::RetVal<QString> addSolo(const muse::io::path_t& soloFile, const QString& name, int startBar, int endBar) override;
    muse::Ret showSolo(const QString& soloId) override;
    muse::Ret showMainScore() override;
    muse::Ret refreshSoloBand(const QString& soloId) override;
    void renameSolo(const QString& soloId, const QString& name) override;
    void removeSolo(const QString& soloId) override;
    muse::Ret exportSolo(const QString& soloId, const muse::io::path_t& msczPath) override;
    std::vector<StarScoreReference> references() const override;
    muse::RetVal<QString> addReference(const muse::io::path_t& pdfFile) override;
    QString identicalReferenceName(const muse::io::path_t& pdfFile) const override;
    void removeReference(const QString& referenceId) override;
    muse::io::path_t referencePath(const QString& referenceId) const override;
    QString currentReferenceId() const override;
    void setCurrentReferenceId(const QString& referenceId) override;
    int referencePageCount(const QString& referenceId) const override;
    void setReferenceInvert(const QString& referenceId, bool invert) override;
    void renameReference(const QString& referenceId, const QString& name) override;
    void setReferenceInstrument(const QString& referenceId, const QString& instrument) override;
    void moveReference(const QString& referenceId, int newIndex) override;
    QStringList referenceInstrumentChoices() const override;
    QString referencePageImage(const QString& referenceId, int page, int widthPx) const override;
    muse::Ret saveAll() override;

    QString bandFolder() const override;
    void setBandFolder(const QString& path) override;
    std::optional<starscore::org::ExportInfo> takeLastExport() override;
    QString projectsFolder() const override;
    void setProjectsFolder(const QString& path) override;
    QString songCode() const override;
    QJsonObject songRecordings() const override;
    void setSongRecordings(const QJsonObject& recordings) override;
    muse::RetVal<StarScoreBandExportPlan> planBandExport() const override;
    void makeBassHornVersions(const QString& sectionId) override;
    std::vector<std::pair<QString, QString> > versionMains(const QString& sectionId) const override;
    std::pair<QString, QString> mainLowHorn(const QString& sectionId) const override;
    std::vector<StarScoreFlexibleSheet> flexibleSheets(const QString& sectionId) const override;
    muse::RetVal<QString> makeFlexibleSheetPart(const QString& sectionId, const QString& sheetName) override;
    StarScoreExportProgress exportProgress() const override;
    std::vector<StarScoreOutdatedSong> outdatedSongs() const override;
    muse::RetVal<QString> updateCurrentSongSheets() override;
    void startUpdateAllSheets(const QStringList& paths) override;
    void cancelUpdateAllSheets() override;
    StarScoreUpdateAllStatus updateAllStatus() const override;
    //! Records where the export is, logs it, and lets the windows repaint
    void reportExportProgress(const QString& phase, int done, int total, const QString& step);
    void endExportProgress() override;
    int flexibleViewMode() const override;
    void setFlexibleViewMode(int mode) override;
    bool hasFlexibleSections() const override;
    bool sectionScoresShown() const override;
    void setSectionScoresShown(bool shown) override;
    bool percussionScoreShown() const override;
    void setPercussionScoreShown(bool shown) override;
    //! The band's name for a low horn ("Bass Trombone", "Bari Sax", …, "Tuba"), or "" when the instrument isn't one
    //! of the eight. The low horn a 7-Horn section is built on and its stand-in versions are all told apart this way.
    static QString lowHornName(const QString& instrumentId);
    muse::Ret registerBandSong(const QString& title, int category, const QString& code) override;
    muse::RetVal<QString> exportArrangementsAsMscz(const QString& folder) override;
    muse::RetVal<QString> exportToBandFolder(const QStringList& onlyPaths) override;
    muse::RetVal<QString> exportAudioDemos() override;
    QStringList bandExportUnticked(const QString& code) const override;
    void setBandExportUnticked(const QString& code, const QStringList& paths) override;

    QString scoreVersion() const override;
    StarScoreVersionSuggestion suggestVersionBump(const StarScoreBandExportPlan& plan) const override;
    void setScoreVersion(const QString& version) override;

    void syncArrangementScores() override;
    void openArrangementScore(const QString& arrangementId) override;

    std::vector<StarScoreComparison> compareParts(const std::map<QString, QString>& referenceByInstrument) const override;
    void selectBar(const QString& partId, int bar) override;
    bool needsImport() const override;
    StarScoreImportPlan planImport() const override;
    muse::Ret applyImport(const std::map<QString, QString>& sectionByPart, const QStringList& arrangementKeys,
                          bool standardize) override;
    void saveAsNewStarScore() override;

    bool isPanelVisible() const override;
    void setPanelVisible(bool visible) override;
    muse::async::Notification panelVisibleChanged() const override;
    std::vector<StarScoreVoiceSection> checkVoiceOrder() const override;
    bool minMajSymbolInCurrentScore() const override;
    void setMinMajSymbolInCurrentScore(bool on) override;
    void syncMinMajDefaults();

    muse::Ret exportArrangement(const QString& arrangementId, const muse::io::path_t& msczPath) override;

    StarScoreAuditReport audit() const override;
    void setAuditReferenceSection(const QString& sectionId) override;
    void setAuditIntentional(const QString& issueKey, bool intentional) override;
    void setArrangementAudited(const QString& arrangementId, bool audited) override;
    void setListenApproved(const QString& stepKey, bool approved) override;
    void showAuditIssue(const StarScoreAuditIssue& issue) override;
    void playListenStep(const StarScoreListenStep& step, bool withRhythmSection) override;
    void stopListening() override;
    bool isListening() const override;
    muse::async::Notification listeningChanged() const override;
    QString auditLibraryFolder() const override;
    void setAuditLibraryFolder(const QString& path) override;
    QStringList auditLibraryFiles(const QString& folder) const override;
    StarScoreAuditFileSummary auditFile(const QString& path, bool force) override;
    std::vector<StarScoreAuditFileSummary> cachedLibraryAudit(const QString& folder) const override;
    void songbookRenderSheets(const QString& songPath, std::vector<StarScoreSongbookSheet>& sheets) override;
    muse::RetVal<std::vector<StarScoreSongbookSheet> > songbookChartSheets(const QString& songPath, const QString& arrangementTemplateKey,
                                                                         const QString& chartTitle, const QString& outDir) override;
    void startAuditWalk(const QStringList& paths) override;
    bool auditWalkActive() const override;
    int auditWalkIndex() const override;
    QStringList auditWalkPaths() const override;
    void auditWalkStep(int delta) override;
    void stopAuditWalk() override;
    void openAuditWalkSong();

    struct Data {
        std::vector<StarScoreSection> sections;
        std::vector<StarScoreArrangement> arrangements;
        std::vector<StarScoreSolo> solos;
        std::vector<StarScoreReference> references;
        QString version;   // "4.0.1"; printed in the footer
        std::map<QString, QString> partStatus;   // part id -> status key
        // Big Band, Orchestra and Marching Band: their full score has a status of its own (arrangement id ->
        // status key); the arrangement is finished only when its full score is marked Finished too
        std::map<QString, QString> scoreStatus;
        // Stand-in versions (alternates) live in their section like its other instruments: shown with it and in its
        // arrangement scores. False in files from before 1.15.1, where they were made hidden; fixed on opening.
        bool alternatesInSection = false;
        // Stand-in versions made before 1.15.3 got their barlines matched to the line they stand in for once; after
        // that their formatting is left to Joel
        bool alternateBarlinesMatched = false;
        // The Flexible chairs' working clefs (treble / soprano / alto, 1.18.7) set once in a file from before that;
        // after that a clef Joel picks for a chair is kept
        bool flexibleClefsSet = false;
        QString fileId;    // permanent id of this .starscore (kept when the file is moved or renamed)
        // StarScore Deco switched on: score ("" = main score, else the part book's name) -> its fonts before
        // (music symbols, music text, dynamics)
        std::map<QString, QStringList> decoRestore;
        // Organizer: each exported sheet's bar signatures at its last export ("sheet path" -> {version, bars, marks}),
        // for bar-by-bar changelog entries; and this song's recordings (a copy of its part of recordings.json)
        QJsonObject exportSignatures;
        // Each Score's system breaks once systems that fit together were joined (export path -> {key, breaks: end
        // ticks}); worked out again when the reference part's systems or the song's bars change
        QJsonObject scoreSystems;
        QJsonObject recordings;

        // Audit mode
        QString auditReferenceSectionId;          // section the others are compared with (empty = automatic)
        QStringList auditIntentional;             // issue keys marked "intentional"
        std::map<QString, std::pair<QString, QString> > auditAudited;   // arrangement id -> (date, fingerprint)
        // part id -> (date, fingerprint): a sheet marked Finished (or in a section marked Finished) no longer needs
        // auditing; an arrangement whose sheets are all like that counts as audited
        std::map<QString, std::pair<QString, QString> > auditPartAudited;
        QStringList auditListened;                // approved listen steps
    };
    //! A sheet's status as the sheet record keeps it: the least finished of its parts (and a full score's own)
    static StarScoreStatus exportedSheetStatus(const Data& data, const StarScoreBandFile& f);

    // (de)serialisation of the "starscore" meta tag; public for tests
    static Data fromJson(const QString& json);
    static QString toJson(const Data& data);
    static QString statusKey(StarScoreStatus status);
    static StarScoreStatus statusFromKey(const QString& key);
    static QString idTextOf(const mu::engraving::Part* part);   // the same as idText (defined in starscoreaudit.cpp)
    //! Big Band, Orchestra and Marching Band are the arrangements whose full score has its own status
    static bool hasOwnScoreStatus(const QString& arrangementTemplateKey);
    //! The full score's own status of such an arrangement (Empty when it has none yet)
    static StarScoreStatus ownScoreStatus(const Data& data, const StarScoreArrangement& arrangement);
    //! A rhythm-section instrument's role: "drums", "percussion", "bass", "guitar" or "keys". The one classifier
    //! behind a section's automatic status (who reads the lead sheet) and the to-do list's rhythm steps.
    static QString rhythmRole(const QString& instrumentId);
    //! Main-score parts of the parts in a part score (each part score staff is linked to a main-score staff);
    //! empty for the main score itself
    static std::vector<mu::engraving::Part*> masterPartsOf(const mu::engraving::Score* score, const mu::engraving::MasterScore* ms);

private:
    //! Organizer: per-bar signatures of a sheet ({bars: [...], marks: {bar: "A"}}), and the 1-3 horn parts analysed
    //! for the Horn Part Guides (starscoreaudit.cpp)
    QJsonObject organizerSignature(const mu::engraving::MasterScore* ms, const QStringList& partIds) const;
    QJsonObject organizerHornAnalysis(const mu::engraving::MasterScore* ms, const StarScoreBandExportPlan& plan) const;
    std::optional<starscore::org::ExportInfo> m_lastExport;
    //! Organizer: each exported sheet's status (with size and md5) and the sheets each folder colour needs, in
    //! 6 Inbox/.organizer/sheets/CODE.json (starscoresheetrecord.cpp). onDisk: the sheets this export wrote or left
    //! as they were because they came out the same
    void writeSheetRecord(const mu::engraving::MasterScore* ms, const Data& data, const StarScoreBandExportPlan& full,
                          const QStringList& onDisk) const;

    mu::engraving::MasterScore* masterScore() const;
    void listenCurrentProject();
    Data load() const;
    void store(const Data& data);
    //! The score's StarScore data. Parsing the "starscore" meta tag on every query made each panel refresh parse
    //! it dozens of times, so the result is cached per score and reused while the tag and the instruments are
    //! the same (store() rewrites the tag, which is what invalidates it).
    Data loadFrom(const mu::engraving::MasterScore* score) const;
    struct LoadCacheEntry {
        muse::String tag;
        QString parts;   // the parts' ids and instruments, which loadFrom's result also depends on
        Data data;
    };
    mutable std::map<const mu::engraving::MasterScore*, LoadCacheEntry> m_loadCache;
    //! The sections with their derived "on" flag worked out from the instruments showing
    std::vector<StarScoreSection> sectionsWithOn(const Data& data) const;
    //! The open project whose main score this is (the main project, a solo project or the current one)
    std::shared_ptr<INotationProject> projectOf(const mu::engraving::MasterScore* score) const;
    //! changed() is fired once from the event loop however many stores and undo-stack changes asked for it:
    //! every listener re-reads everything when it fires
    void scheduleChanged();
    bool m_changedScheduled = false;
    //! A score that has gone bad (a bar with the wrong number of beats, as The Courier's bar 60 got an extra half rest on
    //! top of a whole note): checked a moment after each edit, so the warning comes while Undo can still fix it
    void scheduleIntegrityCheck();
    void checkIntegrity();
    int m_integrityGeneration = 0;
    bool m_integrityBroken = false;
    //! Rests that start inside a note or rest of the same voice (nothing valid does): removed when `remove`,
    //! otherwise only counted. `where` gets "Lead Sheet, bar 60" for each. Inside a command when removing.
    int overlappingRests(mu::engraving::MasterScore* ms, bool remove, QStringList* where) const;
    //! Not a QObject itself: the context for its timers, so a pending timer is dropped with the service
    QObject m_timerGuard;
    std::vector<StarScoreFileArrangement> summarizeArrangements(const Data& data, const StarScoreAuditReport& report) const;
    void storeTo(mu::engraving::MasterScore* score, const Data& data, const std::shared_ptr<INotationProject>& project);

    // solos
    void onCurrentProjectChanged();
    void clearSolos();
    void extractSolos();
    muse::io::path_t soloWorkPath(const QString& soloId) const;
    muse::RetVal<std::shared_ptr<INotationProject> > loadSoloProject(const QString& soloId);
    muse::Ret buildSoloBand(const std::shared_ptr<INotationProject>& soloProject, const StarScoreSolo& solo);
    muse::Ret injectSolos(const muse::io::path_t& starscorePath);
    mu::engraving::Measure* mainMeasureByNumber(int barNumber) const;
    std::shared_ptr<INotationProject> exportSourceProject() const;
    muse::Ret writePdf(const notation::INotationPtr& notation, const QString& path) const;
    void applyOnSections(const QStringList& onSectionIds, const QString& actionName);
    QStringList onSectionIds(const Data& data) const;
    std::vector<mu::engraving::Part*> masterPartsOf(const mu::engraving::Excerpt* excerpt) const;
    void addPartBooksFor(const QStringList& partIds);
    int applyStylesOnly(const QStringList& partIds, const Data& data);
    void syncMinMajDefaults(const Data& data);
    //! Sound, volume, pan, reverb and mute for each instrument, from the defaults in mixer_defaults.json
    void applyMixerDefaults(const QStringList& partIds);
    void standardizeImported();
    //! A new "N-Horn Any" section starts with the music of the "N-Horn" section, chair by chair
    void fillAnyHornsFromStandard(const StarScoreSection& anySection);
    //! A Flexible section's chairs written in the clefs that keep their range on the staff: Horn 1 treble, the
    //! 3-Horn's Horn 2 soprano clef, the bottom chair alto clef. Only a chair still in its old clef (treble, the
    //! bottom chair bass) changes. Returns how many chairs changed. The exported sheets have their own clefs.
    int applyFlexibleView(const StarScoreSection& section);
    void syncSectionTabs(const QStringList& turnedOnSectionIds);
    void removeEmptyVersions(const QStringList& partIds);
    //! A part with stand-in versions (a 7-Horn section's main low horn, a Piccolo) marked Finished: offer the versions
    //! it lacks (versionsFor)
    void offerLowAlternates(const QStringList& partIds, bool asked = false);
    //! Legacy audit: marks (or unmarks) these sheets as no longer needing auditing, with a fingerprint of their music
    void markPartsAudited(Data& data, const mu::engraving::MasterScore* ms, const QStringList& partIds, bool audited) const;
    //! instrumentIds: which of the main part's versions (versionsFor ids) to make
    muse::RetVal<QStringList> createLowAlternates(const QString& sectionId, const QString& mainPartId, const QStringList& instrumentIds);
    //! The low horns a 7-Horn main part's stand-in versions can be made on: everything in lowHornChoices() except
    //! the main part's own instrument (the versions are made in this order)
    std::vector<StarScoreHornChoice> lowVersionsFor(const QString& mainInstrumentId) const;
    //! The stand-in versions a part gets, by its instrument and its section's template: the other low horns for a
    //! 7-Horn section's low horn; a Flute for a Piccolo in any horn section (Standard, 1-Horn or custom; not a Flexible
    //! section, whose chairs are concert-pitch "Horn N" staves, nor a big band / orchestra / marching band). Empty for
    //! everything else.
    std::vector<StarScoreHornChoice> versionsFor(const QString& mainInstrumentId, const QString& sectionTemplateKey) const;
    //! The band's name for a part's instrument, for the versions' texts ("Bass Trombone", "Piccolo")
    static QString versionMainName(const QString& instrumentId);
    //! A template's instruments with the New StarScore choices applied: the doubler takes the alto sax chair of a
    //! 3/4/5-Horn section or the soprano sax chair of a 6/7-Horn one; the low horn takes the 7-Horn bass trombone
    std::vector<StarScoreInstrument> templateInstrumentsFor(const StarScoreSectionTemplate& t, const QString& doublerInstrumentId,
                                                            const QString& lowHornInstrumentId) const;
    //! Name the new parts, hide default-hidden parts/staves, make part books; returns the new section
    StarScoreSection finishNewParts(const std::vector<mu::engraving::Part*>& newParts,
                                    const std::vector<StarScoreInstrument>& instruments);
    std::optional<StarScoreSectionTemplate> sectionTemplate(const QString& key) const;
    static void autoHideLeadBassStaff(const notation::IMasterNotationPtr& master, mu::engraving::Part* part);
    void removePartsKeepingSystemObjects(const QStringList& partIdsToRemove);
    static void removePartsKeepingSystemObjects(const notation::IMasterNotationPtr& master, const QStringList& partIdsToRemove);

    struct StyleSettings {
        QString bandFolder;
        QString defaultStyle;
        int builtinStyleVersion = 0;
        QJsonObject exportUnticked;   // song code -> [relative paths]
        std::vector<StarScoreStyleRule> rules;
    };
    void installBuiltinDefaultStyle();
    //! starscore_styles.json, read once and kept until saveStyleSettings() writes it (it was re-read on every
    //! defaultStylePath() / styleRules() / bandFolder() call, several times per refresh)
    StyleSettings loadStyleSettings() const;
    void saveStyleSettings(const StyleSettings& settings);
    mutable std::optional<StyleSettings> m_styleSettings;

    static QString uniqueId(const QStringList& taken, const QString& base);
    static QString idText(const mu::engraving::Part* part);

    muse::async::Notification m_changed;
    muse::async::Notification m_panelVisibleChanged;

    std::shared_ptr<INotationProject> m_mainProject;
    std::map<QString, std::shared_ptr<INotationProject> > m_soloProjects;
    QString m_workDir;
    QString m_currentReferenceId;
    QString currentScoreKey() const;
    void pickReferenceForCurrentScore();
    QStringList referenceViewSettingsKeys() const;
    void ensureFileId();
    void showOldAlternates();
    //! The horn part scores show their exported title (instrument name, arrangement label); see starscorebandexport.cpp
    int labelPartBooks();
    std::vector<StarScoreTodoItem> todoList(const Data& data) const;   // starscoretodo.cpp
    //! The to-do list as a printable page (HTML for the organizer's PDF printer)
    QString todoPdfHtml(const QString& title, const QString& code, const QString& version) const;
    //! Horn parts are named "7H: Bari Sax", "3H Flexible: Horn 1"…; returns how many were renamed
    int standardizeHornNames();
    //! Tidying on opening a file: older stand-in versions shown, horn names, part score titles
    void tidyOpenedScore();
    void clearComposerInCurrentScore();
    void lockFinishedScore(engraving::Score* score);
    void lockFinishedParts(const QStringList& partIds);
    void openPartBooks(const QStringList& partIds);
    // Automatic test run (starscoreautotest.cpp): STARSCORE_AUTOTEST=<output folder>
    static bool autotestRequested();
    void startAutotest();
    void runAutotestSteps(QStringList steps, int reportNumber);
    void autotestReport(const QString& base);
    bool m_autotestStarted = false;
    QJsonObject loadReferenceView() const;
    void recordReferenceView();
    void listenReferencePanel();
    void applyReferencePanelState();

    // audit
    StarScoreAuditReport auditScore(const mu::engraving::MasterScore* ms, const Data& data) const;
    std::vector<StarScoreVoiceSection> checkVoiceOrderIn(const mu::engraving::MasterScore* ms, const Data& data) const;
    void onPlaybackPosition(int tick);
    muse::async::Notification m_listeningChanged;
    StarScoreExportProgress m_exportProgress;
    // "Update all sheets"
    bool m_exportDryRun = false;          // the export writes nothing; m_dryRunChanged gets the sheets that would change
    QStringList m_dryRunChanged;
    QStringList m_updateQueue;
    StarScoreUpdateAllStatus m_updateStatus;
    int m_updateWaits = 0;
    void updateAllNext();
    void updateAllWhenOpen(const QString& path);
    std::map<QString, bool> m_listenVisibility;   // every part's visibility before listening
    // instruments shown only to show an audit issue: they don't count toward a section being on, aren't
    // remembered as part of a section, and are hidden again when another issue is shown
    std::set<QString> m_auditRevealed;
    bool m_listening = false;
    int m_listenStartTick = -1;
    int m_listenEndTick = -1;
    bool m_restoringReferenceView = false;
    bool m_referencePanelTouched = false;   // you opened or closed the reference panel since the file opened
    qint64 m_projectOpenedMs = 0;   // when the current project was opened (ms since epoch): the notation page
                                    // restores its panels a moment later, which can reopen the reference panel
    bool m_switching = false;
};
}
