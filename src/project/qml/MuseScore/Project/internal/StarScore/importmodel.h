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
#include "iinteractive.h"
#include "actions/iactionsdispatcher.h"

namespace mu::project {
//! "Import into StarScore": choose the section of each instrument and the arrangements of a MuseScore file
class StarScoreImportModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList parts READ parts NOTIFY changed)
    Q_PROPERTY(QVariantList sectionChoices READ sectionChoices NOTIFY changed)
    Q_PROPERTY(QVariantList arrangements READ arrangements NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };

public:
    explicit StarScoreImportModel(QObject* parent = nullptr);

    QVariantList parts() const;
    QVariantList sectionChoices() const;
    QVariantList arrangements() const;
    QString summary() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void setPartSection(int index, const QString& sectionKey);
    Q_INVOKABLE void setArrangementChecked(const QString& key, bool checked);
    //! Asks whether to standardize, imports, and makes the next save a new .starscore. False if nothing was done.
    Q_INVOKABLE bool doImport();
    //! Closes the file without importing it
    Q_INVOKABLE void cancelImport();

signals:
    void changed();

private:
    void refreshArrangements();

    QVariantList m_parts;
    QVariantList m_arrangements;
    QStringList m_checked;
    QString m_summary;
};
}
