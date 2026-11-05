/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Jam (1.27.0, Joel 5 Nov 2026: iReal Pro's backing tracks, "for soloing over changes"). A backing
 * band (bass, drums, piano comping) made from the score's chord symbols, played inside StarScore without touching the
 * score: the bars of the song (or one chorus of a solo's form), in any key, at any tempo, for as many choruses as
 * wanted, the tempo going up each chorus if asked.
 *
 * - jamForm(): the bars and chords to play, from the chord chart StarScore already makes (a solo: its one chorus).
 * - makeJam(): the notes of each instrument for the settings (a pure function: the automatic tests check it).
 * - JamPlayer: plays them through StarScore's own sounds, as a playback of its own beside the score's.
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

#include "async/asyncable.h"
#include "async/channel.h"
#include "modularity/ioc.h"
#include "audio/main/iplayback.h"
#include "audio/main/iplayer.h"
#include "playback/isoundprofilesrepository.h"
#include "playback/iplaybackconfiguration.h"

namespace mu::engraving {
class MasterScore;
}

namespace mu::project::starscore {
struct JamChord {
    double at = 0;         // quarter notes from the bar's start
    int root = 0;          // pitch class (concert)
    int bass = -1;         // a slash chord's bass pitch class, or -1
    QString name;          // as written ("Bb", "m7", "/D" put together: "Bbm7/D")
    QString quality;       // the written quality ("m7", "7sus", "maj7#11", "ø7"…)
};

struct JamBar {
    int beats = 4;         // the time signature's top number
    int beatType = 4;      // its bottom number
    double quarters = 4;   // the bar's length in quarter notes
    QString mark;          // its rehearsal mark, if any
    std::vector<JamChord> chords;   // where the harmony changes (the first at 0: what sounds from the bar's start)
};

struct JamForm {
    std::vector<JamBar> bars;
    int bpm = 120;          // quarter notes per minute
    QString style;          // the style words (the first tempo marking's, like the iReal Pro chart's)
    bool soloForm = false;  // a solo's one chorus (its choruses: soloChoruses)
    int soloChoruses = 0;
    int firstBar = 0;       // the score's bar (0-based) the form's first bar is (a song: its first bar with a chord)
    QString why;            // when there's nothing to jam over: why
};

//! The bars and chords to jam over: a solo transcription's one chorus of its form when it has one, otherwise every
//! bar in the order written (repeats not played out) from the first chord symbol to the last bar
JamForm jamForm(const mu::engraving::MasterScore* ms, bool solo);

//! The grooves: the style words pick one ("Bossa Nova" -> bossa); the names as Joel sees them
QStringList jamGrooves();
QString jamGrooveName(const QString& groove);
QString jamGrooveFor(const QString& styleWords, int beats);

struct JamSettings {
    QString groove = "swing";
    int bpm = 120;
    int choruses = 3;
    int transpose = 0;          // semitones (a solo's key part score: its key)
    int countInBars = 1;
    int tempoStep = 0;          // bpm added each chorus
    int fromBar = 0;            // a stretch of the form only (0-based, inclusive); -1: the whole form
    int toBar = -1;
    bool bass = true;
    bool drums = true;
    bool piano = true;
    unsigned seed = 1;          // the variations (fills, comping rhythms) are random but the same for the same seed
};

struct JamNote {
    double start = 0;      // seconds
    double length = 0;     // seconds
    int pitch = 60;        // MIDI note (drums: General MIDI drum sounds)
    int velocity = 80;     // 1-127
};

struct JamTrack {
    QString instrument;    // "bass" | "drums" | "piano"
    std::vector<JamNote> notes;
};

struct JamBarTime {
    double start = 0;      // seconds
    int chorus = 0;        // 0 = count-in
    int bar = 0;           // the form's bar (0-based)
};

struct Jam {
    std::vector<JamTrack> tracks;
    std::vector<JamBarTime> bars;   // every bar played, count-in first
    double seconds = 0;             // how long it all lasts
    bool electricBass = false;      // funk and rock: an electric bass; the others an acoustic (upright) one
};

Jam makeJam(const JamForm& form, const JamSettings& settings);

//! A chord quality's notes above the root, in semitones (3rd, 5th, 7th, then extensions), e.g. "m7" -> 0 3 7 10
std::vector<int> jamChordTones(const QString& quality);

//! Plays a Jam through StarScore's own sounds (the basic sound profile), as a playback beside the score's
class JamPlayer : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    muse::ContextInject<muse::audio::IPlayback> playback = { this };
    muse::ContextInject<mu::playback::ISoundProfilesRepository> profilesRepo = { this };
    muse::GlobalInject<mu::playback::IPlaybackConfiguration> playbackConfiguration;

public:
    explicit JamPlayer(const muse::modularity::ContextPtr& ctx, QObject* parent = nullptr);
    ~JamPlayer() override;

    //! Replaces what's loaded; starts playing when play is true
    void load(const Jam& jam, bool play);
    void play();
    void stop();
    bool isPlaying() const { return m_playing; }
    double position() const { return m_position; }
    //! The bar playing now (count-in included), or nothing
    const JamBarTime* barAt(double seconds) const;
    const Jam& jam() const { return m_jam; }
    bool isReady() const { return m_ready; }
    //! The loaded jam rendered to a WAV file (44.1 kHz, 16-bit stereo); done(ok) when written
    void renderWav(const QString& path, const std::function<void(bool)>& done);

signals:
    void playingChanged();
    void positionChanged();
    void ready();   // the instruments are loaded

private:
    void clear();
    void addTracks(const std::function<void()>& done);

    Jam m_jam;
    muse::audio::TrackSequenceId m_sequence = -1;
    std::vector<muse::audio::TrackId> m_tracks;
    std::shared_ptr<muse::audio::IPlayer> m_player;
    bool m_playing = false;
    bool m_playWhenReady = false;
    bool m_ready = false;
    double m_position = 0;
    int m_generation = 0;
};
}
