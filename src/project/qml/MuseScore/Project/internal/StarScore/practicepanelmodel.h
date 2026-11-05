/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Practice panel (1.27.0): one transport for practising with the score showing. Its play,
 * speed, loop and count-in are StarScore's playback's own (so the toolbar, the panel and the falling notes agree).
 * Its settings are kept with the song.
 */
#pragma once

#include <QObject>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"
#include "playback/iplaybackcontroller.h"
#include "project/istarscoreservice.h"
#include "project/internal/starscore/starscorejam.h"

#include <QVariantList>
#include <memory>

namespace mu::project {
class PracticePanelModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool hasScore READ hasScore NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(int speed READ speed WRITE setSpeed NOTIFY changed)          // percent of the written tempo
    Q_PROPERTY(bool loop READ loop NOTIFY changed)
    Q_PROPERTY(bool countIn READ countIn NOTIFY changed)
    Q_PROPERTY(QString colors READ colors WRITE setColors NOTIFY changed)   // the falling notes: "hands" | "pitch"
    Q_PROPERTY(double secondsShown READ secondsShown WRITE setSecondsShown NOTIFY changed)

    //! "watch" (falling notes over the score's playback) | "jam" (a backing band from the chord symbols)
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    Q_PROPERTY(QVariantList grooves READ grooves CONSTANT)                   // [{ text, value }]
    Q_PROPERTY(QString groove READ groove WRITE setGroove NOTIFY changed)
    Q_PROPERTY(int jamBpm READ jamBpm WRITE setJamBpm NOTIFY changed)
    Q_PROPERTY(int choruses READ choruses WRITE setChoruses NOTIFY changed)
    Q_PROPERTY(int tempoStep READ tempoStep WRITE setTempoStep NOTIFY changed)
    Q_PROPERTY(int countInBars READ countInBars WRITE setCountInBars NOTIFY changed)
    Q_PROPERTY(int transpose READ transpose WRITE setTranspose NOTIFY changed)   // semitones, on top of a solo key's
    Q_PROPERTY(bool selectedBarsOnly READ selectedBarsOnly WRITE setSelectedBarsOnly NOTIFY changed)
    Q_PROPERTY(bool bassOn READ bassOn WRITE setBassOn NOTIFY changed)
    Q_PROPERTY(bool drumsOn READ drumsOn WRITE setDrumsOn NOTIFY changed)
    Q_PROPERTY(bool pianoOn READ pianoOn WRITE setPianoOn NOTIFY changed)
    Q_PROPERTY(bool jamPlaying READ jamPlaying NOTIFY jamChanged)
    Q_PROPERTY(QString jamWhere READ jamWhere NOTIFY jamChanged)             // "Chorus 2 of 4 · bar 7"
    Q_PROPERTY(QString jamChord READ jamChord NOTIFY jamChanged)             // the chord sounding
    Q_PROPERTY(QString jamNext READ jamNext NOTIFY jamChanged)               // the next chord
    Q_PROPERTY(QString jamInfo READ jamInfo NOTIFY changed)                  // the form: bars, style, or why there's none

    QML_ELEMENT

    muse::ContextInject<mu::context::IGlobalContext> globalContext = { this };
    muse::ContextInject<mu::playback::IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit PracticePanelModel(QObject* parent = nullptr);

    bool hasScore() const;
    bool playing() const;
    int speed() const;
    void setSpeed(int percent);
    bool loop() const;
    bool countIn() const;
    QString colors() const { return m_colors; }
    void setColors(const QString& c);
    double secondsShown() const { return m_secondsShown; }
    void setSecondsShown(double s);

    Q_INVOKABLE void load();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void rewind();
    Q_INVOKABLE void toggleLoop();
    Q_INVOKABLE void toggleCountIn();
    Q_INVOKABLE void jamPlay();
    Q_INVOKABLE void jamStop();

    QString mode() const { return m_mode; }
    void setMode(const QString& m);
    QVariantList grooves() const;
    QString groove() const { return m_groove; }
    void setGroove(const QString& g);
    int jamBpm() const { return m_jamBpm; }
    void setJamBpm(int v);
    int choruses() const { return m_choruses; }
    void setChoruses(int v);
    int tempoStep() const { return m_tempoStep; }
    void setTempoStep(int v);
    int countInBars() const { return m_countInBars; }
    void setCountInBars(int v);
    int transpose() const { return m_transpose; }
    void setTranspose(int v);
    bool selectedBarsOnly() const { return m_selectedBarsOnly; }
    void setSelectedBarsOnly(bool v);
    bool bassOn() const { return m_bass; }
    void setBassOn(bool v);
    bool drumsOn() const { return m_drums; }
    void setDrumsOn(bool v);
    bool pianoOn() const { return m_piano; }
    void setPianoOn(bool v);
    bool jamPlaying() const;
    QString jamWhere() const { return m_jamWhere; }
    QString jamChord() const { return m_jamChord; }
    QString jamNext() const { return m_jamNext; }
    QString jamInfo() const { return m_jamInfo; }

signals:
    void changed();
    void jamChanged();

private:
    void restoreSettings();
    void saveSettings();
    QString songKey() const;
    void loadForm();
    void updateJamPosition();
    starscore::JamPlayer* player();

    QString m_colors = "hands";
    double m_secondsShown = 4.0;

    QString m_mode = "watch";
    QString m_groove;
    int m_jamBpm = 0;          // 0: the score's tempo
    int m_choruses = 3;
    int m_tempoStep = 0;
    int m_countInBars = 1;
    int m_transpose = 0;
    bool m_selectedBarsOnly = false;
    bool m_bass = true;
    bool m_drums = true;
    bool m_piano = true;
    starscore::JamForm m_form;
    std::unique_ptr<starscore::JamPlayer> m_player;
    QString m_jamWhere, m_jamChord, m_jamNext, m_jamInfo;
    unsigned m_seed = 1;
};
}
