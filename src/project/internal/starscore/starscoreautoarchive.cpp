/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Autosave archive (Joel, 7 Oct 2026).
 *
 * Before a save replaces a .starscore file, the version on disk is kept: at most once every 10 minutes per song, and
 * only when it differs from the newest kept version. The copy goes to ~/StarScore Studio/Autosave Archive/Incoming
 * (a plain file copy, so saving doesn't slow down), and a background job packs it into the song's history file,
 * Autosave Archive/<song>.starhistory, then deletes the plain copy.
 *
 * The history file (Joel, 7 Oct 2026: "compressing together all of the files in the archive"):
 * a .starscore is a zip of XML files, each compressed on its own, so two versions of a song share nothing a
 * compressor can see. The history file stores each version's XML uncompressed-then-zstd-compressed, and each version
 * as a patch on the version before it (zstd's "patch from" mode: the earlier version is the dictionary), so a version
 * costs only what changed. Every 30 versions (or when a patch would be big) a version is stored whole, so opening one
 * never replays more than 30 patches. Measured on five real GIJO saves: 15.0 MB as files, 4.2 MB here; one 10-minute
 * change of GIJO is 2-250 KB instead of 4 MB. Opening a version takes well under a second.
 *
 * Layout: "STARHIST1\n", the index's length (8 bytes, little-endian), the index (JSON), then the frames. The index
 * lists each version: time, the frame's place and size, the version it is a patch on (or none), its size and
 * SHA-1 before compression, the song file's size and SHA-1, and its zip entries (name, size) in order. The file is
 * rewritten whole each time (it is small) through a temporary file, so it is never left half-written.
 *
 * Thinning (newest version kept per period): every version from the last 2 hours, then one per hour for the last
 * day, one per day for the last 30 days, one per week for the last 26 weeks, and one per month after that, kept
 * for good. Only versions inside the history files are ever dropped.
 */
#include "starscoreautoarchive.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutex>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QThreadPool>

#include <map>
#include <set>

#include "global/serialization/zipreader.h"
#include "global/serialization/zipwriter.h"
#include "global/types/bytearray.h"

#include "../../thirdparty/zstd/zstd.h"

#include "log.h"

namespace mu::project::starscore {
namespace {
const char* TIME_FORMAT = "yyyy-MM-dd HHmm";
const QByteArray MAGIC("STARHIST1\n");
const int LEVEL = 9;            // measured: as small as level 15 within a few percent, at 1/20 the time
const int MAX_CHAIN = 30;       // patches in a row before a whole version
const char* HISTORY_SUFFIX = ".starhistory";

QMutex s_packMutex;

struct Entry {
    QString name;
    qint64 size = 0;
};

struct Version {
    QDateTime time;
    int ref = -1;               // index of the version this is a patch on (always the one before), or -1: whole
    qint64 offset = 0;
    qint64 length = 0;
    qint64 rawSize = 0;
    QByteArray rawSha1;
    qint64 fileSize = 0;
    QByteArray fileSha1;
    std::vector<Entry> entries;
};

struct History {
    std::vector<Version> versions;   // oldest first
    QByteArray frames;
};

QString incomingDir()
{
    return autosaveArchiveRoot() + "/Incoming";
}

QString historyPath(const QString& song)
{
    return autosaveArchiveRoot() + "/" + song + HISTORY_SUFFIX;
}

QByteArray sha1Of(const QByteArray& data)
{
    return QCryptographicHash::hash(data, QCryptographicHash::Sha1);
}

QByteArray fileSha1(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QByteArray();
    }
    QCryptographicHash h(QCryptographicHash::Sha1);
    h.addData(&f);
    return h.result();
}

// --- zstd -------------------------------------------------------------------------------------------------------

int windowLogFor(qint64 bytes)
{
    int log = 20;
    while ((qint64(1) << log) < bytes && log < 30) {
        ++log;
    }
    return log;
}

QByteArray compress(const QByteArray& src, const QByteArray* ref)
{
    ZSTD_CCtx* c = ZSTD_createCCtx();
    if (!c) {
        return QByteArray();
    }
    ZSTD_CCtx_setParameter(c, ZSTD_c_compressionLevel, LEVEL);
    ZSTD_CCtx_setParameter(c, ZSTD_c_windowLog, windowLogFor(src.size() + (ref ? ref->size() : 0)));
    ZSTD_CCtx_setParameter(c, ZSTD_c_enableLongDistanceMatching, 1);
    ZSTD_CCtx_setParameter(c, ZSTD_c_checksumFlag, 1);
    if (ref && !ref->isEmpty()) {
        ZSTD_CCtx_refPrefix(c, ref->constData(), size_t(ref->size()));
    }
    QByteArray out(qsizetype(ZSTD_compressBound(size_t(src.size()))), Qt::Uninitialized);
    const size_t n = ZSTD_compress2(c, out.data(), size_t(out.size()), src.constData(), size_t(src.size()));
    ZSTD_freeCCtx(c);
    if (ZSTD_isError(n)) {
        LOGE() << "autosave archive: zstd: " << ZSTD_getErrorName(n);
        return QByteArray();
    }
    out.resize(qsizetype(n));
    return out;
}

bool decompress(const QByteArray& frame, const QByteArray* ref, qint64 rawSize, QByteArray& out)
{
    ZSTD_DCtx* d = ZSTD_createDCtx();
    if (!d) {
        return false;
    }
    ZSTD_DCtx_setParameter(d, ZSTD_d_windowLogMax, 31);
    if (ref && !ref->isEmpty()) {
        ZSTD_DCtx_refPrefix(d, ref->constData(), size_t(ref->size()));
    }
    out = QByteArray(qsizetype(rawSize), Qt::Uninitialized);
    const size_t n = ZSTD_decompressDCtx(d, out.data(), size_t(out.size()), frame.constData(), size_t(frame.size()));
    ZSTD_freeDCtx(d);
    if (ZSTD_isError(n) || qint64(n) != rawSize) {
        LOGE() << "autosave archive: couldn't unpack a version" << (ZSTD_isError(n) ? ZSTD_getErrorName(n) : "");
        return false;
    }
    return true;
}

// --- .starscore <-> the XML it holds ----------------------------------------------------------------------------

bool readSong(const QString& path, QByteArray& raw, std::vector<Entry>& entries)
{
    muse::ZipReader zip{ muse::io::path_t(path) };
    if (zip.hasError()) {
        return false;
    }
    raw.clear();
    entries.clear();
    for (const muse::ZipReader::FileInfo& fi : zip.fileInfoList()) {
        if (!fi.isFile) {
            continue;
        }
        const muse::ByteArray data = zip.fileData(fi.filePath.toStdString());
        entries.push_back({ fi.filePath.toQString(), qint64(data.size()) });
        raw.append(reinterpret_cast<const char*>(data.constData()), qsizetype(data.size()));
    }
    return !entries.empty() && !zip.hasError();
}

bool writeSong(const QString& path, const QByteArray& raw, const std::vector<Entry>& entries)
{
    const QString part = path + ".part";
    QFile::remove(part);
    {
        muse::ZipWriter zip{ muse::io::path_t(part) };
        qint64 pos = 0;
        for (const Entry& e : entries) {
            if (pos + e.size > raw.size()) {
                return false;
            }
            zip.addFile(e.name.toStdString(), muse::ByteArray(reinterpret_cast<const uint8_t*>(raw.constData() + pos), size_t(e.size)));
            pos += e.size;
        }
        zip.close();
        if (zip.hasError()) {
            QFile::remove(part);
            return false;
        }
    }
    QFile::remove(path);
    return QFile::rename(part, path);
}

// --- the history file -------------------------------------------------------------------------------------------

bool readIndex(QFile& f, std::vector<Version>& versions, qint64& framesStart)
{
    if (f.read(MAGIC.size()) != MAGIC) {
        return false;
    }
    const QByteArray lenBytes = f.read(8);
    if (lenBytes.size() != 8) {
        return false;
    }
    qint64 len = 0;
    for (int i = 7; i >= 0; --i) {
        len = (len << 8) | quint8(lenBytes[i]);
    }
    if (len <= 0 || len > 64 * 1024 * 1024) {
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.read(len));
    framesStart = MAGIC.size() + 8 + len;
    versions.clear();
    for (const QJsonValue& v : doc.object().value("versions").toArray()) {
        const QJsonObject o = v.toObject();
        Version ver;
        ver.time = QDateTime::fromString(o.value("time").toString(), TIME_FORMAT);
        ver.ref = o.value("ref").toInt(-1);
        ver.offset = qint64(o.value("offset").toDouble());
        ver.length = qint64(o.value("length").toDouble());
        ver.rawSize = qint64(o.value("rawSize").toDouble());
        ver.rawSha1 = QByteArray::fromHex(o.value("rawSha1").toString().toLatin1());
        ver.fileSize = qint64(o.value("fileSize").toDouble());
        ver.fileSha1 = QByteArray::fromHex(o.value("fileSha1").toString().toLatin1());
        for (const QJsonValue& e : o.value("entries").toArray()) {
            const QJsonArray a = e.toArray();
            ver.entries.push_back({ a.at(0).toString(), qint64(a.at(1).toDouble()) });
        }
        if (!ver.time.isValid()) {
            return false;
        }
        versions.push_back(std::move(ver));
    }
    return true;
}

bool readHistoryIndex(const QString& path, std::vector<Version>& versions)
{
    QFile f(path);
    qint64 start = 0;
    return f.open(QIODevice::ReadOnly) && readIndex(f, versions, start);
}

bool readHistory(const QString& path, History& h)
{
    QFile f(path);
    qint64 start = 0;
    if (!f.open(QIODevice::ReadOnly) || !readIndex(f, h.versions, start)) {
        return false;
    }
    h.frames = f.readAll();
    for (const Version& v : h.versions) {
        if (v.offset < 0 || v.offset + v.length > h.frames.size()) {
            return false;
        }
    }
    return true;
}

bool writeHistory(const QString& path, const History& h)
{
    QJsonArray versions;
    for (const Version& v : h.versions) {
        QJsonArray entries;
        for (const Entry& e : v.entries) {
            entries.append(QJsonArray { e.name, double(e.size) });
        }
        versions.append(QJsonObject {
            { "time", v.time.toString(TIME_FORMAT) }, { "ref", v.ref }, { "offset", double(v.offset) },
            { "length", double(v.length) }, { "rawSize", double(v.rawSize) },
            { "rawSha1", QString::fromLatin1(v.rawSha1.toHex()) }, { "fileSize", double(v.fileSize) },
            { "fileSha1", QString::fromLatin1(v.fileSha1.toHex()) }, { "entries", entries } });
    }
    const QByteArray index = QJsonDocument(QJsonObject { { "versions", versions } }).toJson(QJsonDocument::Compact);
    QByteArray len(8, 0);
    for (int i = 0; i < 8; ++i) {
        len[i] = char((qint64(index.size()) >> (8 * i)) & 0xff);
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) {
        return false;
    }
    out.write(MAGIC);
    out.write(len);
    out.write(index);
    out.write(h.frames);
    return out.commit();
}

//! Version i's XML: from the nearest whole version before it, through each patch
bool unpackVersion(const History& h, int i, QByteArray& raw)
{
    int start = i;
    while (start >= 0 && h.versions[start].ref >= 0) {
        if (h.versions[start].ref != start - 1) {
            return false;
        }
        --start;
    }
    if (start < 0) {
        return false;
    }
    QByteArray prev;
    for (int k = start; k <= i; ++k) {
        const Version& v = h.versions[k];
        QByteArray out;
        if (!decompress(h.frames.mid(v.offset, v.length), k == start ? nullptr : &prev, v.rawSize, out)
            || sha1Of(out) != v.rawSha1) {
            return false;
        }
        prev = out;
    }
    raw = prev;
    return true;
}

//! Which period a version of this age belongs to; the newest version in each period is kept
QString periodOf(const QDateTime& t, const QDateTime& now)
{
    const qint64 mins = t.secsTo(now) / 60;
    if (mins < 120) {
        return "all " + t.toString(TIME_FORMAT);
    }
    if (mins < 24 * 60) {
        return "hour " + t.toString("yyyy-MM-dd HH");
    }
    if (mins < 30 * 24 * 60) {
        return "day " + t.toString("yyyy-MM-dd");
    }
    if (mins < 26 * 7 * 24 * 60) {
        int year = 0;
        const int week = t.date().weekNumber(&year);
        return QString("week %1-%2").arg(year).arg(week);
    }
    return "month " + t.toString("yyyy-MM");
}

struct Waiting {
    QDateTime time;
    QString path;
};

//! Copies waiting in Incoming: song -> copies, oldest first
std::map<QString, std::vector<Waiting> > waitingCopies()
{
    static const QRegularExpression stamp("^(.+) (\\d{4}-\\d\\d-\\d\\d \\d{4})\\.starscore$");
    std::map<QString, std::vector<Waiting> > out;
    for (const QFileInfo& fi : QDir(incomingDir()).entryInfoList({ "*.starscore" }, QDir::Files)) {
        const QRegularExpressionMatch m = stamp.match(fi.fileName());
        const QDateTime t = m.hasMatch() ? QDateTime::fromString(m.captured(2), TIME_FORMAT) : QDateTime();
        if (t.isValid()) {
            out[m.captured(1)].push_back({ t, fi.absoluteFilePath() });
        }
    }
    for (auto& [song, list] : out) {
        std::sort(list.begin(), list.end(), [](const Waiting& a, const Waiting& b) { return a.time < b.time; });
    }
    return out;
}

//! Adds the song's waiting copies to its history and thins it. Returns how many copies went in, or -1 when the
//! history couldn't be read or written (the copies then stay in Incoming).
int packSong(const QString& song, const std::vector<Waiting>& waiting, const QDateTime& now)
{
    const QString path = historyPath(song);
    History old;
    if (QFile::exists(path) && !readHistory(path, old)) {
        LOGE() << "autosave archive: couldn't read " << path.toStdString() << "; left as it is";
        return -1;
    }

    // everything, oldest first: the versions already in the history, then the waiting copies
    struct Item {
        int oldIndex = -1;
        const Waiting* copy = nullptr;
        QDateTime time;
    };
    std::vector<Item> items;
    for (int i = 0; i < int(old.versions.size()); ++i) {
        items.push_back({ i, nullptr, old.versions[i].time });
    }
    for (const Waiting& w : waiting) {
        if (items.empty() || w.time > items.back().time) {
            items.push_back({ -1, &w, w.time });
        }
    }

    // the newest item in each period is kept
    std::vector<bool> keep(items.size(), false);
    std::set<QString> periods;
    for (int i = int(items.size()) - 1; i >= 0; --i) {
        keep[i] = periods.insert(periodOf(items[i].time, now)).second;
    }

    History out;
    QByteArray prevOldRaw;      // the old version before this one (patches are on it)
    QByteArray lastKeptRaw;
    QByteArray lastKeptFileSha1;
    int lastKeptOld = -1;       // the old index of the last kept version, -1 when it was a new copy (or none)
    int chain = 0;              // patches since the last whole version
    qint64 lastWholeSize = 0;
    int added = 0;
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& it = items[i];
        QByteArray raw;
        Version v;
        if (it.oldIndex >= 0) {
            v = old.versions[it.oldIndex];
            const QByteArray frame = old.frames.mid(v.offset, v.length);
            if (!decompress(frame, v.ref >= 0 ? &prevOldRaw : nullptr, v.rawSize, raw) || sha1Of(raw) != v.rawSha1) {
                LOGE() << "autosave archive: " << path.toStdString() << " is damaged; left as it is";
                return -1;
            }
            prevOldRaw = raw;
        } else {
            if (!readSong(it.copy->path, raw, v.entries)) {
                LOGW() << "autosave archive: not a song file: " << it.copy->path.toStdString();
                continue;
            }
            v.time = it.time;
            v.rawSize = raw.size();
            v.rawSha1 = sha1Of(raw);
            v.fileSize = QFileInfo(it.copy->path).size();
            v.fileSha1 = fileSha1(it.copy->path);
            if (!lastKeptFileSha1.isEmpty() && v.fileSha1 == lastKeptFileSha1) {
                continue;   // the same as the version before it
            }
        }
        if (!keep[i]) {
            continue;
        }

        QByteArray frame;
        bool whole = false;
        if (it.oldIndex >= 0 && v.ref < 0) {
            frame = old.frames.mid(v.offset, v.length);   // a whole version: kept as it is
            whole = true;
        } else if (it.oldIndex >= 0 && v.ref == lastKeptOld && lastKeptOld >= 0 && chain < MAX_CHAIN) {
            frame = old.frames.mid(v.offset, v.length);   // a patch on the version still before it
        } else if (!lastKeptRaw.isEmpty() && chain < MAX_CHAIN) {
            frame = compress(raw, &lastKeptRaw);
            if (lastWholeSize > 0 && frame.size() > lastWholeSize / 2) {
                // a big change: stored whole when that costs little more, so later patches start fresh
                const QByteArray full = compress(raw, nullptr);
                if (!full.isEmpty() && full.size() <= frame.size() * 3 / 2) {
                    frame = full;
                    whole = true;
                }
            }
        } else {
            frame = compress(raw, nullptr);
            whole = true;
        }
        if (frame.isEmpty()) {
            return -1;
        }
        if (whole) {
            v.ref = -1;
            chain = 0;
            lastWholeSize = frame.size();
        } else {
            v.ref = int(out.versions.size()) - 1;
            ++chain;
        }
        v.offset = out.frames.size();
        v.length = frame.size();
        out.frames.append(frame);
        out.versions.push_back(v);
        lastKeptRaw = raw;
        lastKeptFileSha1 = v.fileSha1;
        lastKeptOld = it.oldIndex;
        if (it.oldIndex < 0) {
            ++added;
        }
    }

    QDir().mkpath(autosaveArchiveRoot());
    if (!writeHistory(path, out)) {
        LOGE() << "autosave archive: couldn't write " << path.toStdString();
        return -1;
    }
    // check it reads back before letting the waiting copies go
    History check;
    if (!readHistory(path, check) || check.versions.size() != out.versions.size()
        || (!check.versions.empty() && !unpackVersion(check, int(check.versions.size()) - 1, lastKeptRaw))) {
        LOGE() << "autosave archive: " << path.toStdString() << " didn't read back; the copies stay in Incoming";
        return -1;
    }
    for (const Waiting& w : waiting) {
        QFile::remove(w.path);
    }
    LOGI() << "autosave archive: " << song.toStdString() << ": " << added << " added, " << out.versions.size()
           << " versions, " << out.frames.size() << " bytes";
    return added;
}
}

QString autosaveArchiveRoot()
{
    return QDir::homePath() + "/StarScore Studio/Autosave Archive";
}

bool autosaveArchiveEnabled()
{
    return QSettings().value("StarScore/autosaveArchive", true).toBool();
}

QString archiveBeforeSave(const QString& songPath, const QDateTime& now, bool background)
{
    if (!autosaveArchiveEnabled() || !songPath.endsWith(".starscore", Qt::CaseInsensitive)) {
        return QString();
    }
    const QFileInfo song(songPath);
    if (!song.exists() || song.size() == 0) {
        return QString();
    }
    const QString root = autosaveArchiveRoot();
    if (song.absoluteFilePath().startsWith(root + "/")) {
        return QString();   // a version opened from the archive and saved: not archived again
    }

    // the newest version kept: waiting in Incoming, else the newest in the history file
    const QString base = song.completeBaseName();
    QDateTime newest;
    QByteArray newestSha1;
    const auto waiting = waitingCopies();
    if (auto it = waiting.find(base); it != waiting.end() && !it->second.empty()) {
        newest = it->second.back().time;
        newestSha1 = fileSha1(it->second.back().path);
    } else {
        std::vector<Version> versions;
        if (readHistoryIndex(historyPath(base), versions) && !versions.empty()) {
            newest = versions.back().time;
            newestSha1 = versions.back().fileSha1;
        }
    }
    if (newest.isValid()) {
        if (newest.secsTo(now) < 10 * 60) {
            return QString();   // one version per 10 minutes at most
        }
        if (newestSha1 == fileSha1(songPath)) {
            return QString();   // nothing changed since then
        }
    }

    if (!QDir().mkpath(incomingDir())) {
        LOGW() << "autosave archive: couldn't make " << incomingDir().toStdString();
        return QString();
    }
    const QString target = incomingDir() + "/" + base + " " + now.toString(TIME_FORMAT) + ".starscore";
    const QString part = target + ".part";
    QFile::remove(part);
    if (QFile::exists(target) || !QFile::copy(songPath, part) || !QFile::rename(part, target)) {
        LOGW() << "autosave archive: couldn't copy to " << target.toStdString();
        QFile::remove(part);
        return QString();
    }
    if (background) {
        packIncomingInBackground();
    }
    return target;
}

int packIncoming(const QDateTime& now)
{
    QMutexLocker lock(&s_packMutex);
    int packed = 0;
    for (const auto& [song, list] : waitingCopies()) {
        packed += std::max(0, packSong(song, list, now));
    }
    return packed;
}

void packIncomingInBackground()
{
    QThreadPool::globalInstance()->start([]() { packIncoming(); });
}

QStringList archivedSongs()
{
    std::map<QString, QDateTime> newest;
    for (const QFileInfo& fi : QDir(autosaveArchiveRoot()).entryInfoList({ QString("*") + HISTORY_SUFFIX }, QDir::Files)) {
        newest[fi.completeBaseName()] = fi.lastModified();
    }
    for (const auto& [song, list] : waitingCopies()) {
        if (!list.empty() && (!newest.count(song) || newest[song] < list.back().time)) {
            newest[song] = list.back().time;
        }
    }
    std::vector<std::pair<QDateTime, QString> > sorted;
    for (const auto& [song, t] : newest) {
        sorted.emplace_back(t, song);
    }
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    QStringList out;
    for (const auto& [t, song] : sorted) {
        out << song;
    }
    return out;
}

std::vector<ArchivedVersion> archivedVersions(const QString& song)
{
    std::vector<ArchivedVersion> out;
    std::vector<Version> versions;
    if (readHistoryIndex(historyPath(song), versions)) {
        for (const Version& v : versions) {
            out.push_back({ v.time, v.fileSize, v.length, false });
        }
    }
    const auto waiting = waitingCopies();
    if (auto it = waiting.find(song); it != waiting.end()) {
        for (const Waiting& w : it->second) {
            if (std::none_of(out.begin(), out.end(), [&](const ArchivedVersion& a) { return a.time == w.time; })) {
                out.push_back({ w.time, QFileInfo(w.path).size(), 0, true });
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const ArchivedVersion& a, const ArchivedVersion& b) { return a.time > b.time; });
    return out;
}

qint64 archivedBytes(const QString& song)
{
    qint64 total = QFileInfo(historyPath(song)).size();
    const auto waiting = waitingCopies();
    if (auto it = waiting.find(song); it != waiting.end()) {
        for (const Waiting& w : it->second) {
            total += QFileInfo(w.path).size();
        }
    }
    return total;
}

QString extractVersion(const QString& song, const QDateTime& time, const QString& targetIn, QString* error)
{
    QString target = targetIn;
    if (target.isEmpty()) {
        QDir().mkpath(autosaveArchiveRoot() + "/Opened");
        target = autosaveArchiveRoot() + "/Opened/" + song + " (autosave " + time.toString(TIME_FORMAT) + ").starscore";
    }
    // still waiting in Incoming: the copy itself
    const auto waiting = waitingCopies();
    if (auto it = waiting.find(song); it != waiting.end()) {
        for (const Waiting& w : it->second) {
            if (w.time == time) {
                QFile::remove(target);
                if (QFile::copy(w.path, target)) {
                    return target;
                }
            }
        }
    }
    History h;
    {
        QMutexLocker lock(&s_packMutex);
        if (!readHistory(historyPath(song), h)) {
            if (error) {
                *error = "The archive for this song couldn't be read.";
            }
            return QString();
        }
    }
    for (int i = 0; i < int(h.versions.size()); ++i) {
        if (h.versions[i].time != time) {
            continue;
        }
        QByteArray raw;
        if (!unpackVersion(h, i, raw) || !writeSong(target, raw, h.versions[i].entries)) {
            if (error) {
                *error = "This version couldn't be unpacked.";
            }
            return QString();
        }
        return target;
    }
    if (error) {
        *error = "This version is no longer in the archive.";
    }
    return QString();
}

int extractAllVersions(const QString& song, const QString& folder)
{
    QDir().mkpath(folder);
    int n = 0;
    for (const ArchivedVersion& v : archivedVersions(song)) {
        const QString target = folder + "/" + song + " " + v.time.toString(TIME_FORMAT) + ".starscore";
        if (!extractVersion(song, v.time, target).isEmpty()) {
            ++n;
        }
    }
    return n;
}
}
