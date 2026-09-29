/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Reference PDF panel beside the score
 */
#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "iinteractive.h"
#include "project/istarscoreservice.h"

namespace mu::project {
class ReferencePanelModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList references READ references NOTIFY changed)   // [{ id, name }]
    Q_PROPERTY(QString currentId READ currentId NOTIFY changed)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY changed)
    Q_PROPERTY(int pageCount READ pageCount NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit ReferencePanelModel(QObject* parent = nullptr);

    QVariantList references() const;
    QString currentId() const;
    int currentIndex() const;
    int pageCount() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void selectIndex(int index);
    //! file:// URL of a page drawn widthPx wide (empty when it can't be drawn)
    Q_INVOKABLE QUrl pageUrl(int page, int widthPx) const;
    Q_INVOKABLE void addReference();
    Q_INVOKABLE void openInViewer();

signals:
    void changed();

private:
    void refresh();

    QString m_currentId;
    int m_pageCount = 0;
};
}
