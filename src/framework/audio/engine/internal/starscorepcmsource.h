/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a recording as a track of a playback (1.27.0, Practice › Listen: Joel's stems "stay synced and he
 * swaps between them"; "speed controls are crucial"). Decoded audio, played from the playback's position, slowed down
 * or sped up without changing pitch, and shifted in pitch, by Rubber Band (GPL). Every stem of a song is a track of
 * the same playback, so they run on one clock; switching between them is a gain on each ("gain"), never a seek.
 *
 * Its input params' configuration: "speed" (0.1–2, 1 = as recorded), "pitch" (semitones, may be fractional), "gain"
 * (0–1). The data comes as a StarScorePcmDevice (audio/common/starscorepcmdevice.h, shared), or an IODevice holding a
 * header (StarScorePcmHeader) and the samples, interleaved floats (copied).
 */
#pragma once

#include <memory>
#include <vector>

#include "track.h"

namespace RubberBand {
class RubberBandStretcher;
}

namespace muse::audio::engine {
struct StarScorePcmHeader {
    char magic[8] = { 'S', 'S', 'P', 'C', 'M', 0, 0, 1 };
    int32_t sampleRate = 44100;
    int32_t channels = 2;
    int64_t frames = 0;
};

class StarScorePcmSource : public ITrackAudioInput
{
public:
    //! The data from a device in the format above; nullptr when it isn't that
    static std::shared_ptr<StarScorePcmSource> fromDevice(io::IODevice* device);

    StarScorePcmSource(std::shared_ptr<const std::vector<float> > samples, int sampleRate, int channels);
    ~StarScorePcmSource() override;

    bool isActive() const override { return m_active; }
    void setIsActive(bool active) override { m_active = active; }

    void setOutputSpec(const OutputSpec& spec) override;
    unsigned int audioChannelsCount() const override { return 2; }
    async::Channel<unsigned int> audioChannelsCountChanged() const override { return m_channelsChanged; }
    samples_t process(float* buffer, samples_t samplesPerChannel) override;

    void seek(const msecs_t newPositionMsecs, const bool flushSound = true) override;
    void flush() override;

    const AudioInputParams& inputParams() const override { return m_params; }
    void applyInputParams(const AudioInputParams& requiredParams) override;
    async::Channel<AudioInputParams> inputParamsChanged() const override { return m_paramsChanged; }

    void prepareToPlay() override {}
    bool readyToPlay() const override { return true; }
    async::Notification readyToPlayChanged() const override { return m_readyChanged; }

    bool hasPendingChunks() const override { return false; }
    void processInput() override {}
    InputProcessingProgress inputProcessingProgress() const override { return {}; }

    void clearCache() override {}

private:
    bool straight() const;
    void makeStretcher();
    void readInput(float* const* out, size_t frames);   // from m_pos, silence past the end

    std::shared_ptr<const std::vector<float> > m_samples;
    int m_rate = 44100;
    int m_channels = 2;
    int64_t m_frames = 0;
    double m_pos = 0;            // in the recording's frames
    OutputSpec m_out;
    bool m_active = false;
    double m_speed = 1.0;
    double m_pitch = 0.0;
    float m_gain = 1.0f;
    float m_gainNow = 1.0f;      // (ramped towards m_gain, so switching stems doesn't click)
    AudioInputParams m_params;
    std::unique_ptr<RubberBand::RubberBandStretcher> m_stretcher;
    size_t m_toDrop = 0;   // the stretcher's start delay, still to drop
    std::vector<float> m_in[2];
    std::vector<float> m_outBuf[2];
    async::Channel<unsigned int> m_channelsChanged;
    async::Channel<AudioInputParams> m_paramsChanged;
    async::Notification m_readyChanged;
};
}
