/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Streaming zip reader and writer for .starscore files holding media (see starscorezip.h).
 */
#include "starscorezip.h"

#include <algorithm>

#include <QDateTime>
#include <QFileInfo>

#if __has_include(<zlib.h>)
#include <zlib.h>
#define STARSCORE_ZIP_ZLIB 1
#endif

#include "log.h"

namespace mu::project::starscore::zip {
namespace {
quint16 le16(const uchar* p)
{
    return quint16(p[0]) | quint16(quint16(p[1]) << 8);
}

quint32 le32(const uchar* p)
{
    return quint32(p[0]) | (quint32(p[1]) << 8) | (quint32(p[2]) << 16) | (quint32(p[3]) << 24);
}

void put16(QByteArray& b, quint16 v)
{
    b.append(char(v & 0xff));
    b.append(char((v >> 8) & 0xff));
}

void put32(QByteArray& b, quint32 v)
{
    for (int i = 0; i < 4; ++i) {
        b.append(char((v >> (8 * i)) & 0xff));
    }
}

const quint32 LOCAL_SIG = 0x04034b50;
const quint32 CENTRAL_SIG = 0x02014b50;
const quint32 END_SIG = 0x06054b50;
const quint16 UTF8_NAMES = 0x0800;
const quint16 DATA_DESCRIPTOR = 0x0008;
const qint64 CHUNK = 4 * 1024 * 1024;
}

quint32 crc32(quint32 crc, const char* data, qint64 len)
{
    static quint32 table[256];
    static const bool ready = []() {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) {
                c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        return true;
    }();
    Q_UNUSED(ready);
    crc = ~crc;
    const uchar* p = reinterpret_cast<const uchar*>(data);
    for (qint64 i = 0; i < len; ++i) {
        crc = table[(crc ^ p[i]) & 0xff] ^ (crc >> 8);
    }
    return ~crc;
}

// ---------------------------------------------------------------------------------------------------------------------
//  Reader
// ---------------------------------------------------------------------------------------------------------------------

Reader::Reader(const QString& path)
    : m_file(path)
{
    if (!m_file.open(QIODevice::ReadOnly)) {
        return;
    }
    m_size = m_file.size();
    if (m_size < 22) {
        return;
    }
    m_map = m_file.map(0, m_size);
    if (!m_map) {
        return;
    }
    // the end-of-directory record: within the last 64 KB (its comment can be that long)
    qint64 end = -1;
    for (qint64 pos = m_size - 22; pos >= 0 && pos >= m_size - 22 - 65535; --pos) {
        if (le32(m_map + pos) == END_SIG) {
            end = pos;
            break;
        }
    }
    if (end < 0) {
        return;
    }
    const quint16 count = le16(m_map + end + 10);
    qint64 pos = qint64(le32(m_map + end + 16));
    for (int i = 0; i < count; ++i) {
        if (pos + 46 > m_size || le32(m_map + pos) != CENTRAL_SIG) {
            return;
        }
        const uchar* h = m_map + pos;
        Entry e;
        e.versionMade = le16(h + 4);
        e.flags = le16(h + 8);
        e.method = le16(h + 10);
        e.dosTime = le16(h + 12);
        e.dosDate = le16(h + 14);
        e.crc = le32(h + 16);
        e.compressedSize = le32(h + 20);
        e.size = le32(h + 24);
        const quint16 nameLen = le16(h + 28);
        const quint16 extraLen = le16(h + 30);
        const quint16 commentLen = le16(h + 32);
        e.externalAttr = le32(h + 38);
        e.localHeaderOffset = le32(h + 42);
        if (pos + 46 + nameLen > m_size) {
            return;
        }
        e.name = QString::fromUtf8(reinterpret_cast<const char*>(h + 46), nameLen);
        m_entries.push_back(e);
        pos += 46 + nameLen + extraLen + commentLen;
    }
    m_ok = true;
}

Reader::~Reader()
{
    if (m_map) {
        m_file.unmap(const_cast<uchar*>(m_map));
    }
}

const Entry* Reader::find(const QString& name) const
{
    for (const Entry& e : m_entries) {
        if (e.name == name) {
            return &e;
        }
    }
    return nullptr;
}

const char* Reader::rawData(const Entry& e) const
{
    if (!m_map || e.localHeaderOffset + 30 > m_size || le32(m_map + e.localHeaderOffset) != LOCAL_SIG) {
        return nullptr;
    }
    const uchar* h = m_map + e.localHeaderOffset;
    const qint64 start = e.localHeaderOffset + 30 + le16(h + 26) + le16(h + 28);
    if (start + e.compressedSize > m_size) {
        return nullptr;
    }
    return reinterpret_cast<const char*>(m_map + start);
}

QByteArray Reader::read(const Entry& e) const
{
    const char* raw = rawData(e);
    if (!raw) {
        return {};
    }
    if (e.method == 0) {
        QByteArray out(raw, qsizetype(e.size));
        if (crc32(0, out.constData(), out.size()) != e.crc) {
            LOGW() << "[starscore] zip: " << e.name << " is damaged (its checksum doesn't match)";
            return {};
        }
        return out;
    }
#ifdef STARSCORE_ZIP_ZLIB
    if (e.method == 8) {
        QByteArray out(qsizetype(e.size), Qt::Uninitialized);
        z_stream z {};
        if (inflateInit2(&z, -MAX_WBITS) != Z_OK) {
            return {};
        }
        z.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw));
        z.avail_in = uInt(e.compressedSize);
        z.next_out = reinterpret_cast<Bytef*>(out.data());
        z.avail_out = uInt(out.size());
        const int r = inflate(&z, Z_FINISH);
        inflateEnd(&z);
        if (r != Z_STREAM_END || qint64(z.total_out) != e.size) {
            return {};
        }
        return out;
    }
#endif
    return {};
}

bool Reader::copyTo(const Entry& e, const QString& path) const
{
    const char* raw = rawData(e);
    if (!raw) {
        return false;
    }
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    if (e.method != 0) {
        const QByteArray data = read(e);
        return !data.isEmpty() || e.size == 0 ? out.write(data) == data.size() : false;
    }
    quint32 crc = 0;
    for (qint64 done = 0; done < e.size; done += CHUNK) {
        const qint64 n = std::min(CHUNK, e.size - done);
        crc = crc32(crc, raw + done, n);
        if (out.write(raw + done, n) != n) {
            return false;
        }
    }
    if (crc != e.crc) {
        LOGW() << "[starscore] zip: " << e.name << " is damaged (its checksum doesn't match)";
        out.close();
        QFile::remove(path);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
//  Writer
// ---------------------------------------------------------------------------------------------------------------------

Writer::Writer(const QString& path)
    : m_file(path)
{
    m_ok = m_file.open(QIODevice::WriteOnly | QIODevice::Truncate);
}

Writer::~Writer()
{
    if (!m_closed) {
        close();
    }
}

bool Writer::write(const char* data, qint64 len)
{
    if (!m_ok) {
        return false;
    }
    if (m_pos + len > MAX_FILE_BYTES || m_file.write(data, len) != len) {
        m_ok = false;
        return false;
    }
    m_pos += len;
    return true;
}

void Writer::stamp(Entry& e) const
{
    const QDateTime now = QDateTime::currentDateTime();
    const QDate d = now.date();
    const QTime t = now.time();
    e.dosTime = quint16((t.hour() << 11) | (t.minute() << 5) | (t.second() / 2));
    e.dosDate = quint16(((std::max(d.year(), 1980) - 1980) << 9) | (d.month() << 5) | d.day());
}

bool Writer::writeLocal(const Entry& e)
{
    const QByteArray name = e.name.toUtf8();
    QByteArray h;
    put32(h, LOCAL_SIG);
    put16(h, 20);
    put16(h, e.flags);
    put16(h, e.method);
    put16(h, e.dosTime);
    put16(h, e.dosDate);
    put32(h, e.crc);
    put32(h, quint32(e.compressedSize));
    put32(h, quint32(e.size));
    put16(h, quint16(name.size()));
    put16(h, 0);
    h.append(name);
    return write(h.constData(), h.size());
}

bool Writer::addRaw(const Reader& from, const Entry& src)
{
    const char* raw = from.rawData(src);
    if (!raw || !m_ok) {
        m_ok = false;
        return false;
    }
    Entry e = src;
    e.flags = quint16((src.flags & ~DATA_DESCRIPTOR) | UTF8_NAMES);
    e.localHeaderOffset = m_pos;
    if (!writeLocal(e)) {
        return false;
    }
    for (qint64 done = 0; done < e.compressedSize; done += CHUNK) {
        if (!write(raw + done, std::min(CHUNK, e.compressedSize - done))) {
            return false;
        }
    }
    m_entries.push_back(e);
    return true;
}

bool Writer::addData(const QString& name, const QByteArray& data)
{
    Entry e;
    e.name = name;
    e.flags = UTF8_NAMES;
    e.method = 0;
    e.crc = crc32(0, data.constData(), data.size());
    e.size = e.compressedSize = data.size();
    e.localHeaderOffset = m_pos;
    stamp(e);
    if (!writeLocal(e) || !write(data.constData(), data.size())) {
        return false;
    }
    m_entries.push_back(e);
    return true;
}

bool Writer::addFile(const QString& name, const QString& sourcePath)
{
    QFile in(sourcePath);
    if (!m_ok || !in.open(QIODevice::ReadOnly)) {
        m_ok = false;
        return false;
    }
    Entry e;
    e.name = name;
    e.flags = UTF8_NAMES;
    e.method = 0;
    e.size = e.compressedSize = in.size();
    e.localHeaderOffset = m_pos;
    stamp(e);
    if (m_pos + 30 + e.name.toUtf8().size() + e.size > MAX_FILE_BYTES) {
        LOGE() << "[starscore] zip: " << name << " would take the file past " << MAX_FILE_BYTES << " bytes";
        m_ok = false;
        return false;
    }
    if (!writeLocal(e)) {
        return false;
    }
    QByteArray buf(qsizetype(CHUNK), Qt::Uninitialized);
    quint32 crc = 0;
    qint64 done = 0;
    while (done < e.size) {
        const qint64 n = in.read(buf.data(), std::min(CHUNK, e.size - done));
        if (n <= 0) {
            m_ok = false;
            return false;
        }
        crc = crc32(crc, buf.constData(), n);
        if (!write(buf.constData(), n)) {
            return false;
        }
        done += n;
    }
    // the CRC, now known, into the local header (14 bytes in)
    e.crc = crc;
    QByteArray c;
    put32(c, crc);
    if (!m_file.seek(e.localHeaderOffset + 14) || m_file.write(c) != 4 || !m_file.seek(m_pos)) {
        m_ok = false;
        return false;
    }
    m_entries.push_back(e);
    return true;
}

bool Writer::close()
{
    if (m_closed) {
        return m_ok;
    }
    m_closed = true;
    if (m_ok) {
        const qint64 dirStart = m_pos;
        QByteArray dir;
        for (const Entry& e : m_entries) {
            const QByteArray name = e.name.toUtf8();
            put32(dir, CENTRAL_SIG);
            put16(dir, e.versionMade);
            put16(dir, 20);
            put16(dir, e.flags);
            put16(dir, e.method);
            put16(dir, e.dosTime);
            put16(dir, e.dosDate);
            put32(dir, e.crc);
            put32(dir, quint32(e.compressedSize));
            put32(dir, quint32(e.size));
            put16(dir, quint16(name.size()));
            put16(dir, 0);
            put16(dir, 0);
            put16(dir, 0);
            put16(dir, 0);
            put32(dir, e.externalAttr);
            put32(dir, quint32(e.localHeaderOffset));
            dir.append(name);
        }
        QByteArray end;
        put32(end, END_SIG);
        put16(end, 0);
        put16(end, 0);
        put16(end, quint16(m_entries.size()));
        put16(end, quint16(m_entries.size()));
        put32(end, quint32(dir.size()));
        put32(end, quint32(dirStart));
        put16(end, 0);
        write(dir.constData(), dir.size());
        write(end.constData(), end.size());
    }
    if (!m_file.flush()) {
        m_ok = false;
    }
    m_file.close();
    return m_ok;
}

bool copyWithoutMedia(const QString& from, const QString& to)
{
    bool hasMedia = false;
    {
        const Reader in(from);
        for (const Entry& e : in.entries()) {
            hasMedia = hasMedia || (e.name.startsWith("StarScoreMedia/") && e.name != "StarScoreMedia/media.json");
        }
        if (in.ok() && hasMedia) {
            Writer out(to);
            for (const Entry& e : in.entries()) {
                if (!e.name.startsWith("StarScoreMedia/") || e.name == "StarScoreMedia/media.json") {
                    out.addRaw(in, e);
                }
            }
            if (out.close()) {
                return true;
            }
            QFile::remove(to);
            return false;
        }
    }
    return QFile::copy(from, to);
}
}
