/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <map>

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "project/istarscoreservice.h"

namespace mu::project {
//! "Compare parts": per instrument, a strip of bars for each of its parts, marking the bars that differ
//! from the reference part. Click a bar to select it in the main score.
class ComparePartsModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit ComparePartsModel(QObject* parent = nullptr);

    QVariantList groups() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void setReference(const QString& instrument, const QString& partId);
    Q_INVOKABLE void selectBar(const QString& partId, int bar);

signals:
    void changed();

private:
    QVariantList m_groups;
    std::map<QString, QString> m_refs;   // instrument family -> reference part id
};
}
