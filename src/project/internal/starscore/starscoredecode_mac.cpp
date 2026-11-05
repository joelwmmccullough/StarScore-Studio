/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — decoding a recording with the Mac's own decoders (ExtAudioFile, AudioToolbox)
 */
#include "starscoredecode.h"

#include <AudioToolbox/AudioToolbox.h>

#include <QFile>

namespace mu::project::starscore {
bool decodeAudioMac(const QString& path, DecodedAudio& out)
{
    const QByteArray p = QFile::encodeName(path);
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(p.constData()), p.size(), false);
    if (!url) {
        return false;
    }
    ExtAudioFileRef file = nullptr;
    OSStatus st = ExtAudioFileOpenURL(url, &file);
    CFRelease(url);
    if (st != noErr || !file) {
        return false;
    }
    AudioStreamBasicDescription in {};
    UInt32 size = sizeof(in);
    st = ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileDataFormat, &size, &in);
    if (st != noErr || in.mSampleRate <= 0) {
        ExtAudioFileDispose(file);
        return false;
    }
    const int channels = in.mChannelsPerFrame >= 2 ? 2 : 1;
    AudioStreamBasicDescription client {};
    client.mSampleRate = in.mSampleRate;
    client.mFormatID = kAudioFormatLinearPCM;
    client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    client.mChannelsPerFrame = UInt32(channels);
    client.mBitsPerChannel = 32;
    client.mBytesPerFrame = UInt32(4 * channels);
    client.mFramesPerPacket = 1;
    client.mBytesPerPacket = client.mBytesPerFrame;
    st = ExtAudioFileSetProperty(file, kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client);
    if (st != noErr) {
        ExtAudioFileDispose(file);
        return false;
    }
    SInt64 frames = 0;
    size = sizeof(frames);
    ExtAudioFileGetProperty(file, kExtAudioFileProperty_FileLengthFrames, &size, &frames);
    auto samples = std::make_shared<std::vector<float> >();
    samples->reserve(size_t(std::max<SInt64>(frames, 0)) * size_t(channels) + 8192);
    std::vector<float> buf(size_t(8192) * size_t(channels));
    for (;;) {
        AudioBufferList list;
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = UInt32(channels);
        list.mBuffers[0].mDataByteSize = UInt32(buf.size() * sizeof(float));
        list.mBuffers[0].mData = buf.data();
        UInt32 n = 8192;
        st = ExtAudioFileRead(file, &n, &list);
        if (st != noErr || n == 0) {
            break;
        }
        samples->insert(samples->end(), buf.begin(), buf.begin() + size_t(n) * size_t(channels));
    }
    ExtAudioFileDispose(file);
    if (samples->empty()) {
        return false;
    }
    out.sampleRate = int(std::lround(in.mSampleRate));
    out.channels = channels;
    out.samples = samples;
    return true;
}
}
