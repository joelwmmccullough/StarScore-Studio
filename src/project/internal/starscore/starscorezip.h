/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * A small zip reader and writer for .starscore files that hold big media (1.26.0, Joel 5 Nov 2026: a song's stems
 * and video are stored inside its .starscore, "same as the reference PDFs"). MuseScore's own zip classes read a
 * whole file into memory and write the whole file again for every entry; these stream instead:
 * - Reader maps the file and finds entries from its central directory; an entry can be copied out to a file
 *   without being held in memory.
 * - Writer copies entries from a Reader byte for byte (nothing unpacked or packed again), stores new entries
 *   uncompressed (audio and video don't compress), and streams a file on disk into an entry.
 * No ZIP64: a .starscore stays under 4 GB (StarScoreZip::MAX_FILE_BYTES).
 */
#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>

#include <vector>

namespace mu::project::starscore::zip {
//! The biggest .starscore StarScore writes (the zip's 32-bit sizes and offsets end at 4 GB)
constexpr qint64 MAX_FILE_BYTES = qint64(3900) * 1024 * 1024;

struct Entry {
    QString name;
    quint16 method = 0;          // 0 stored, 8 deflated
    quint16 flags = 0;
    quint32 crc = 0;
    qint64 compressedSize = 0;
    qint64 size = 0;
    quint16 dosTime = 0;
    quint16 dosDate = 0;
    qint64 localHeaderOffset = 0;
    quint16 versionMade = (3 << 8) | 20;      // made on Unix (MuseScore reads the file type from the Unix mode)
    quint32 externalAttr = quint32(0100644) << 16;   // a regular file
    bool isDir() const { return name.endsWith('/'); }
};

quint32 crc32(quint32 crc, const char* data, qint64 len);

//! A copy of a .starscore for Version History: without its recordings and video (they stay in the song itself and in the
//! Autosave Archive), so a copy costs only the score. A file without media is copied as it is.
bool copyWithoutMedia(const QString& from, const QString& to);

class Reader
{
public:
    explicit Reader(const QString& path);
    ~Reader();

    bool ok() const { return m_ok; }
    const std::vector<Entry>& entries() const { return m_entries; }
    const Entry* find(const QString& name) const;

    //! The entry's bytes as stored in the file (compressed when it is), or nullptr
    const char* rawData(const Entry& e) const;
    //! A stored (uncompressed) entry's contents; a deflated one only when zlib is there. Empty on failure.
    QByteArray read(const Entry& e) const;
    //! A stored entry copied into a file, streamed, its CRC checked
    bool copyTo(const Entry& e, const QString& path) const;

private:
    QFile m_file;
    const uchar* m_map = nullptr;
    qint64 m_size = 0;
    std::vector<Entry> m_entries;
    bool m_ok = false;
};

class Writer
{
public:
    explicit Writer(const QString& path);
    ~Writer();

    bool ok() const { return m_ok; }
    //! An entry of another zip, copied as it is stored there
    bool addRaw(const Reader& from, const Entry& e);
    //! New data, stored uncompressed
    bool addData(const QString& name, const QByteArray& data);
    //! A file on disk, streamed into a stored entry
    bool addFile(const QString& name, const QString& sourcePath);
    //! Writes the central directory; false when anything failed
    bool close();
    qint64 bytesWritten() const { return m_pos; }
    //! The checksum of the last entry added
    quint32 lastCrc() const { return m_entries.empty() ? 0 : m_entries.back().crc; }

private:
    bool writeLocal(const Entry& e);
    bool write(const char* data, qint64 len);
    void stamp(Entry& e) const;

    QFile m_file;
    std::vector<Entry> m_entries;
    qint64 m_pos = 0;
    bool m_ok = false;
    bool m_closed = false;
};
}
