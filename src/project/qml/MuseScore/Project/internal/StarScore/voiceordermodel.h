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
//! "Check voice order": per section, a bar strip for each ordering rule between two of its parts
class VoiceOrderModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList sections READ sections NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit VoiceOrderModel(QObject* parent = nullptr);

    QVariantList sections() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void selectBar(const QString& partId, int bar);

signals:
    void changed();

private:
    QVariantList m_sections;
};
}
