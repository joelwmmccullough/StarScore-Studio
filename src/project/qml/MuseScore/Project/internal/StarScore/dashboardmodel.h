/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Dashboard: every Starsign song's arrangements, what's done, and what to work on next
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "iinteractive.h"
#include "actions/iactionsdispatcher.h"
#include "dockwindow/idockwindowprovider.h"
#include "project/istarscoreservice.h"

namespace mu::project {
class DashboardModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList columns READ columns CONSTANT)
    Q_PROPERTY(QVariantList tiers READ tiers NOTIFY changed)
    Q_PROPERTY(QVariantList tasks READ tasks NOTIFY changed)
    Q_PROPERTY(QVariantList songs READ songs NOTIFY changed)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY changed)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY changed)
    Q_PROPERTY(int unscanned READ unscanned NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<muse::dock::IDockWindowProvider> dockWindowProvider = { this };

public:
    explicit DashboardModel(QObject* parent = nullptr);

    QString folder() const;
    bool scanning() const;
    QString status() const;
    QVariantList columns() const;
    QVariantList tiers() const;
    QVariantList tasks() const;
    QVariantList songs() const;
    int doneCount() const;
    int totalCount() const;
    int unscanned() const;

    Q_INVOKABLE void load();
    //! "starscore-organize", "starscore-rebuild-all", "starscore-band-roster"
    Q_INVOKABLE void runAction(const QString& action);
    Q_INVOKABLE void chooseFolder();
    //! Reads every song that changed since it was last read (all of them with force)
    Q_INVOKABLE void scan(bool force);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openSong(const QString& path, bool withAudit);
    Q_INVOKABLE void openTask(int index);
    //! The audit tasks' songs one after another (Audit all songs), in priority order
    Q_INVOKABLE void auditInOrder();

signals:
    void changed();

private:
    struct Song {
        QString path;
        QString title;
        bool cover = false;
        bool legacy = false;      // in the library before 29 Sep 2026: done means audited
        int plays = 0;            // setlist.fm, 2026
        bool scanned = false;
        StarScoreAuditFileSummary summary;
    };
    struct Cell {
        QString state;            // "unscanned", "missing", "todo", "done"
        QString text;             // short label for the grid
        QString action;           // what to do next, for the task list
        QString detail;
        int closeness = 0;        // higher = closer to done
        bool audit = false;       // the next step is auditing
        int status = 0;
    };

    void rebuild();
    Cell cellFor(const Song& song, const QString& column) const;
    void scanNext();

    QString m_folder;
    std::vector<Song> m_songs;
    QVariantList m_tiers;
    QVariantList m_tasks;
    QVariantList m_rows;
    std::vector<QString> m_taskPaths;
    std::vector<bool> m_taskAudit;
    QStringList m_queue;
    int m_total = 0;
    int m_done = 0;
    int m_cells = 0;
    bool m_force = false;
    bool m_scanning = false;
};
}
