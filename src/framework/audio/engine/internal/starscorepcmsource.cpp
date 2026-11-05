/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a recording as a track of a playback (see starscorepcmsource.h)
 */
#include "starscorepcmsource.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>

#include "rubberband/RubberBandStretcher.h"
#include "audio/common/starscorepcmdevice.h"

#include "log.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::engine;

std::shared_ptr<StarScorePcmSource> StarScorePcmSource::fromDevice(io::IODevice* device)
{
    if (!device) {
        return nullptr;
    }
    // the samples shared with the device that brought them (no copy)
    if (auto* pcm = dynamic_cast<StarScorePcmDevice*>(device)) {
        if (!pcm->samples() || pcm->channels() < 1 || pcm->channels() > 2 || pcm->sampleRate() < 8000) {
            return nullptr;
        }
        return std::make_shared<StarScorePcmSource>(pcm->samples(), pcm->sampleRate(), pcm->channels());
    }
    if (!device->isOpen()) {
        device->open(io::IODevice::ReadOnly);
    }
    const uint8_t* raw = device->readData();
    const size_t size = device->size();
    StarScorePcmHeader h;
    if (!raw || size < sizeof(h) || std::memcmp(raw, h.magic, sizeof(h.magic)) != 0) {
        LOGE() << "[starscore] not recording data";
        return nullptr;
    }
    std::memcpy(&h, raw, sizeof(h));
    if (h.channels < 1 || h.channels > 2 || h.sampleRate < 8000 || h.frames < 0
        || sizeof(h) + size_t(h.frames) * size_t(h.channels) * sizeof(float) > size) {
        LOGE() << "[starscore] recording data with a bad header";
        return nullptr;
    }
    auto samples = std::make_shared<std::vector<float> >(size_t(h.frames) * size_t(h.channels));
    std::memcpy(samples->data(), raw + sizeof(h), samples->size() * sizeof(float));
    return std::make_shared<StarScorePcmSource>(samples, h.sampleRate, h.channels);
}

StarScorePcmSource::StarScorePcmSource(std::shared_ptr<const std::vector<float> > samples, int sampleRate, int channels)
    : m_samples(std::move(samples)), m_rate(sampleRate), m_channels(channels)
{
    m_frames = m_samples ? int64_t(m_samples->size()) / std::max(1, m_channels) : 0;
}

StarScorePcmSource::~StarScorePcmSource() = default;

void StarScorePcmSource::setOutputSpec(const OutputSpec& spec)
{
    m_out = spec;
    makeStretcher();
}

bool StarScorePcmSource::straight() const
{
    return std::abs(m_speed - 1.0) < 1e-6 && std::abs(m_pitch) < 1e-6 && (m_out.sampleRate == 0 || int(m_out.sampleRate) == m_rate);
}

void StarScorePcmSource::makeStretcher()
{
    m_stretcher.reset();
    if (straight() || m_out.sampleRate == 0) {
        return;
    }
    using RB = RubberBand::RubberBandStretcher;
    // the recording's rate to the output's, the speed, and the pitch: output frames per input frame, and the pitch
    // put right for playing at the output's rate
    const double rateRatio = double(m_out.sampleRate) / double(m_rate);
    const double timeRatio = rateRatio / m_speed;
    const double pitchScale = (1.0 / rateRatio) * std::pow(2.0, m_pitch / 12.0);
    m_stretcher = std::make_unique<RB>(size_t(m_rate), 2,
                                       RB::OptionProcessRealTime | RB::OptionEngineFiner | RB::OptionPitchHighConsistency
                                       | RB::OptionChannelsTogether, timeRatio, pitchScale);
    m_stretcher->setMaxProcessSize(4096);
    // the stretcher's start-up: fed until it gives sound, so the first block isn't late
    const size_t pad = m_stretcher->getPreferredStartPad();
    if (pad > 0) {
        std::vector<float> zeros(pad, 0.f);
        const float* in[2] = { zeros.data(), zeros.data() };
        m_stretcher->process(in, pad, false);
    }
    m_toDrop = m_stretcher->getStartDelay();
}

void StarScorePcmSource::readInput(float* const* out, size_t frames)
{
    const float* s = m_samples ? m_samples->data() : nullptr;
    int64_t p = int64_t(m_pos);
    for (size_t i = 0; i < frames; ++i, ++p) {
        if (!s || p < 0 || p >= m_frames) {
            out[0][i] = out[1][i] = 0.f;
            continue;
        }
        if (m_channels == 1) {
            out[0][i] = out[1][i] = s[p];
        } else {
            out[0][i] = s[p * 2];
            out[1][i] = s[p * 2 + 1];
        }
    }
    m_pos += double(frames);
}

samples_t StarScorePcmSource::process(float* buffer, samples_t samplesPerChannel)
{
    const size_t n = samplesPerChannel;
    const unsigned outCh = std::max<unsigned>(1, m_out.audioChannelCount);
    if (!m_active) {
        return 0;
    }
    for (int c = 0; c < 2; ++c) {
        if (m_outBuf[c].size() < n) {
            m_outBuf[c].resize(n);
        }
    }
    float* out[2] = { m_outBuf[0].data(), m_outBuf[1].data() };
    if (!m_stretcher) {
        readInput(out, n);
    } else {
        size_t got = 0;
        while (got < n) {
            const int avail = m_stretcher->available();
            if (avail > 0) {
                const size_t want = std::min<size_t>(n - got, size_t(avail));
                float* o[2] = { out[0] + got, out[1] + got };
                const size_t r = m_stretcher->retrieve(o, want);
                // (the stretcher's own delay at the start: dropped)
                if (m_toDrop > 0) {
                    const size_t drop = std::min(m_toDrop, r);
                    std::memmove(o[0], o[0] + drop, (r - drop) * sizeof(float));
                    std::memmove(o[1], o[1] + drop, (r - drop) * sizeof(float));
                    m_toDrop -= drop;
                    got += r - drop;
                } else {
                    got += r;
                }
                continue;
            }
            size_t need = m_stretcher->getSamplesRequired();
            if (need == 0) {
                need = 512;
            }
            need = std::min<size_t>(need, 4096);
            for (int c = 0; c < 2; ++c) {
                if (m_in[c].size() < need) {
                    m_in[c].resize(need);
                }
            }
            float* in[2] = { m_in[0].data(), m_in[1].data() };
            readInput(in, need);
            const float* cin[2] = { in[0], in[1] };
            m_stretcher->process(cin, need, false);
        }
    }
    // into the playback's buffer, the gain ramped over the block
    const float from = m_gainNow;
    const float to = m_gain;
    for (size_t i = 0; i < n; ++i) {
        const float g = from + (to - from) * float(i + 1) / float(n);
        if (outCh == 1) {
            buffer[i] = 0.5f * (out[0][i] + out[1][i]) * g;
        } else {
            buffer[i * outCh] = out[0][i] * g;
            buffer[i * outCh + 1] = out[1][i] * g;
            for (unsigned c = 2; c < outCh; ++c) {
                buffer[i * outCh + c] = 0.f;
            }
        }
    }
    m_gainNow = to;
    return samplesPerChannel;
}

void StarScorePcmSource::seek(const msecs_t newPositionMsecs, const bool)
{
    // (the playback's time runs at the speed: a second of it is `speed` seconds of the recording)
    m_pos = double(newPositionMsecs) / 1000000.0 * m_speed * m_rate;
    flush();
}

void StarScorePcmSource::flush()
{
    makeStretcher();
}

void StarScorePcmSource::applyInputParams(const AudioInputParams& requiredParams)
{
    m_params = requiredParams;
    auto num = [&](const char* key, double def) {
        auto it = requiredParams.configuration.find(key);
        if (it == requiredParams.configuration.end()) {
            return def;
        }
        try {
            return std::stod(it->second);
        } catch (...) {
            return def;
        }
    };
    const double speed = std::clamp(num("speed", 1.0), 0.1, 2.0);
    const double pitch = std::clamp(num("pitch", 0.0), -24.0, 24.0);
    m_gain = float(std::clamp(num("gain", 1.0), 0.0, 2.0));
    if (std::abs(speed - m_speed) > 1e-9 || std::abs(pitch - m_pitch) > 1e-9) {
        // (a new speed keeps the place in the recording: the player seeks to the matching time after)
        m_speed = speed;
        m_pitch = pitch;
        if (m_stretcher && !straight()) {
            const double rateRatio = double(m_out.sampleRate) / double(m_rate);
            m_stretcher->setTimeRatio(rateRatio / m_speed);
            m_stretcher->setPitchScale((1.0 / rateRatio) * std::pow(2.0, m_pitch / 12.0));
        } else {
            makeStretcher();
        }
    }
    m_paramsChanged.send(m_params);
}
