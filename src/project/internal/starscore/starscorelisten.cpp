/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice › Listen (see starscorelisten.h)
 */
#include "starscorelisten.h"
#include "starscoredecode.h"
#include "starscorevideo.h"

#include <algorithm>
#include <cmath>

#include <QFile>
#include <QRegularExpression>

#include "log.h"

using namespace muse;
using namespace muse::audio;

namespace mu::project::starscore {
// ---------------------------------------------------------------------------------------------------------------------
//  The bar map
// ---------------------------------------------------------------------------------------------------------------------

int BarMap::barAt(double seconds) const
{
    if (barTimes.empty() || seconds < barTimes.front()) {
        return -1;
    }
    const auto it = std::upper_bound(barTimes.begin(), barTimes.end(), seconds);
    int i = int(it - barTimes.begin()) - 1;
    if (it == barTimes.end() && barTimes.size() >= 2) {
        // past the last marker: at the last bars' pace
        const double bar = barTimes.back() - barTimes[barTimes.size() - 2];
        if (bar > 0) {
            i += int(std::floor((seconds - barTimes.back()) / bar));
        }
    }
    return i + firstBar;
}

double BarMap::timeOfBar(int bar) const
{
    const int i = bar - firstBar;
    if (barTimes.empty()) {
        return 0;
    }
    if (i < 0) {
        return barTimes.front();
    }
    if (i < int(barTimes.size())) {
        return barTimes[size_t(i)];
    }
    const double pace = barTimes.size() >= 2 ? barTimes.back() - barTimes[barTimes.size() - 2] : 2.0;
    return barTimes.back() + pace * (i - int(barTimes.size()) + 1);
}

BarMap transcribeBarMap(const QString& xscPath)
{
    // Transcribe! keeps its markers as lines of comma-separated fields, the kind first (M a measure, S a section,
    // B a beat) and the time last as h:mm:ss.ffffff
    BarMap map;
    QFile f(xscPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return map;
    }
    const QString text = QString::fromUtf8(f.readAll());
    static const QRegularExpression timeRe("^(\\d+):(\\d\\d):(\\d\\d(?:\\.\\d+)?)$");
    bool inMarkers = false;
    std::vector<double> bars;
    for (const QString& raw : text.split(QRegularExpression("[\\r\\n]+"), Qt::SkipEmptyParts)) {
        const QString line = raw.trimmed();
        if (line.startsWith("SectionStart,")) {
            inMarkers = line.contains("Marker", Qt::CaseInsensitive);
            continue;
        }
        if (line.startsWith("SectionEnd,")) {
            inMarkers = false;
            continue;
        }
        if (!inMarkers) {
            continue;
        }
        const QStringList fields = line.split(',');
        if (fields.size() < 2 || fields.front().size() != 1) {
            continue;
        }
        const QRegularExpressionMatch m = timeRe.match(fields.back().trimmed());
        if (!m.hasMatch()) {
            continue;
        }
        const double t = m.captured(1).toDouble() * 3600 + m.captured(2).toDouble() * 60 + m.captured(3).toDouble();
        const QChar kind = fields.front().at(0).toUpper();
        if (kind == 'M' || kind == 'S') {
            bars.push_back(t);
        }
        if (kind == 'S') {
            map.sectionTimes.push_back(t);
            map.sectionNames << (fields.size() > 3 ? fields.at(3).trimmed() : QString());
        }
    }
    std::sort(bars.begin(), bars.end());
    for (double t : bars) {
        if (map.barTimes.empty() || t - map.barTimes.back() > 0.05) {
            map.barTimes.push_back(t);
        }
    }
    return map;
}

// ---------------------------------------------------------------------------------------------------------------------
//  The player
// ---------------------------------------------------------------------------------------------------------------------

ListenPlayer::ListenPlayer(const muse::modularity::ContextPtr& ctx, QObject* parent)
    : QObject(parent), muse::Contextable(ctx)
{
}

ListenPlayer::~ListenPlayer()
{
    clear();
}

void ListenPlayer::clear()
{
    ++m_generation;
    if (m_player) {
        m_player->stop();
        m_player->playbackPositionChanged().disconnect(this);
        m_player->playbackStatusChanged().disconnect(this);
        m_player.reset();
    }
    if (m_sequence >= 0 && playback()) {
        playback()->removeSequence(m_sequence);
    }
    m_sequence = -1;
    // (the decoded sound goes once the engine's tracks are gone: removing the sequence above)
    m_tracks.clear();
    m_stems.clear();
    m_errors.clear();
    m_heard.clear();
    m_enginePos = 0;
    m_length = 0;
    if (m_ready) {
        m_ready = false;
        emit readyChanged();
    }
    if (m_playing) {
        m_playing = false;
        emit playingChanged();
    }
}

void ListenPlayer::load(const std::vector<ListenStem>& stems)
{
    clear();
    m_stems = stems;
    for (const ListenStem& s : stems) {
        const DecodedAudio a = decodeAudio(s.path);
        if (!a.samples || a.samples->empty() || a.sampleRate <= 0) {
            m_errors << QString("%1: %2").arg(s.name, a.error.isEmpty() ? QString("couldn't be read") : a.error);
            continue;
        }
        auto t = std::make_unique<Track>();
        t->stem = s;
        t->sampleRate = a.sampleRate;
        t->channels = a.channels;
        t->seconds = a.seconds();
        t->device = std::make_unique<StarScorePcmDevice>(a.samples, a.sampleRate, a.channels);
        m_length = std::max(m_length, t->seconds);
        m_tracks.push_back(std::move(t));
    }
    if (m_tracks.empty() || !playback()) {
        LOGW() << "[starscore] listen: nothing to load (" << m_tracks.size() << " decoded, playback "
               << (playback() ? "there" : "missing") << ")";
        emit readyChanged();
        return;
    }
    if (m_heard.isEmpty()) {
        m_heard << m_tracks.front()->stem.mediaId;
    }
    const int generation = m_generation;
    playback()->addSequence().onResolve(this, [this, generation](const TrackSequenceId& id) {
        if (generation != m_generation) {
            playback()->removeSequence(id);
            return;
        }
        m_sequence = id;
        m_player = playback()->player(id);
        LOGI() << "[starscore] listen: sequence " << id << (m_player ? "" : " (no player)");
        if (!m_player) {
            return;
        }
        m_player->playbackPositionChanged().onReceive(this, [this](const secs_t& s) {
            m_enginePos = double(s);
            emit positionChanged();
        });
        m_player->playbackStatusChanged().onReceive(this, [this](PlaybackStatus st) {
            const bool playing = st == PlaybackStatus::Running;
            if (playing != m_playing) {
                m_playing = playing;
                emit playingChanged();
            }
        });
        auto remaining = std::make_shared<int>(int(m_tracks.size()));
        for (auto& t : m_tracks) {
            Track* track = t.get();
            AudioParams params;
            playback()->addTrack(m_sequence, ("Listen " + track->stem.name).toStdString(), track->device.get(), std::move(params))
            .onResolve(this, [this, track, remaining, generation](const TrackId tid, const AudioParams&) {
                if (generation != m_generation) {
                    return;
                }
                track->id = tid;
                if (--*remaining == 0) {
                    m_player->setDuration(msecs_t((m_length / m_speed + 1.0) * 1000.0));
                    applyParams();
                    m_ready = true;
                    emit readyChanged();
                }
            })
            .onReject(this, [this, remaining, track](int code, const std::string& text) {
                m_errors << QString("%1: %2 (%3)").arg(track->stem.name, QString::fromStdString(text)).arg(code);
                --*remaining;
            });
        }
    });
}

void ListenPlayer::applyParams()
{
    if (m_sequence < 0 || !playback()) {
        return;
    }
    const double share = m_heard.isEmpty() ? 0.0 : 1.0 / std::max(1.0, std::sqrt(double(m_heard.size())));
    for (const auto& t : m_tracks) {
        if (t->id < 0) {
            continue;
        }
        AudioInputParams in;
        // (written with a point whatever the Mac's language: the engine reads them so)
        in.configuration["speed"] = QString::number(m_speed, 'f', 6).toStdString();
        in.configuration["pitch"] = QString::number(m_pitch, 'f', 6).toStdString();
        in.configuration["gain"] = QString::number(m_heard.contains(t->stem.mediaId) ? share : 0.0, 'f', 6).toStdString();
        playback()->setInputParams(m_sequence, t->id, in);
    }
}

void ListenPlayer::setHeard(const QStringList& mediaIds)
{
    m_heard = mediaIds;
    applyParams();
}

void ListenPlayer::play()
{
    if (!m_player || !m_ready) {
        return;
    }
    if (position() >= m_length - 0.05) {
        seek(0);
    }
    m_player->play();
}

void ListenPlayer::pause()
{
    if (m_player) {
        m_player->pause();
    }
}

void ListenPlayer::stop()
{
    if (m_player) {
        m_player->stop();
    }
}

double ListenPlayer::position() const
{
    return m_enginePos * m_speed;
}

void ListenPlayer::seek(double seconds)
{
    seconds = std::clamp(seconds, 0.0, std::max(0.0, m_length));
    m_enginePos = seconds / m_speed;
    if (m_player) {
        m_player->seek(secs_t(m_enginePos));
    }
    emit positionChanged();
}

void ListenPlayer::setSpeed(double speed)
{
    speed = std::clamp(speed, 0.1, 2.0);
    if (std::abs(speed - m_speed) < 1e-9) {
        return;
    }
    // the place in the recording kept: the playback's own time is the recording's divided by the speed
    const double at = position();
    m_speed = speed;
    applyParams();
    if (m_player) {
        m_player->setDuration(msecs_t((m_length / m_speed + 1.0) * 1000.0));
    }
    applyLoop();
    seek(at);
}

void ListenPlayer::setPitch(double semitones)
{
    m_pitch = std::clamp(semitones, -24.0, 24.0);
    applyParams();
}

void ListenPlayer::setLoop(double from, double to)
{
    m_loopFrom = from;
    m_loopTo = to;
    applyLoop();
}

void ListenPlayer::applyLoop()
{
    if (!m_player) {
        return;
    }
    if (m_loopTo > m_loopFrom) {
        m_player->setLoop(msecs_t(m_loopFrom / m_speed * 1000.0), msecs_t(m_loopTo / m_speed * 1000.0));
    } else {
        m_player->resetLoop();
    }
}

void ListenPlayer::renderWav(const QString& path, const SoundTrackFormat& format, const std::function<void(bool)>& done)
{
    if (m_sequence < 0 || !m_ready || !playback()) {
        done(false);
        return;
    }
    SoundTrackFormat f = format;
    playback()->saveSoundTrack(m_sequence, muse::io::path_t(path), std::move(f))
    .onResolve(this, [done](const bool ok) { done(ok); })
    .onReject(this, [done](int, const std::string&) { done(false); });
}

std::vector<ListenPlayer::Info> ListenPlayer::info() const
{
    std::vector<Info> out;
    for (const auto& t : m_tracks) {
        out.push_back({ t->stem.name, t->sampleRate, t->channels, t->seconds });
    }
    return out;
}

#ifndef Q_OS_MACOS
// (no video window off the Mac)
namespace video {
bool available() { return false; }
bool open(const QString&, const QString&) { return false; }
void close() {}
bool isOpen() { return false; }
void follow(double, double, bool, double) {}
}
#endif
}
