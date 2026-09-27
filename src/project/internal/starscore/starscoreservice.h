/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include "../../istarscoreservice.h"

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "context/iglobalcontext.h"
#include "notation/iinstrumentsrepository.h"
#include "iprojectcreator.h"

namespace mu::engraving {
class MasterScore;
class Part;
class Excerpt;
}

namespace mu::project {
class StarScoreService : public IStarScoreService, public muse::Contextable, public muse::async::Asyncable
{
    muse::GlobalInject<IProjectCreator> projectCreator;
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<notation::IInstrumentsRepository> instrumentsRepository = { this };

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

    muse::Ret exportArrangement(const QString& arrangementId, const muse::io::path_t& msczPath) override;

    struct Data {
        std::vector<StarScoreSection> sections;
        std::vector<StarScoreArrangement> arrangements;
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
    void applyOnSections(const QStringList& onSectionIds, const QString& actionName);
    QStringList onSectionIds(const Data& data) const;
    std::vector<mu::engraving::Part*> masterPartsOf(const mu::engraving::Excerpt* excerpt) const;
    void addPartBooksFor(const QStringList& partIds);
    //! Name the new parts, hide default-hidden parts/staves, make part books; returns the new section
    StarScoreSection finishNewParts(const std::vector<mu::engraving::Part*>& newParts,
                                    const std::vector<StarScoreInstrument>& instruments);
    const StarScoreSectionTemplate* sectionTemplate(const QString& key) const;
    void removePartsKeepingSystemObjects(const QStringList& partIdsToRemove);

    static QString uniqueId(const QStringList& taken, const QString& base);
    static QString idText(const mu::engraving::Part* part);

    muse::async::Notification m_changed;
};
}
