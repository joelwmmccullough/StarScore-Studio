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
#include "project/istarscoreservice.h"

namespace mu::project {
//! Behind the New StarScore dialog: the dropdown lists, the remembered choices, and creating the score
class NewStarScoreModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList arrangementTemplates READ arrangementTemplates CONSTANT)
    Q_PROPERTY(QVariantList keys READ keys CONSTANT)
    //! The arrangement, doubler instrument and 7th horn chosen last time (settings), to start from
    Q_PROPERTY(QString lastArrangementKey READ lastArrangementKey CONSTANT)
    Q_PROPERTY(QString lastDoublerId READ lastDoublerId CONSTANT)
    Q_PROPERTY(QString lastLowHornId READ lastLowHornId CONSTANT)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit NewStarScoreModel(QObject* parent = nullptr);

    QVariantList arrangementTemplates() const;
    QVariantList keys() const;
    QString lastArrangementKey() const;
    QString lastDoublerId() const;
    QString lastLowHornId() const;

    //! Standard arrangements of 3 or more horns have a woodwind doubler whose instrument is chosen
    Q_INVOKABLE bool hasDoubler(const QString& arrangementKey) const;
    //! 7-Horn Standard: the 7th horn is chosen
    Q_INVOKABLE bool hasLowHorn(const QString& arrangementKey) const;
    //! The doubler dropdown for an arrangement: "<Name> on Alto Sax (recommended)" … (the name from the band roster)
    Q_INVOKABLE QVariantList doublerChoices(const QString& arrangementKey) const;
    //! The arrangement's usual doubler instrument: alto sax for 3/4/5-Horn, soprano sax for 6/7-Horn
    Q_INVOKABLE QString defaultDoublerId(const QString& arrangementKey) const;
    //! The 7th horn dropdown: Bass Trombone (recommended), then the other low horns
    Q_INVOKABLE QVariantList lowHornChoices() const;

    Q_INVOKABLE bool create(const QVariantMap& options);
};
}
