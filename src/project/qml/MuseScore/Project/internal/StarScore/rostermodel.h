/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the band roster (Sheets and Demos/6 Inbox/.organizer/roster.json): who plays what, for the
 * changelogs and the Horn Part Guides. Members who leave are kept as former members, so their history stays.
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "project/istarscoreservice.h"
#include "project/internal/starscore/organizer/orgstores.h"

namespace mu::project {
class RosterModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList players READ players NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit RosterModel(QObject* parent = nullptr);

    QVariantList players() const;
    QString status() const { return m_status; }
    bool dirty() const { return m_dirty; }

    Q_INVOKABLE void load();
    //! key: name, instruments (comma separated), blurb, current, horn, chair2, key2, chair3, key3
    Q_INVOKABLE void setField(int index, const QString& key, const QVariant& value);
    Q_INVOKABLE void addPlayer();
    Q_INVOKABLE bool save();

signals:
    void changed();

private:
    starscore::org::Roster m_roster;
    QString m_status;
    bool m_dirty = false;
    bool m_unreadable = false;    // roster.json is there but couldn't be read: never save over it
};
}
