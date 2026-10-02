/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the To-do panel
 */
#include "todopanelmodel.h"

#include "translation.h"

using namespace mu::project;

TodoPanelModel::TodoPanelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(500);
    connect(&m_timer, &QTimer::timeout, this, [this]() { refresh(); });
}

void TodoPanelModel::load()
{
    starScore()->changed().onNotify(this, [this]() {
        scheduleRefresh();
    });
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        scheduleRefresh();
    });
    refresh();
}

void TodoPanelModel::setActive(bool active)
{
    m_active = active;
    if (active && m_dirty) {
        refresh();
    }
}

void TodoPanelModel::scheduleRefresh()
{
    if (!m_active) {
        m_dirty = true;
        return;
    }
    m_timer.start();
}

void TodoPanelModel::refresh()
{
    m_dirty = false;
    m_items.clear();
    m_nextUp.clear();
    m_done = 0;
    m_hasScore = globalContext()->currentProject() != nullptr;

    static const QStringList NAMES { muse::qtrc("starscore", "No status"), muse::qtrc("starscore", "Sketch"),
                                     muse::qtrc("starscore", "In progress"), muse::qtrc("starscore", "Needs review"),
                                     muse::qtrc("starscore", "Finished") };
    if (m_hasScore) {
        int rank = 0;
        for (const StarScoreTodoItem& t : starScore()->todoList()) {
            ++rank;
            const bool done = t.status == int(StarScoreStatus::Finished);
            const bool missing = t.status < 0;
            if (done) {
                ++m_done;
            } else if (m_nextUp.isEmpty()) {
                m_nextUp = t.title;
            }
            m_items << QVariantMap {
                { "key", t.key }, { "title", t.title }, { "status", t.status }, { "rank", rank },
                { "statusText", missing ? muse::qtrc("starscore", "Not in the score yet") : NAMES.value(t.status) },
                { "details", t.details }, { "note", t.note }, { "done", done }, { "missing", missing },
            };
        }
    }
    emit changed();
}
