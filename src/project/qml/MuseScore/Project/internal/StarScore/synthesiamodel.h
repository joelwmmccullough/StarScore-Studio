/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "project/istarscoreservice.h"

namespace mu::project {
//! Run in Synthesia: this song's setup (1.25.5, Joel 5 Nov 2026): the score sent, each staff's hand and each part's
//! sound, kept with the song
class SynthesiaModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString problem READ problem NOTIFY changed)
    Q_PROPERTY(QString song READ song NOTIFY changed)
    Q_PROPERTY(QString scoreName READ scoreName NOTIFY changed)
    Q_PROPERTY(bool sendShowing READ sendShowing NOTIFY changed)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY changed)
    Q_PROPERTY(QStringList soundNames READ soundNames CONSTANT)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit SynthesiaModel(QObject* parent = nullptr);

    QString problem() const { return m_setup.problem; }
    QString song() const { return m_setup.song; }
    QString scoreName() const { return m_setup.scoreName; }
    bool sendShowing() const { return m_setup.sendShowing; }
    QVariantList tracks() const;
    QStringList soundNames() const;

    //! The tracks of the score that would be sent (the one showing, or the main score)
    Q_INVOKABLE void choose(bool sendShowing);
    Q_INVOKABLE void setHand(int track, const QString& hand);
    Q_INVOKABLE void setSound(int track, int program);
    //! Kept with the song; with `run`, the score sent to Synthesia too. Returns a problem to show, or "".
    Q_INVOKABLE QString apply(bool run);

signals:
    void changed();

private:
    StarScoreSynthesiaSetup m_setup;
};
}
