/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — decoding a recording for Practice › Listen (1.27.0): into floats at the file's own rate.
 * On the Mac, Apple's own decoders read everything the Mac plays (WAV, AIFF, MP3, M4A/AAC, ALAC, FLAC, CAF, and the
 * sound of an MP4 or MOV). Elsewhere: WAV, AIFF and MP3 (minimp3, CC0).
 */
#pragma once

#include <QString>

#include <memory>
#include <vector>

namespace mu::project::starscore {
struct DecodedAudio {
    int sampleRate = 0;
    int channels = 0;                                  // 1 or 2 (more are mixed down to 2)
    std::shared_ptr<std::vector<float> > samples;      // interleaved
    QString error;
    double seconds() const { return sampleRate > 0 && channels > 0 && samples ? double(samples->size()) / channels / sampleRate : 0.0; }
};

DecodedAudio decodeAudio(const QString& path);

//! The Mac's decoders (starscoredecode_mac.cpp); false when not on a Mac or it couldn't read the file
bool decodeAudioMac(const QString& path, DecodedAudio& out);
}
