/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once

#include <vector>

#include <QString>
#include <QStringList>

#include "modularity/imoduleinterface.h"
#include "global/io/path.h"
#include "global/types/ret.h"
#include "global/types/retval.h"
#include "global/async/notification.h"

namespace mu::project {
//! Status shown by the coloured dot on each section (and, derived, each arrangement).
enum class StarScoreStatus {
    Empty = 0,
    Sketch,
    InProgress,
    NeedsReview,
    Finished
};

//! A section is a named group of instruments inside the one score: "Lead Sheet", "3-Horn Section",
//! "Rhythm Section". A part can (rarely) belong to more than one section. Parts in no section are
//! never touched by the toggles.
//!
//! A section is "on" when any of its instruments is visible. Turning it off remembers which of its
//! instruments were visible, so turning it back on restores them (e.g. Congas stay hidden).
struct StarScoreSection
{
    QString id;
    QString name;
    QString templateKey;     // "lead-sheet", "3-horn", "rhythm", … or "custom"
    StarScoreStatus status = StarScoreStatus::InProgress;
    bool on = true;          // derived, not stored
    QStringList partIds;     // muse::ID of each part, as text
    QStringList shownPartIds;  // parts to show when the section is turned on
};

//! An arrangement is a named set of sections, e.g. "3-Horn Standard" = Lead Sheet + 3-Horn Section + Rhythm Section.
//! Clicking an arrangement shows exactly its sections and hides/mutes every other section.
struct StarScoreArrangement
{
    QString id;
    QString name;
    QString templateKey;     // template it was made from, or empty
    QStringList sectionIds;
};

struct StarScoreInstrument
{
    QString instrumentId;        // MuseScore instrument id, e.g. "bb-trumpet"
    QString partName;            // name for the part, e.g. "Trumpet" (empty = MuseScore default)
    bool hidden = false;         // hidden when the section is first created (e.g. Congas)
    std::vector<int> hiddenStaves;  // staves of the instrument hidden by default (e.g. 1 = bass staff of a grand staff)
};

struct StarScoreSectionTemplate
{
    QString key;
    QString name;
    std::vector<StarScoreInstrument> instruments;
};

struct StarScoreArrangementTemplate
{
    QString key;
    QString name;
    QStringList sectionKeys;   // StarScoreSectionTemplate keys
};

struct StarScoreNewOptions
{
    QString title;
    QString composer;
    int keyFifths = 0;          // -7 … 7
    int timeSigNumerator = 4;
    int timeSigDenominator = 4;
    int tempoBpm = 120;
    int measures = 32;
    QString arrangementTemplateKey = "3-horn-standard";
};

struct StarScorePartInfo
{
    QString partId;
    QString name;
    bool visible = true;
    QStringList sectionIds;
};

//! StarScore features for the current score. The data lives in the score itself
//! (meta tag "starscore"), so a .starscore file is a normal MuseScore zip:
//! rename it to .mscz and MuseScore Studio opens it.
class IStarScoreService : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IStarScoreService)

public:
    virtual ~IStarScoreService() = default;

    virtual bool hasScore() const = 0;
    virtual bool isStarScoreFile() const = 0;

    virtual std::vector<StarScoreSection> sections() const = 0;
    virtual std::vector<StarScoreArrangement> arrangements() const = 0;
    virtual QString activeArrangementId() const = 0;     // empty when the sections showing match no arrangement
    virtual StarScoreStatus arrangementStatus(const QString& arrangementId) const = 0;  // least-finished section
    virtual muse::async::Notification changed() const = 0;

    virtual std::vector<StarScorePartInfo> parts() const = 0;

    // --- showing / hiding (one undo step each) ---
    virtual void showArrangement(const QString& arrangementId) = 0;
    virtual void setSectionOn(const QString& sectionId, bool on) = 0;
    virtual void soloSection(const QString& sectionId) = 0;
    virtual void setAllSectionsOn(bool on) = 0;

    // --- templates ---
    virtual std::vector<StarScoreSectionTemplate> sectionTemplates() const = 0;
    virtual std::vector<StarScoreArrangementTemplate> arrangementTemplates() const = 0;

    //! Create a brand-new score holding the template arrangement (3-Horn Standard by default) and make it current.
    virtual muse::Ret newStarScore(const StarScoreNewOptions& options) = 0;

    // --- sections ---
    //! Add the template's instruments (empty, following the score's bars and structure) as a new section.
    virtual muse::RetVal<QString> createSectionFromTemplate(const QString& templateKey) = 0;
    virtual muse::RetVal<QString> createSection(const QString& templateKey, const QString& name,
                                                const std::vector<StarScoreInstrument>& instruments) = 0;
    virtual muse::RetVal<QString> createSectionFromParts(const QString& name, const QStringList& partIds) = 0;
    virtual void setSectionStatus(const QString& sectionId, StarScoreStatus status) = 0;
    virtual void renameSection(const QString& sectionId, const QString& name) = 0;
    virtual void setSectionParts(const QString& sectionId, const QStringList& partIds) = 0;
    virtual void moveSection(const QString& sectionId, int newIndex) = 0;
    //! With deleteInstruments, also deletes the section's instruments that no other section uses.
    virtual void removeSection(const QString& sectionId, bool deleteInstruments) = 0;

    // --- arrangements ---
    //! Uses sections that already exist (matched by template key, e.g. an existing "Rhythm Section"),
    //! creates only the missing ones, then shows the new arrangement.
    virtual muse::RetVal<QString> createArrangementFromTemplate(const QString& templateKey) = 0;
    virtual muse::RetVal<QString> createArrangement(const QString& name, const QStringList& sectionIds) = 0;
    virtual void renameArrangement(const QString& arrangementId, const QString& name) = 0;
    virtual void setArrangementSections(const QString& arrangementId, const QStringList& sectionIds) = 0;
    virtual void moveArrangement(const QString& arrangementId, int newIndex) = 0;
    virtual void removeArrangement(const QString& arrangementId) = 0;

    //! Build sections from the score's existing part books ("3-Horn Arrangement", "Lead Sheet", …)
    //! and instrument types (rhythm section), plus a matching arrangement for each horn size.
    //! Returns how many sections were added.
    virtual int detectSections() = 0;

    //! Save a .mscz holding only the instruments and part books of this arrangement's sections.
    virtual muse::Ret exportArrangement(const QString& arrangementId, const muse::io::path_t& msczPath) = 0;
};
}
