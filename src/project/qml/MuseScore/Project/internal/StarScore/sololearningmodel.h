/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Dashboard › Solo transcriptions (1.25.0): every solo whose sheets have been exported,
 * each of its twelve keys marked as learned or memorized
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "iinteractive.h"
#include "actions/iactionsdispatcher.h"
#include "project/istarscoreservice.h"

namespace mu::project {
class SoloLearningModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    //! [{ id, path, player, song, instrument, exported, learned (count), memorized (count),
    //!    keys: [{ index, name, short, state }] }]
    Q_PROPERTY(QVariantList solos READ solos NOTIFY changed)
    Q_PROPERTY(int keysMemorized READ keysMemorized NOTIFY changed)
    Q_PROPERTY(int keysTotal READ keysTotal NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };

public:
    explicit SoloLearningModel(QObject* parent = nullptr);

    QVariantList solos() const;
    int keysMemorized() const;
    int keysTotal() const;

    Q_INVOKABLE void load();
    //! A key's next state: not yet -> learned -> memorized -> not yet
    Q_INVOKABLE void cycleKey(const QString& id, int keyIndex);
    Q_INVOKABLE void setKeyState(const QString& id, int keyIndex, const QString& state);
    Q_INVOKABLE void openSolo(const QString& path);

signals:
    void changed();

private:
    QVariantList m_solos;
    int m_memorized = 0;
    int m_total = 0;
    bool m_listening = false;
};
}
