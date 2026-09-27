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
//! Model for the small StarScore dialog used to rename things and to pick
//! the instruments of a section or the sections of an arrangement.
class StarScoreEditModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString dialogTitle READ dialogTitle NOTIFY loaded)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(bool showList READ showList NOTIFY loaded)
    Q_PROPERTY(QString listTitle READ listTitle NOTIFY loaded)
    Q_PROPERTY(QVariantList items READ items NOTIFY itemsChanged)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit StarScoreEditModel(QObject* parent = nullptr);

    QString dialogTitle() const;
    QString name() const;
    void setName(const QString& name);
    bool showList() const;
    QString listTitle() const;
    QVariantList items() const;

    Q_INVOKABLE void load(const QString& mode, const QString& itemId);
    Q_INVOKABLE void setChecked(int index, bool checked);
    Q_INVOKABLE bool apply();

signals:
    void loaded();
    void nameChanged();
    void itemsChanged();

private:
    QString m_mode;
    QString m_itemId;
    QString m_name;
    QVariantList m_items;
};
}
