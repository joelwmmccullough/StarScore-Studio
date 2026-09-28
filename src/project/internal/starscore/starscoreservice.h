/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QJsonObject>

#include "../../istarscoreservice.h"

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "notation/iinstrumentsrepository.h"
#include "iprojectcreator.h"
#include "inotationproject.h"
#include "inotationwritersregister.h"
#include "playback/iplaybackcontroller.h"
#include "global/iglobalconfiguration.h"

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
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<notation::IInstrumentsRepository> instrumentsRepository = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };
    muse::ContextInject<INotationWritersRegister> writers = { this };

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

    std::vector<StarScorePartInfo> parts() const override;

    void showArrangement(const QString& arrangementId) override;
    void setSectionOn(const QString& sectionId, bool on) override;
    void soloSection(const QString& sectionId) override;
    void setAllSectionsOn(bool on) override;

    std::vector<StarScoreSectionTemplate> sectionTemplates() const override;
    std::vector<StarScoreArrangementTemplate> arrangementTemplates() const override;
    muse::Ret newStarScore(const StarScoreNewOptions& options) override;

    muse::RetVal<QString> createSectionFromTemplate(const QString& templateKey) override;
    muse::RetVal<QString> createSection(const QString& templateKey, const QString& name,
                                        const std::vector<StarScoreInstrument>& instruments) override;
    muse::RetVal<QString> createSectionFromParts(const QString& name, const QStringList& partIds) override;
    void setSectionStatus(const QString& sectionId, StarScoreStatus status) override;
    void setSectionSkipSheet(const QString& sectionId, const QString& which, bool skip) override;
    void renameSection(const QString& sectionId, const QString& name) override;
    void setSectionParts(const QString& sectionId, const QStringList& partIds) override;
    void moveSection(const QString& sectionId, int newIndex) override;
    void removeSection(const QString& sectionId, bool deleteInstruments) override;

    muse::RetVal<QString> createArrangementFromTemplate(const QString& templateKey) override;
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
    muse::Ret saveAll() override;

    QString bandFolder() const override;
    void setBandFolder(const QString& path) override;
    muse::RetVal<StarScoreBandExportPlan> planBandExport() const override;
    muse::RetVal<QString> exportToBandFolder(const QStringList& onlyPaths) override;
    QStringList bandExportUnticked(const QString& code) const override;
    void setBandExportUnticked(const QString& code, const QStringList& paths) override;

    QString scoreVersion() const override;
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

    struct Data {
        std::vector<StarScoreSection> sections;
        std::vector<StarScoreArrangement> arrangements;
        std::vector<StarScoreSolo> solos;
        QString version;   // "4.0.1"; printed in the footer
    };

    // (de)serialisation of the "starscore" meta tag; public for tests
    static Data fromJson(const QString& json);
    static QString toJson(const Data& data);
    static QString statusKey(StarScoreStatus status);
    static StarScoreStatus statusFromKey(const QString& key);

private:
    mu::engraving::MasterScore* masterScore() const;
    void listenCurrentProject();
    Data load() const;
    void store(const Data& data);
    Data loadFrom(const mu::engraving::MasterScore* score) const;
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
    int applyStylesOnly(const QStringList& partIds);
    //! Sound, volume, pan, reverb and mute for each instrument, from the defaults in mixer_defaults.json
    void applyMixerDefaults(const QStringList& partIds);
    void standardizeImported();
    //! A new "N-Horn Any" section starts with the music of the "N-Horn" section, chair by chair
    void fillAnyHornsFromStandard(const StarScoreSection& anySection);
    //! Name the new parts, hide default-hidden parts/staves, make part books; returns the new section
    StarScoreSection finishNewParts(const std::vector<mu::engraving::Part*>& newParts,
                                    const std::vector<StarScoreInstrument>& instruments);
    const StarScoreSectionTemplate* sectionTemplate(const QString& key) const;
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
    StyleSettings loadStyleSettings() const;
    void saveStyleSettings(const StyleSettings& settings);

    static QString uniqueId(const QStringList& taken, const QString& base);
    static QString idText(const mu::engraving::Part* part);

    muse::async::Notification m_changed;
    muse::async::Notification m_panelVisibleChanged;

    std::shared_ptr<INotationProject> m_mainProject;
    std::map<QString, std::shared_ptr<INotationProject> > m_soloProjects;
    QString m_workDir;
    bool m_switching = false;
};
}
