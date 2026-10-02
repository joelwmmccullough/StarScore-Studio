/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the To-do panel: the open song's work in priority order, with what's finished
 */
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "project/istarscoreservice.h"

namespace mu::project {
class TodoPanelModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList items READ items NOTIFY changed)       // [{ key, title, status, statusText, details, note, done, missing, rank }]
    Q_PROPERTY(QString nextUp READ nextUp NOTIFY changed)          // the first step that isn't finished
    Q_PROPERTY(int doneCount READ doneCount NOTIFY changed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
    Q_PROPERTY(bool hasScore READ hasScore NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<mu::context::IGlobalContext> globalContext = { this };

public:
    explicit TodoPanelModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void setActive(bool active);

    QVariantList items() const { return m_items; }
    QString nextUp() const { return m_nextUp; }
    int doneCount() const { return m_done; }
    int totalCount() const { return int(m_items.size()); }
    bool hasScore() const { return m_hasScore; }

signals:
    void changed();

private:
    void refresh();
    void scheduleRefresh();

    QVariantList m_items;
    QString m_nextUp;
    int m_done = 0;
    bool m_hasScore = false;
    bool m_active = true;
    bool m_dirty = false;
    QTimer m_timer;
};
}
