/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "notationswitchlistmodel.h"

#include "notation/inotationparts.h"
#include "notation/inotationelements.h"
#include "translation.h"

#include "log.h"

using namespace mu::notation;
using namespace mu::project;

NotationSwitchListModel::NotationSwitchListModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    m_notationChangedReceiver = std::make_unique<muse::async::Asyncable>();
}

void NotationSwitchListModel::load()
{
    TRACEFUNC;

    onCurrentProjectChanged();
    context()->currentProjectChanged().onNotify(m_notationChangedReceiver.get(), [this]() {
        onCurrentProjectChanged();
    });

    onCurrentNotationChanged();
    context()->currentNotationChanged().onNotify(m_notationChangedReceiver.get(), [this]() {
        onCurrentNotationChanged();
    });
}

void NotationSwitchListModel::onCurrentProjectChanged()
{
    async_disconnectAll();

    loadNotations();

    INotationProjectPtr project = context()->currentProject();
    if (!project) {
        return;
    }

    project->masterNotation()->parts()->partsChanged().onNotify(this, [this]() {
        loadNotations();
    });

    project->masterNotation()->excerptsChanged().onNotify(this, [this]() {
        loadNotations();
    });

    listenProjectSavingStatusChanged();

    // StarScore: part score status dots follow the parts' tags
    starScore()->changed().onNotify(this, [this]() {
        if (!m_notations.isEmpty()) {
            emit dataChanged(index(0), index(m_notations.size() - 1), { RoleStatusColor, RoleStatusName });
        }
    });
}

void NotationSwitchListModel::onCurrentNotationChanged()
{
    INotationPtr notation = context()->currentNotation();
    if (!notation) {
        return;
    }

    int currentNotationIndex = m_notations.indexOf(notation);
    emit currentNotationIndexChanged(currentNotationIndex);
}

void NotationSwitchListModel::loadNotations()
{
    TRACEFUNC;

    beginResetModel();
    m_notations.clear();

    IMasterNotationPtr masterNotation = currentMasterNotation();
    if (!masterNotation) {
        endResetModel();
        return;
    }

    m_notations << masterNotation->notation();
    listenNotationOpeningStatus(masterNotation->notation());

    for (const IExcerptNotationPtr& excerpt: masterNotation->excerpts()) {
        // StarScore Studio: no tabs for part books whose sections are switched off
        if (excerpt->notation()->isOpen() && excerpt->hasVisibleParts()) {
            m_notations << excerpt->notation();
        }

        listenNotationOpeningStatus(excerpt->notation());
        listenExcerptNotationTitleChanged(excerpt);
    }

    endResetModel();

    if (!m_notations.contains(context()->currentNotation())) {
        constexpr int MASTER_NOTATION_INDEX = 0;
        setCurrentNotation(MASTER_NOTATION_INDEX);
    }
}

void NotationSwitchListModel::listenNotationOpeningStatus(INotationPtr notation)
{
    INotationWeakPtr weakNotationPtr = notation;

    notation->openChanged().onNotify(this, [this, weakNotationPtr]() {
        INotationPtr notation = weakNotationPtr.lock();
        if (!notation) {
            return;
        }

        if (notation->isOpen()) {
            if (m_notations.contains(notation)) {
                return;
            }

            beginInsertRows(QModelIndex(), m_notations.size(), m_notations.size());
            m_notations << notation;
            endInsertRows();
        } else {
            int notationIndex = m_notations.indexOf(notation);
            beginRemoveRows(QModelIndex(), notationIndex, notationIndex);
            m_notations.removeAt(notationIndex);
            endRemoveRows();
        }
    }, Asyncable::Mode::SetReplace);
}

void NotationSwitchListModel::listenExcerptNotationTitleChanged(IExcerptNotationPtr excerptNotation)
{
    INotationWeakPtr weakNotationPtr = excerptNotation->notation();

    excerptNotation->nameChanged().onNotify(this, [this, weakNotationPtr]() {
        INotationPtr notation = weakNotationPtr.lock();
        if (!notation) {
            return;
        }

        int index = m_notations.indexOf(notation);
        QModelIndex modelIndex = this->index(index);
        emit dataChanged(modelIndex, modelIndex, { RoleTitle });
    }, Asyncable::Mode::SetReplace);
}

void NotationSwitchListModel::listenProjectSavingStatusChanged()
{
    INotationProjectPtr currentProject = context()->currentProject();
    if (!currentProject) {
        return;
    }

    currentProject->needSave().notification.onNotify(this, [this]() {
        INotationProjectPtr project = context()->currentProject();
        if (!project) {
            return;
        }

        int index = m_notations.indexOf(project->masterNotation()->notation());
        QModelIndex modelIndex = this->index(index);
        emit dataChanged(modelIndex, modelIndex, { RoleNeedSave, RoleIsCloud });
    });

    currentProject->displayNameChanged().onNotify(this, [this]() {
        INotationProjectPtr project = context()->currentProject();
        if (!project) {
            return;
        }

        int index = m_notations.indexOf(project->masterNotation()->notation());
        QModelIndex modelIndex = this->index(index);
        emit dataChanged(modelIndex, modelIndex, { RoleTitle, RoleIsCloud });
    });
}

INotationPtr NotationSwitchListModel::currentNotation() const
{
    return context()->currentNotation();
}

IMasterNotationPtr NotationSwitchListModel::currentMasterNotation() const
{
    return context()->currentMasterNotation();
}

QVariant NotationSwitchListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid()) {
        return QVariant();
    }

    INotationPtr notation = m_notations[index.row()];

    switch (role) {
    case RoleTitle: return QVariant::fromValue(notation->name());
    case RoleNeedSave: {
        bool needSave = context()->currentProject()->needSave().val && isMasterNotation(notation);
        return QVariant::fromValue(needSave);
    }
    case RoleIsCloud: {
        bool isCloud = context()->currentProject()->isCloudProject() && isMasterNotation(notation);
        return QVariant::fromValue(isCloud);
    }
    case RoleStatusColor: {
        static const char* COLORS[] = { "#8A8A8A", "#E0463A", "#F29B30", "#3C8CE7", "#3FB05A" };
        const int status = starscoreStatus(notation);
        return status < 0 || status > 4 ? QString() : QString(COLORS[status]);
    }
    case RoleStatusName: {
        const int status = starscoreStatus(notation);
        static const char* NAMES[] = { "Empty", "Sketch", "In progress", "Needs review", "Finished" };
        return status < 0 || status > 4 ? QString() : muse::qtrc("starscore", NAMES[status]);
    }
    }

    return QVariant();
}

int NotationSwitchListModel::rowCount(const QModelIndex&) const
{
    return m_notations.size();
}

QHash<int, QByteArray> NotationSwitchListModel::roleNames() const
{
    static const QHash<int, QByteArray> roles {
        { RoleTitle, "title" },
        { RoleNeedSave, "needSave" },
        { RoleIsCloud, "isCloud" },
        { RoleStatusColor, "statusColor" },
        { RoleStatusName, "statusName" }
    };

    return roles;
}

void NotationSwitchListModel::setCurrentNotation(int index)
{
    if (!isIndexValid(index)) {
        return;
    }

    context()->setCurrentNotation(m_notations[index]);
}

void NotationSwitchListModel::closeNotation(int index)
{
    if (!isIndexValid(index)) {
        return;
    }

    INotationPtr notation = m_notations[index];

    if (isMasterNotation(notation)) {
        dispatcher()->dispatch("file-close");
    } else {
        if (notation == currentNotation()) {
            // Set new current notation
            context()->setCurrentNotation(m_notations[std::max(0, index - 1)]);
        }
        currentMasterNotation()->setExcerptIsOpen(notation, false);
    }
}

void NotationSwitchListModel::closeOtherNotations(int index)
{
    if (!isIndexValid(index)) {
        return;
    }

    INotationPtr notationToKeepOpen = m_notations[index];
    context()->setCurrentNotation(notationToKeepOpen);

    // Copy the list to avoid modifying it while iterating
    QList<INotationPtr> notations = m_notations;

    for (const INotationPtr& notation : notations) {
        if (!isMasterNotation(notation) && notation != notationToKeepOpen) {
            currentMasterNotation()->setExcerptIsOpen(notation, false);
        }
    }
}

void NotationSwitchListModel::closeAllNotations()
{
    dispatcher()->dispatch("file-close");
}

int NotationSwitchListModel::starscoreStatus(const INotationPtr& notation) const
{
    if (!notation || isMasterNotation(notation) || !notation->elements() || !starScore()) {
        return -1;
    }
    return starScore()->partScoreStatus(notation->elements()->msScore());
}

QVariantList NotationSwitchListModel::contextMenuItems(int index) const
{
    if (!isIndexValid(index)) {
        return {};
    }

    QVariantList result {
        QVariantMap { { "id", "close-tab" }, { "title", muse::qtrc("notation", "Close tab") } },
    };

    bool canCloseOtherTabs = rowCount() > 2 || (rowCount() == 2 && isMasterNotation(m_notations[index]));
    if (canCloseOtherTabs) {
        result << QVariantMap { { "id", "close-other-tabs" }, { "title", muse::qtrc("notation", "Close other tabs") } };
    }

    bool canCloseAllTabs = rowCount() > 1;
    if (canCloseAllTabs) {
        result << QVariantMap { { "id", "close-all-tabs" }, { "title", muse::qtrc("notation", "Close all tabs") } };
    }

    // StarScore: the part score's status (tags every part in it)
    if (!isMasterNotation(m_notations[index]) && starScore() && starScore()->isStarScoreFile()) {
        const int current = starscoreStatus(m_notations[index]);
        static const char* NAMES[] = { "Empty", "Sketch", "In progress", "Needs review", "Finished" };
        QVariantList statusItems;
        for (int i = 0; i < 5; ++i) {
            statusItems << QVariantMap { { "id", "starscore-status:" + QString::number(i) }, { "title", muse::qtrc("starscore", NAMES[i]) },
                                         { "checkable", true }, { "checked", i == current }, { "enabled", true } };
        }
        statusItems << QVariantMap {};
        statusItems << QVariantMap { { "id", "starscore-status:-1" }, { "title", muse::qtrc("starscore", "No tag") },
                                     { "checkable", true }, { "checked", current < 0 }, { "enabled", true } };
        result << QVariantMap {};
        result << QVariantMap { { "title", muse::qtrc("starscore", "Part status") }, { "subitems", statusItems }, { "enabled", true } };
    }

    return result;
}

void NotationSwitchListModel::handleContextMenuItem(int index, const QString& itemId)
{
    if (itemId == "close-tab") {
        closeNotation(index);
    } else if (itemId == "close-other-tabs") {
        closeOtherNotations(index);
    } else if (itemId == "close-all-tabs") {
        closeAllNotations();
    } else if (itemId.startsWith("starscore-status:") && isIndexValid(index)) {
        const INotationPtr notation = m_notations[index];
        if (notation->elements()) {
            starScore()->setPartScoreStatus(notation->elements()->msScore(), itemId.mid(QString("starscore-status:").size()).toInt());
        }
    }
}

bool NotationSwitchListModel::isIndexValid(int index) const
{
    return index >= 0 && index < m_notations.size();
}

bool NotationSwitchListModel::isMasterNotation(const INotationPtr notation) const
{
    return currentMasterNotation()->notation() == notation;
}
