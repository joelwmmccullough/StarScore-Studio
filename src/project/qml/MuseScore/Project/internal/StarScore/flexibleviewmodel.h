/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the status bar's "Show Flexible horns as" menu
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "project/istarscoreservice.h"

namespace mu::project {
//! How every Flexible section's chairs are shown while writing (Joel, 6 Oct 2026). One setting for all scores, kept on
//! this computer; shown in the status bar only when the score has a Flexible section. The display only: ranges and
//! exported sheets don't change.
class FlexibleViewModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int current READ current NOTIFY changed)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY changed)
    Q_PROPERTY(QVariantList menuItems READ menuItems NOTIFY changed)

    QML_ELEMENT

public:
    explicit FlexibleViewModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE void choose(const QString& itemId);

    bool available() const;
    int current() const;
    QString currentTitle() const;
    QVariantList menuItems() const;

signals:
    void changed();

private:
    muse::ContextInject<IStarScoreService> starScore = { this };
};
}
