/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — decoding a recording (see starscoredecode.h)
 */
#include "starscoredecode.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#define MINIMP3_IMPLEMENTATION
#define MINIMP3_FLOAT_OUTPUT
#include "../../thirdparty/minimp3/minimp3.h"
#include "../../thirdparty/minimp3/minimp3_ex.h"

namespace mu::project::starscore {
namespace {
quint16 le16(const uchar* p) { return quint16(p[0] | (p[1] << 8)); }
quint32 le32(const uchar* p) { return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24); }
quint16 be16(const uchar* p) { return quint16((p[0] << 8) | p[1]); }
quint32 be32(const uchar* p) { return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]); }

//! 80-bit IEEE extended (AIFF's sample rate)
double ext80(const uchar* p)
{
    const int expon = ((p[0] & 0x7F) << 8) | p[1];
    quint64 mant = 0;
    for (int i = 0; i < 8; ++i) {
        mant = (mant << 8) | p[2 + i];
    }
    if (expon == 0 && mant == 0) {
        return 0;
    }
    return std::ldexp(double(mant), expon - 16383 - 63);
}

//! PCM samples into floats, more than two channels folded to two
void pcmToFloat(const uchar* d, qint64 frames, int ch, int bits, bool isFloat, bool bigEndian, DecodedAudio& out)
{
    const int bytes = bits / 8;
    const int outCh = std::min(ch, 2);
    out.channels = outCh;
    out.samples = std::make_shared<std::vector<float> >(size_t(frames) * size_t(outCh), 0.f);
    float* o = out.samples->data();
    auto sample = [&](const uchar* p) -> float {
        if (isFloat && bits == 32) {
            quint32 u = bigEndian ? be32(p) : le32(p);
            float f;
            std::memcpy(&f, &u, 4);
            return f;
        }
        if (isFloat && bits == 64) {
            quint64 u = 0;
            for (int i = 0; i < 8; ++i) {
                u |= quint64(p[bigEndian ? 7 - i : i]) << (8 * i);
            }
            double f;
            std::memcpy(&f, &u, 8);
            return float(f);
        }
        switch (bits) {
        case 8: return bigEndian ? float(int8_t(p[0])) / 128.f : (float(p[0]) - 128.f) / 128.f;
        case 16: return float(int16_t(bigEndian ? be16(p) : le16(p))) / 32768.f;
        case 24: {
            int32_t v = bigEndian ? (int32_t(p[0]) << 24) | (int32_t(p[1]) << 16) | (int32_t(p[2]) << 8)
                        : (int32_t(p[2]) << 24) | (int32_t(p[1]) << 16) | (int32_t(p[0]) << 8);
            return float(v) / 2147483648.f;
        }
        case 32: return float(int32_t(bigEndian ? be32(p) : le32(p))) / 2147483648.f;
        default: return 0.f;
        }
    };
    for (qint64 f = 0; f < frames; ++f) {
        const uchar* fp = d + f * ch * bytes;
        if (ch == 1) {
            o[f] = sample(fp);
        } else {
            float l = 0, r = 0;
            for (int c = 0; c < ch; ++c) {
                const float v = sample(fp + c * bytes);
                if (c % 2 == 0) {
                    l += v;
                } else {
                    r += v;
                }
            }
            const float n = float((ch + 1) / 2);
            o[f * 2] = l / n;
            o[f * 2 + 1] = r / std::max(1.f, float(ch / 2));
        }
    }
}

bool decodeWav(const QByteArray& b, DecodedAudio& out)
{
    const uchar* d = reinterpret_cast<const uchar*>(b.constData());
    const qint64 n = b.size();
    if (n < 12 || std::memcmp(d, "RIFF", 4) != 0 || std::memcmp(d + 8, "WAVE", 4) != 0) {
        return false;
    }
    int fmt = 0, ch = 0, rate = 0, bits = 0;
    for (qint64 p = 12; p + 8 <= n;) {
        const quint32 size = le32(d + p + 4);
        if (std::memcmp(d + p, "fmt ", 4) == 0 && p + 8 + 16 <= n) {
            fmt = le16(d + p + 8);
            ch = le16(d + p + 10);
            rate = int(le32(d + p + 12));
            bits = le16(d + p + 22);
            if (fmt == 0xFFFE && size >= 40) {
                fmt = le16(d + p + 8 + 24);   // WAVE_FORMAT_EXTENSIBLE: the sub-format
            }
        } else if (std::memcmp(d + p, "data", 4) == 0 && fmt != 0) {
            const qint64 len = std::min<qint64>(size, n - p - 8);
            if ((fmt != 1 && fmt != 3) || ch < 1 || bits < 8) {
                out.error = "unsupported WAV encoding";
                return true;
            }
            out.sampleRate = rate;
            pcmToFloat(d + p + 8, len / (ch * (bits / 8)), ch, bits, fmt == 3, false, out);
            return true;
        }
        p += 8 + size + (size & 1);
    }
    out.error = "no audio in the WAV file";
    return true;
}

bool decodeAiff(const QByteArray& b, DecodedAudio& out)
{
    const uchar* d = reinterpret_cast<const uchar*>(b.constData());
    const qint64 n = b.size();
    if (n < 12 || std::memcmp(d, "FORM", 4) != 0 || (std::memcmp(d + 8, "AIFF", 4) != 0 && std::memcmp(d + 8, "AIFC", 4) != 0)) {
        return false;
    }
    const bool aifc = std::memcmp(d + 8, "AIFC", 4) == 0;
    int ch = 0, bits = 0;
    double rate = 0;
    bool isFloat = false, little = false;
    for (qint64 p = 12; p + 8 <= n;) {
        const quint32 size = be32(d + p + 4);
        if (std::memcmp(d + p, "COMM", 4) == 0 && p + 8 + 18 <= n) {
            ch = be16(d + p + 8);
            bits = be16(d + p + 14);
            rate = ext80(d + p + 16);
            if (aifc && size >= 22) {
                isFloat = std::memcmp(d + p + 26, "fl32", 4) == 0 || std::memcmp(d + p + 26, "FL32", 4) == 0;
                little = std::memcmp(d + p + 26, "sowt", 4) == 0;
            }
        } else if (std::memcmp(d + p, "SSND", 4) == 0 && ch > 0) {
            const quint32 offset = be32(d + p + 8);
            const qint64 start = p + 16 + offset;
            const qint64 len = std::min<qint64>(qint64(size) - 8 - offset, n - start);
            out.sampleRate = int(std::lround(rate));
            pcmToFloat(d + start, len / (ch * (bits / 8)), ch, bits, isFloat, !little, out);
            return true;
        }
        p += 8 + size + (size & 1);
    }
    out.error = "no audio in the AIFF file";
    return true;
}

bool decodeMp3(const QString& path, DecodedAudio& out)
{
    mp3dec_t dec;
    mp3dec_file_info_t info;
    std::memset(&info, 0, sizeof(info));
    if (mp3dec_load(&dec, QFile::encodeName(path).constData(), &info, nullptr, nullptr) != 0 || info.samples == 0) {
        free(info.buffer);
        return false;
    }
    out.sampleRate = info.hz;
    out.channels = std::min(info.channels, 2);
    out.samples = std::make_shared<std::vector<float> >(info.buffer, info.buffer + info.samples);
    free(info.buffer);
    return true;
}
}

DecodedAudio decodeAudio(const QString& path)
{
    DecodedAudio out;
    if (decodeAudioMac(path, out)) {
        return out;
    }
    const QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "mp3") {
        if (!decodeMp3(path, out)) {
            out.error = "couldn't read the MP3 file";
        }
        return out;
    }
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        out.error = "couldn't open the file";
        return out;
    }
    const QByteArray b = f.readAll();
    if (decodeWav(b, out) || decodeAiff(b, out)) {
        return out;
    }
    out.error = QString("can't read .%1 files here").arg(ext);
    return out;
}

#ifndef Q_OS_MACOS
bool decodeAudioMac(const QString&, DecodedAudio&)
{
    return false;
}
#endif
}
