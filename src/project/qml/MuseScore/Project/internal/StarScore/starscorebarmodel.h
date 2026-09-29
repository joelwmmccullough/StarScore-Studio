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

#include "project/istarscoreservice.h"

namespace mu::project {
//! Model for the Arrangements / Sections bar above the score
class StarScoreBarModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool hasScore READ hasScore NOTIFY changed)
    Q_PROPERTY(bool panelVisible READ panelVisible NOTIFY changed)
    Q_PROPERTY(bool isStarScoreFile READ isStarScoreFile NOTIFY changed)
    Q_PROPERTY(QVariantList arrangements READ arrangements NOTIFY changed)
    Q_PROPERTY(QVariantList sections READ sections NOTIFY changed)
    Q_PROPERTY(QVariantList solos READ solos NOTIFY changed)
    Q_PROPERTY(bool isSoloView READ isSoloView NOTIFY changed)
    Q_PROPERTY(bool canAddSolos READ canAddSolos NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<notation::ISelectInstrumentsScenario> selectInstrumentsScenario = { this };

public:
    explicit StarScoreBarModel(QObject* parent = nullptr);

    bool hasScore() const;
    bool isStarScoreFile() const;
    QVariantList arrangements() const;
    QVariantList sections() const;
    QVariantList solos() const;
    bool isSoloView() const;
    bool canAddSolos() const;

    Q_INVOKABLE void showSolo(const QString& id);
    Q_INVOKABLE void showMainScore();
    Q_INVOKABLE QVariantList soloMenu(const QString& id) const;

    Q_INVOKABLE void load();
    //! While a chip's menu is open, panel updates wait until it closes (rebuilding the chips would close the menu)
    Q_INVOKABLE void setHoldUpdates(bool hold);
    Q_INVOKABLE void hidePanel();
    bool panelVisible() const;

    Q_INVOKABLE void showArrangement(const QString& id);
    Q_INVOKABLE void toggleSection(const QString& id);
    Q_INVOKABLE void soloSection(const QString& id);

    Q_INVOKABLE QVariantList arrangementMenu(const QString& id) const;
    Q_INVOKABLE QVariantList sectionMenu(const QString& id) const;
    Q_INVOKABLE QVariantList addArrangementMenu() const;
    Q_INVOKABLE QVariantList addSectionMenu() const;
    Q_INVOKABLE QVariantList moreMenu() const;
    Q_INVOKABLE void handleMenuItem(const QString& itemId);

    //! Colour for a status value (0 = empty … 4 = finished)
    Q_INVOKABLE QString statusColor(int status) const;
    Q_INVOKABLE QString statusName(int status) const;

signals:
    void changed();

private:
    QVariantList statusSubmenu(const QString& prefix, int current, bool rhythm = false) const;
    void openEditDialog(const QString& mode, const QString& id, const QString& slot = QString());
    void createCustomSection();
    void askRename(bool isSection, const QString& id);
    void exportArrangement(const QString& id);

    bool m_holdUpdates = false;
    bool m_pendingUpdate = false;
};
}
