/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Audit library: every .starscore in the projects folder, and what's left to audit in each
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
#include "dockwindow/idockwindow.h"
#include "project/istarscoreservice.h"

namespace mu::project {
class AuditLibraryModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QString folder READ folder NOTIFY changed)
    Q_PROPERTY(QVariantList songs READ songs NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<muse::dock::IDockWindowProvider> dockWindowProvider = { this };

public:
    explicit AuditLibraryModel(QObject* parent = nullptr);

    QString folder() const;
    QVariantList songs() const;
    bool scanning() const;
    QString status() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void chooseFolder();
    //! Checks every file that changed since it was last checked (all of them with force)
    Q_INVOKABLE void scan(bool force);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openSong(int index);

signals:
    void changed();
    void closeRequested();

private:
    void scanNext();
    void sortResults();

    QString m_folder;
    std::vector<StarScoreAuditFileSummary> m_results;
    QStringList m_queue;
    int m_total = 0;
    bool m_force = false;
    bool m_scanning = false;
};
}
