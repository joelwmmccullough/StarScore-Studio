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
#include "dockwindow/idockwindowprovider.h"
#include "dockwindow/idockwindow.h"
#include "project/istarscoreservice.h"
#include "project/internal/starscore/starscorejam.h"
#include "project/internal/starscore/starscorelisten.h"

#include <QTimer>

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

    // Listen (1.27.0): the song's stems, on one clock
    Q_PROPERTY(QVariantList stems READ stems NOTIFY listenChanged)           // [{ id, name, kind, heard }]
    Q_PROPERTY(bool listenReady READ listenReady NOTIFY listenChanged)
    Q_PROPERTY(bool listenPlaying READ listenPlaying NOTIFY listenChanged)
    Q_PROPERTY(QString listenTime READ listenTime NOTIFY listenPositionChanged)   // "1:23 / 6:02"
    Q_PROPERTY(QString listenBar READ listenBar NOTIFY listenPositionChanged)     // "Bar 17 · B section"
    Q_PROPERTY(double listenFraction READ listenFraction NOTIFY listenPositionChanged)
    Q_PROPERTY(int listenSpeed READ listenSpeed WRITE setListenSpeed NOTIFY listenChanged)   // percent
    Q_PROPERTY(double listenPitch READ listenPitch WRITE setListenPitch NOTIFY listenChanged) // semitones
    Q_PROPERTY(bool listenLoop READ listenLoop NOTIFY listenChanged)
    Q_PROPERTY(bool follow READ follow WRITE setFollow NOTIFY listenChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY listenChanged)
    Q_PROPERTY(bool videoShown READ videoShown NOTIFY listenChanged)
    Q_PROPERTY(QString barMapInfo READ barMapInfo NOTIFY listenChanged)
    Q_PROPERTY(bool tapping READ tapping NOTIFY listenChanged)
    Q_PROPERTY(QString listenInfo READ listenInfo NOTIFY listenChanged)
    //! where the recording is, as seconds of the score's playback (for the falling notes under Listen); -1 without a bar map
    Q_PROPERTY(double listenScoreTime READ listenScoreTime NOTIFY listenPositionChanged)

    QML_ELEMENT

    muse::ContextInject<mu::context::IGlobalContext> globalContext = { this };
    muse::ContextInject<mu::playback::IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::dock::IDockWindowProvider> dockWindowProvider = { this };

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
    //! (1.28.0) the Song page's Watch / Jam / Listen: the mode it asked for, once
    Q_INVOKABLE void takeRequestedMode();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void rewind();
    Q_INVOKABLE void toggleLoop();
    Q_INVOKABLE void toggleCountIn();
    Q_INVOKABLE void jamPlay();
    Q_INVOKABLE void jamStop();

    Q_INVOKABLE void listenPlayPause();
    Q_INVOKABLE void listenRewind();
    Q_INVOKABLE void listenSeekFraction(double f);
    Q_INVOKABLE void listenSkip(int bars);           // a bar (or 5 seconds without a bar map) back or on
    Q_INVOKABLE void chooseStem(const QString& id, bool add);   // add: blend it with those heard
    Q_INVOKABLE void toggleListenLoop();             // the selected bars (or none)
    Q_INVOKABLE void goToSelection();                // the recording to the selected bar
    Q_INVOKABLE void toggleVideo();
    Q_INVOKABLE void startTapping();                 // the bar map tapped in: T (or the button) at each bar
    Q_INVOKABLE void tap();
    Q_INVOKABLE void finishTapping();
    Q_INVOKABLE void clearTaps();

    QVariantList stems() const;
    bool listenReady() const;
    bool listenPlaying() const;
    QString listenTime() const;
    QString listenBar() const;
    double listenFraction() const;
    int listenSpeed() const;
    void setListenSpeed(int percent);
    double listenPitch() const;
    void setListenPitch(double st);
    bool listenLoop() const;
    bool follow() const { return m_follow; }
    void setFollow(bool f);
    bool hasVideo() const;
    bool videoShown() const;
    QString barMapInfo() const { return m_barMapInfo; }
    bool tapping() const { return m_tapping; }
    QString listenInfo() const { return m_listenInfo; }
    double listenScoreTime() const;

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
    void listenChanged();
    void listenPositionChanged();

private:
    void restoreSettings();
    void saveSettings();
    QString songKey() const;
    void loadForm();
    void updateJamPosition();
    starscore::JamPlayer* player();
    starscore::ListenPlayer* listener();
    void loadListen();
    void loadBarMap();
    void onListenPosition();
    QString videoPath() const;

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

    std::unique_ptr<starscore::ListenPlayer> m_listen;
    QString m_listenSong;          // the song the stems were loaded from
    starscore::BarMap m_barMap;
    QString m_barMapInfo;
    QString m_listenInfo;
    int m_listenBarNow = -9999;
    bool m_follow = true;
    bool m_tapping = false;
    std::vector<double> m_taps;
    QTimer m_videoTimer;
    std::vector<double> m_playedBarSeconds;   // each bar as played (repeats played out): its start in the score's seconds
};
}
