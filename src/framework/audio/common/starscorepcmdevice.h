/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a decoded recording handed to the audio engine as a track's data (1.27.0, Practice › Listen).
 * The samples are shared, not copied: the engine's track plays from the same memory (a song's three stems are a few
 * hundred MB). Keep the device alive while its track exists.
 */
#pragma once

#include <memory>
#include <vector>

#include "global/io/iodevice.h"

namespace muse::audio {
class StarScorePcmDevice : public io::IODevice
{
public:
    StarScorePcmDevice(std::shared_ptr<const std::vector<float> > samples, int sampleRate, int channels)
        : m_samples(std::move(samples)), m_sampleRate(sampleRate), m_channels(channels) {}

    const std::shared_ptr<const std::vector<float> >& samples() const { return m_samples; }
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }

protected:
    bool doOpen(OpenMode) override { return true; }
    size_t dataSize() const override { return 0; }
    const uint8_t* rawData() const override { return nullptr; }
    bool resizeData(size_t) override { return false; }
    size_t writeData(const uint8_t*, size_t) override { return 0; }

private:
    std::shared_ptr<const std::vector<float> > m_samples;
    int m_sampleRate = 44100;
    int m_channels = 2;
};
}
