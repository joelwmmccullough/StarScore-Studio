/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "project/istarscoreservice.h"

namespace mu::project {
//! Add › Player annotations: a band member's own note on a sheet (Add player annotation…), or the selected markings
//! made theirs (Mark as player annotation…)
class PlayerAnnotationModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString problem READ problem CONSTANT)
    Q_PROPERTY(QString sheet READ sheet CONSTANT)
    Q_PROPERTY(bool canAdd READ canAdd CONSTANT)
    Q_PROPERTY(int count READ count CONSTANT)
    Q_PROPERTY(QString selectionText READ selectionText CONSTANT)
    Q_PROPERTY(QStringList players READ players CONSTANT)
    Q_PROPERTY(QString defaultPlayer READ defaultPlayer CONSTANT)
    Q_PROPERTY(QString summary READ summary CONSTANT)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit PlayerAnnotationModel(QObject* parent = nullptr);

    QString problem() const { return m_target.problem; }
    QString sheet() const { return m_target.sheet; }
    bool canAdd() const { return m_target.canAdd; }
    int count() const { return m_target.count; }
    QString selectionText() const;
    QStringList players() const { return m_target.players; }
    QString defaultPlayer() const { return m_target.defaultPlayer; }
    QString summary() const { return m_target.summary.join("\n"); }

    //! Each returns a problem to show, or an empty string when done
    Q_INVOKABLE QString add(const QString& player, const QString& text);
    Q_INVOKABLE QString mark(const QString& player);

private:
    StarScoreAnnotationTarget m_target;
};
}
