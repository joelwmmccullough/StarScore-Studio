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
//! "Export to Sheets and Demos": every planned sheet with a tick box. Un-ticked sheets are
//! remembered per song (by its code) for the next export.
class BandExportModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString heading READ heading NOTIFY loaded)
    Q_PROPERTY(QString errorText READ errorText NOTIFY loaded)
    Q_PROPERTY(QString notes READ notes NOTIFY loaded)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY itemsChanged)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit BandExportModel(QObject* parent = nullptr);

    QString heading() const;
    QString errorText() const;
    QString notes() const;
    QVariantList items() const;
    int checkedCount() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void setChecked(int index, bool checked);
    Q_INVOKABLE void setFolderChecked(const QString& folder, bool checked);
    Q_INVOKABLE void setAllChecked(bool checked);
    Q_INVOKABLE bool isFolderChecked(const QString& folder) const;

    //! Remembers the ticks, writes the ticked sheets and returns a summary
    Q_INVOKABLE QString exportNow();

signals:
    void loaded();
    void itemsChanged();

private:
    void saveTicks();
    void refreshHeaders();

    QString m_heading;
    QString m_error;
    QString m_notes;
    QString m_code;
    QVariantList m_items;
};
}
