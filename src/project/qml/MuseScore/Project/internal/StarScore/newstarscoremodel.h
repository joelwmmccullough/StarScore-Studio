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
class NewStarScoreModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList arrangementTemplates READ arrangementTemplates CONSTANT)
    Q_PROPERTY(QVariantList keys READ keys CONSTANT)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit NewStarScoreModel(QObject* parent = nullptr);

    QVariantList arrangementTemplates() const;
    QVariantList keys() const;

    Q_INVOKABLE bool create(const QVariantMap& options);
};
}
