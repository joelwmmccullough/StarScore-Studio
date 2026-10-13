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
//! Mutes for this part… (Joel, 13 Oct 2026): the mutes a brass part plays with, printed under its name top left
class PartMutesModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString problem READ problem CONSTANT)
    Q_PROPERTY(QString partName READ partName CONSTANT)
    Q_PROPERTY(QStringList options READ options CONSTANT)
    Q_PROPERTY(QStringList chosen READ chosen CONSTANT)
    Q_PROPERTY(QString custom READ custom CONSTANT)
    Q_PROPERTY(bool flexible READ flexible CONSTANT)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit PartMutesModel(QObject* parent = nullptr);

    QString problem() const { return m_target.problem; }
    QString partName() const { return m_target.partName; }
    QStringList options() const { return m_target.options; }
    QStringList chosen() const;
    QString custom() const;
    bool flexible() const { return m_target.flexible; }

    //! The sheet's lines for these mutes, as a preview ("with Harmon Mute" / "and Cup Mute")
    Q_INVOKABLE QString preview(const QStringList& ticked, const QString& custom) const;
    Q_INVOKABLE void apply(const QStringList& ticked, const QString& custom);

private:
    QStringList mutesFor(const QStringList& ticked, const QString& custom) const;

    StarScoreMuteTarget m_target;
};
}
