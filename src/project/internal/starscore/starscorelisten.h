/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice › Listen (1.27.0, Joel 5 Nov 2026: Transcribe!'s part of his work, inside StarScore).
 * The song's stems (Logic's full mix, piano only, everything but piano), stored in its .starscore, played on one
 * clock: switching between them never loses the place. Slower or faster without changing pitch, shifted in pitch,
 * looped, and tied to the score's bars by a bar map (a Transcribe! file's measure markers, or bars tapped in once).
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "audio/main/iplayback.h"
#include "audio/main/iplayer.h"
#include "audio/common/starscorepcmdevice.h"

namespace mu::project::starscore {
struct ListenStem {
    QString mediaId;
    QString name;
    QString kind;
    QString path;
};

//! Where the score's bars are in the recording: the bar (0-based, counted from the score's first bar) each time starts
struct BarMap {
    std::vector<double> barTimes;       // seconds of the recording
    QStringList sectionNames;           // Transcribe!'s section markers, with their times
    std::vector<double> sectionTimes;
    int firstBar = 0;                   // the score's bar the first time is (a pickup, or a recording starting later)

    bool empty() const { return barTimes.size() < 2; }
    //! The score's bar playing at this time (-1 before the first)
    int barAt(double seconds) const;
    //! The time a score bar starts (beyond the map: carried on at the last bars' pace)
    double timeOfBar(int bar) const;
};

//! A Transcribe! file's measure (and section) markers as a bar map; empty when it has none
BarMap transcribeBarMap(const QString& xscPath);

class ListenPlayer : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    muse::ContextInject<muse::audio::IPlayback> playback = { this };

public:
    explicit ListenPlayer(const muse::modularity::ContextPtr& ctx, QObject* parent = nullptr);
    ~ListenPlayer() override;

    //! Decodes the stems and loads them (replacing any); errors() says what couldn't be read
    void load(const std::vector<ListenStem>& stems);
    void clear();
    bool isReady() const { return m_ready; }
    QStringList errors() const { return m_errors; }
    const std::vector<ListenStem>& stems() const { return m_stems; }

    //! The stems heard: one (a switch) or more (a blend, each at an equal share)
    void setHeard(const QStringList& mediaIds);
    QStringList heard() const { return m_heard; }

    void play();
    void pause();
    void stop();
    bool isPlaying() const { return m_playing; }
    //! Seconds of the recording
    void seek(double seconds);
    double position() const;
    double length() const { return m_length; }

    void setSpeed(double speed);        // 0.1–2
    double speed() const { return m_speed; }
    void setPitch(double semitones);
    double pitch() const { return m_pitch; }
    //! Loops from-to (seconds of the recording); to <= from: no loop
    void setLoop(double from, double to);
    bool hasLoop() const { return m_loopTo > m_loopFrom; }

    //! The stem's decoded sound, for the automatic tests: frames (per channel), rate
    struct Info {
        QString name;
        int sampleRate = 0;
        int channels = 0;
        double seconds = 0;
    };
    std::vector<Info> info() const;
    //! Rendered to a file, as heard (speed, pitch, the stems heard); done(ok) when written
    void renderWav(const QString& path, const muse::audio::SoundTrackFormat& format, const std::function<void(bool)>& done);

signals:
    void readyChanged();
    void playingChanged();
    void positionChanged();

private:
    void applyParams();
    void applyLoop();

    struct Track {
        ListenStem stem;
        std::unique_ptr<muse::audio::StarScorePcmDevice> device;
        int sampleRate = 0;
        int channels = 0;
        double seconds = 0;
        muse::audio::TrackId id = -1;
    };
    std::vector<ListenStem> m_stems;
    std::vector<std::unique_ptr<Track> > m_tracks;
    QStringList m_errors;
    QStringList m_heard;
    muse::audio::TrackSequenceId m_sequence = -1;
    std::shared_ptr<muse::audio::IPlayer> m_player;
    bool m_ready = false;
    bool m_playing = false;
    double m_enginePos = 0;   // the playback's own seconds (the recording's, divided by the speed)
    double m_length = 0;
    double m_speed = 1.0;
    double m_pitch = 0.0;
    double m_loopFrom = 0;
    double m_loopTo = 0;
    int m_generation = 0;
};
}
