/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice: libraries, progress, and the planner (see starscorepractice.h)
 */
#include "starscorepractice.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <set>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include "global/serialization/zipreader.h"

#if __has_include(<zlib.h>)
#include <zlib.h>
#define STARSCORE_PRACTICE_ZLIB 1
#endif

#include "log.h"

using namespace mu::project::practice;

const Item* Library::item(const QString& id) const
{
    for (const Item& it : items) {
        if (it.id == id) {
            return &it;
        }
    }
    return nullptr;
}

const Solo* Library::solo(const QString& id) const
{
    for (const Solo& s : solos) {
        if (s.id == id) {
            return &s;
        }
    }
    return nullptr;
}

static QStringList strings(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& x : v.toArray()) {
        if (x.isString()) {
            out << x.toString();
        } else if (x.isObject()) {
            const QJsonObject o = x.toObject();
            const QString what = o.value("what").toString();
            const QString bars = o.value("bars").toString();
            out << (bars.isEmpty() ? what : QString("%1, bars %2").arg(what, bars));
        }
    }
    return out;
}

Library mu::project::practice::parseLibrary(const QJsonObject& root, bool withXml)
{
    Library lib;
    lib.code = root.value("song").toString();
    lib.title = root.value("title").toString();
    lib.summary = root.value("summary").toString();
    lib.style = root.value("style").toString();
    lib.instruments = strings(root.value("instruments"));
    const QJsonObject needs = root.value("needs").toObject();
    for (const QString& k : needs.keys()) {
        lib.needs[k] = needs.value(k).toInt();
    }
    for (const QJsonValue& v : root.value("sections").toArray()) {
        const QJsonObject o = v.toObject();
        lib.sections.push_back({ o.value("id").toString(), o.value("name").toString(), o.value("bars").toString(),
                                 o.value("summary").toString() });
    }
    for (const QJsonValue& v : root.value("solos").toArray()) {
        const QJsonObject o = v.toObject();
        lib.solos.push_back({ o.value("id").toString(), o.value("name").toString(), o.value("bars").toString(),
                              o.value("form").toString(), o.value("changes").toString(), o.value("soloist").toString(),
                              o.value("notes").toString() });
    }
    for (const QJsonValue& v : root.value("items").toArray()) {
        const QJsonObject o = v.toObject();
        Item it;
        it.id = o.value("id").toString();
        it.title = o.value("title").toString();
        it.category = o.value("category").toString();
        it.solo = o.value("solo").toString();
        it.sections = strings(o.value("sections"));
        it.level = std::clamp(o.value("level").toInt(1), 1, 3);
        it.minutes = std::clamp(o.value("minutes").toInt(10), 3, 60);
        it.prereq = strings(o.value("prereq"));
        it.goal = o.value("goal").toString();
        it.steps = strings(o.value("steps"));
        const QJsonObject tempo = o.value("tempo").toObject();
        it.tempoStart = tempo.value("start").toInt();
        it.tempoGoal = tempo.value("goal").toInt(it.tempoStart);
        it.moveOnWhen = o.value("moveOnWhen").toString();
        it.listen = o.value("listen").toString();
        it.refs = strings(o.value("refs"));
        if (withXml) {
            const QString xml = o.value("xml").toString();
            if (!xml.isEmpty()) {
                it.xmlPacked = qCompress(xml.toUtf8(), 1);
            }
        }
        const QJsonObject ir = o.value("ireal").toObject();
        it.irealStyle = ir.value("style").toString();
        it.irealKey = ir.value("key").toString();
        it.irealProg = ir.value("prog").toString();
        it.groove = o.value("backing").toObject().value("groove").toString();
        if (!it.id.isEmpty()) {
            lib.items.push_back(it);
        }
    }
    return lib;
}

QString Item::xml() const
{
    return xmlPacked.isEmpty() ? QString() : QString::fromUtf8(qUncompress(xmlPacked));
}

Library mu::project::practice::decodeLibrary(const QByteArray& blob, bool withXml)
{
    const QByteArray packed = QByteArray::fromBase64(blob.trimmed());
    const QByteArray json = qUncompress(packed);
    if (json.isEmpty()) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    return doc.isObject() ? parseLibrary(doc.object(), withXml) : Library();
}

//! The main score's mscx inside a .starscore
static QByteArray mainMscx(const QString& path)
{
    muse::ZipReader zip { muse::io::path_t(path) };
    if (zip.hasError()) {
        return {};
    }
    for (const muse::ZipReader::FileInfo& fi : zip.fileInfoList()) {
        const QString name = fi.filePath.toQString();
        if (fi.isFile && !name.contains('/') && name.endsWith(".mscx")) {
            return zip.fileData(name.toStdString()).toQByteArray();   // (a copy: the data is a temporary)
        }
    }
    return {};
}

static QByteArray metaTagValue(const QByteArray& mscx, const char* name)
{
    const QByteArray open = QByteArray("<metaTag name=\"") + name + "\">";
    const int a = mscx.indexOf(open);
    if (a < 0) {
        return {};
    }
    const int s = a + open.size();
    const int e = mscx.indexOf("</metaTag>", s);
    return e < 0 ? QByteArray() : mscx.mid(s, e - s);
}

#ifdef STARSCORE_PRACTICE_ZLIB
static quint16 le16(const char* p)
{
    return quint16(uchar(p[0])) | quint16(uchar(p[1])) << 8;
}

static quint32 le32(const char* p)
{
    return quint32(le16(p)) | quint32(le16(p + 2)) << 16;
}

//! The practice metaTag's value, read by decompressing only the start of the main score: the metaTags come before the
//! first <Part>, so a 25 MB score needs only its first few KB. Returns nullopt when it can't tell (zip64, an unusual
//! entry, damage), and the caller reads the whole score instead. An empty value means the song has no library.
static std::optional<QByteArray> practiceBlobFromHead(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly) || f.size() < 22) {
        return std::nullopt;
    }
    // end of central directory: the last 22 bytes plus up to 64 KB of comment
    const qint64 tail = std::min<qint64>(f.size(), 22 + 65535);
    f.seek(f.size() - tail);
    const QByteArray end = f.read(tail);
    const int eocd = end.lastIndexOf(QByteArray("PK\x05\x06", 4));
    if (eocd < 0 || eocd + 22 > end.size()) {
        return std::nullopt;
    }
    const quint32 cdSize = le32(end.constData() + eocd + 12);
    const quint32 cdOffset = le32(end.constData() + eocd + 16);
    if (cdOffset == 0xFFFFFFFF || cdSize == 0xFFFFFFFF || qint64(cdOffset) + cdSize > f.size()) {
        return std::nullopt;
    }
    f.seek(cdOffset);
    const QByteArray cd = f.read(cdSize);
    if (cd.size() != int(cdSize)) {
        return std::nullopt;
    }
    // the one top-level .mscx (as mainMscx picks it)
    int method = -1;
    quint32 compSize = 0, localOffset = 0;
    for (int p = 0; p + 46 <= cd.size();) {
        const char* e = cd.constData() + p;
        if (le32(e) != 0x02014b50) {
            return std::nullopt;
        }
        const int nameLen = le16(e + 28), extraLen = le16(e + 30), commentLen = le16(e + 32);
        if (p + 46 + nameLen > cd.size()) {
            return std::nullopt;
        }
        const QByteArray name(e + 46, nameLen);
        if (!name.contains('/') && name.endsWith(".mscx")) {
            method = le16(e + 10);
            compSize = le32(e + 20);
            localOffset = le32(e + 42);
            break;
        }
        p += 46 + nameLen + extraLen + commentLen;
    }
    if ((method != 0 && method != 8) || compSize == 0xFFFFFFFF || localOffset == 0xFFFFFFFF) {
        return std::nullopt;
    }
    f.seek(localOffset);
    const QByteArray local = f.read(30);
    if (local.size() != 30 || le32(local.constData()) != 0x04034b50) {
        return std::nullopt;
    }
    f.seek(qint64(localOffset) + 30 + le16(local.constData() + 26) + le16(local.constData() + 28));

    const QByteArray open = QByteArray("<metaTag name=\"") + PRACTICE_META_TAG + "\">";
    QByteArray text;
    z_stream zs {};
    if (method == 8 && inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
        return std::nullopt;
    }
    std::optional<QByteArray> result;
    QByteArray out(256 * 1024, Qt::Uninitialized);
    bool decided = false;
    qint64 left = compSize;
    int status = Z_OK;
    while (!decided && left > 0 && text.size() < 8 * 1024 * 1024) {
        QByteArray in = f.read(std::min<qint64>(left, 64 * 1024));
        if (in.isEmpty()) {
            break;
        }
        left -= in.size();
        if (method == 0) {
            text += in;
        } else {
            zs.next_in = reinterpret_cast<Bytef*>(in.data());
            zs.avail_in = uInt(in.size());
            do {
                zs.next_out = reinterpret_cast<Bytef*>(out.data());
                zs.avail_out = uInt(out.size());
                status = inflate(&zs, Z_NO_FLUSH);
                if (status != Z_OK && status != Z_STREAM_END) {
                    break;
                }
                text.append(out.constData(), out.size() - int(zs.avail_out));
            } while (zs.avail_in > 0 && status == Z_OK);
            if (status != Z_OK && status != Z_STREAM_END) {
                break;
            }
        }
        const int a = text.indexOf(open);
        if (a >= 0) {
            const int e = text.indexOf("</metaTag>", a + open.size());
            if (e >= 0) {
                result = text.mid(a + open.size(), e - a - open.size());
                decided = true;
            }
        } else if (text.contains("<Part ") || text.contains("<Part>")) {
            result = QByteArray();   // past the metaTags: no library
            decided = true;
        }
        if (status == Z_STREAM_END) {
            break;
        }
    }
    if (method == 8) {
        inflateEnd(&zs);
    }
    return decided ? result : std::nullopt;
}

#endif

Library mu::project::practice::readLibrary(const QString& starscorePath, bool withXml)
{
    QByteArray blob;
#ifdef STARSCORE_PRACTICE_ZLIB
    if (const std::optional<QByteArray> head = practiceBlobFromHead(starscorePath)) {
        blob = *head;
    } else
#endif
    blob = metaTagValue(mainMscx(starscorePath), PRACTICE_META_TAG);
    if (blob.isEmpty()) {
        return {};
    }
    Library lib = decodeLibrary(blob, withXml);
    lib.path = starscorePath;
    return lib;
}

const Library& mu::project::practice::cachedLibrary(const QString& starscorePath)
{
    static std::map<QString, std::pair<QString, Library> > cache;
    const QFileInfo fi(starscorePath);
    const QString stamp = QString("%1|%2").arg(fi.lastModified().toMSecsSinceEpoch()).arg(fi.size());
    auto it = cache.find(starscorePath);
    if (it == cache.end() || it->second.first != stamp) {
        cache[starscorePath] = { stamp, readLibrary(starscorePath) };
        it = cache.find(starscorePath);
    }
    return it->second.second;
}

std::vector<SongEntry> mu::project::practice::scanLibraries(const QString& rootFolder, const QString& cacheFile)
{
    QJsonObject cache;
    {
        QFile f(cacheFile);
        if (f.open(QIODevice::ReadOnly)) {
            cache = QJsonDocument::fromJson(f.readAll()).object();
        }
    }
    QJsonObject newCache;
    std::vector<SongEntry> out;
    if (rootFolder.isEmpty() || !QFileInfo(rootFolder).isDir()) {
        return out;
    }
    QDirIterator it(rootFolder, { "*.starscore" }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        // (the folders the library audit skips, and autosaves)
        static const QStringList SKIP { "/Version History/", "/MuseScore Files/", "Quarantine", "/Templates/", "/Z Empty",
                                        "Superseded", "Autosave" };
        bool skip = false;
        for (const QString& s : SKIP) {
            skip = skip || path.contains(s);
        }
        if (skip) {
            continue;
        }
        const QFileInfo fi(path);
        const QString stamp = QString("%1|%2").arg(fi.lastModified().toMSecsSinceEpoch()).arg(fi.size());
        QJsonObject entry = cache.value(path).toObject();
        if (entry.value("stamp").toString() != stamp || !entry.contains("needs")) {
            const Library lib = readLibrary(path, false);
            int minutes = 0;
            for (const Item& item : lib.items) {
                minutes += item.minutes;
            }
            QJsonObject needs;
            for (const auto& [area, n] : lib.needs) {
                needs[area] = n;
            }
            entry = QJsonObject { { "stamp", stamp }, { "code", lib.code }, { "title", lib.title },
                                  { "items", int(lib.items.size()) }, { "solos", int(lib.solos.size()) }, { "minutes", minutes },
                                  { "needs", needs } };
        }
        newCache[path] = entry;
        if (entry.value("items").toInt() > 0) {
            SongEntry e { entry.value("code").toString(), entry.value("title").toString(), path, entry.value("items").toInt(),
                          entry.value("solos").toInt(), entry.value("minutes").toInt(), {} };
            const QJsonObject needs = entry.value("needs").toObject();
            for (const QString& area : needs.keys()) {
                e.needs[area] = needs.value(area).toInt();
            }
            out.push_back(e);
        }
    }
    // one entry per song code: the newest file (a stray copy outside the skipped folders loses)
    {
        std::map<QString, size_t> byCode;
        std::vector<SongEntry> unique;
        for (const SongEntry& e : out) {
            auto it = byCode.find(e.code);
            if (it == byCode.end()) {
                byCode[e.code] = unique.size();
                unique.push_back(e);
            } else if (QFileInfo(e.path).lastModified() > QFileInfo(unique[it->second].path).lastModified()) {
                unique[it->second] = e;
            }
        }
        out = unique;
    }
    std::sort(out.begin(), out.end(), [](const SongEntry& a, const SongEntry& b) {
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    QDir().mkpath(QFileInfo(cacheFile).absolutePath());
    QSaveFile f(cacheFile);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(newCache).toJson(QJsonDocument::Compact));
        f.commit();
    }
    return out;
}

// ---------------------------------------------------------------- progress
Progress::Progress(const QString& file)
    : m_file(file)
{
    if (m_file.isEmpty()) {
        m_file = practiceFolder() + "/progress.json";
    }
}

void Progress::load()
{
    m_items.clear();
    m_log = QJsonArray();
    QFile f(m_file);
    if (!f.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject items = root.value("items").toObject();
    for (const QString& k : items.keys()) {
        const QJsonObject o = items.value(k).toObject();
        ItemProgress p;
        p.stage = o.value("stage").toInt();
        p.sessions = o.value("sessions").toInt();
        p.minutes = o.value("minutes").toInt();
        p.bestTempo = o.value("bestTempo").toInt();
        p.last = QDate::fromString(o.value("last").toString(), Qt::ISODate);
        p.note = o.value("note").toString();
        m_items[k] = p;
    }
    m_log = root.value("log").toArray();
}

bool Progress::save() const
{
    QJsonObject items;
    for (const auto& [k, p] : m_items) {
        items[k] = QJsonObject { { "stage", p.stage }, { "sessions", p.sessions }, { "minutes", p.minutes }, { "bestTempo", p.bestTempo },
                                 { "last", p.last.toString(Qt::ISODate) }, { "note", p.note } };
    }
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile f(m_file);
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(QJsonDocument(QJsonObject { { "items", items }, { "log", m_log } }).toJson());
    return f.commit();
}

ItemProgress Progress::get(const QString& code, const QString& itemId) const
{
    auto it = m_items.find(code + "/" + itemId);
    return it == m_items.end() ? ItemProgress() : it->second;
}

void Progress::record(const QString& code, const QString& itemId, int minutes, int tempo, int stage, const QString& note,
                      const QDate& date)
{
    ItemProgress& p = m_items[code + "/" + itemId];
    p.sessions += 1;
    p.minutes += minutes;
    p.bestTempo = std::max(p.bestTempo, tempo);
    if (stage >= 0) {
        p.stage = stage;
    }
    p.last = date;
    if (!note.isEmpty()) {
        p.note = note;
    }
    m_log.append(QJsonObject { { "date", date.toString(Qt::ISODate) }, { "song", code }, { "item", itemId }, { "minutes", minutes },
                               { "tempo", tempo }, { "stage", stage }, { "note", note } });
}

// ---------------------------------------------------------------- plans (JSON)
QJsonObject Plan::toJson() const
{
    QJsonArray s;
    for (const PlanSong& ps : songs) {
        QJsonObject needsObj;
        for (const auto& [k, v] : ps.needs) {
            needsObj[k] = v;
        }
        s.append(QJsonObject { { "code", ps.code }, { "title", ps.title }, { "set", ps.set }, { "needs", needsObj } });
    }
    QJsonArray d;
    for (const Day& day : days) {
        QJsonArray b;
        for (const Block& bl : day.blocks) {
            b.append(QJsonObject { { "code", bl.code }, { "item", bl.itemId }, { "kind", bl.kind }, { "minutes", bl.minutes },
                                   { "tempo", bl.tempo }, { "done", bl.done }, { "tempoReached", bl.tempoReached }, { "stage", bl.stage } });
        }
        d.append(QJsonObject { { "date", day.date.toString(Qt::ISODate) }, { "minutes", day.minutes }, { "label", day.label },
                               { "blocks", b } });
    }
    return QJsonObject { { "id", id }, { "name", name }, { "kind", kind }, { "created", created.toString(Qt::ISODate) },
                         { "showDate", showDate.toString(Qt::ISODate) }, { "songs", s }, { "days", d } };
}

Plan Plan::fromJson(const QJsonObject& o)
{
    Plan p;
    p.id = o.value("id").toString();
    p.name = o.value("name").toString();
    p.kind = o.value("kind").toString("routine");
    p.created = QDate::fromString(o.value("created").toString(), Qt::ISODate);
    p.showDate = QDate::fromString(o.value("showDate").toString(), Qt::ISODate);
    for (const QJsonValue& v : o.value("songs").toArray()) {
        const QJsonObject so = v.toObject();
        PlanSong ps;
        ps.code = so.value("code").toString();
        ps.title = so.value("title").toString();
        ps.set = so.value("set").toInt();
        const QJsonObject n = so.value("needs").toObject();
        for (const QString& k : n.keys()) {
            ps.needs[k] = n.value(k).toInt();
        }
        p.songs.push_back(ps);
    }
    for (const QJsonValue& v : o.value("days").toArray()) {
        const QJsonObject dO = v.toObject();
        Day day;
        day.date = QDate::fromString(dO.value("date").toString(), Qt::ISODate);
        day.minutes = dO.value("minutes").toInt();
        day.label = dO.value("label").toString();
        for (const QJsonValue& bv : dO.value("blocks").toArray()) {
            const QJsonObject b = bv.toObject();
            Block bl;
            bl.code = b.value("code").toString();
            bl.itemId = b.value("item").toString();
            bl.kind = b.value("kind").toString("item");
            bl.minutes = b.value("minutes").toInt();
            bl.tempo = b.value("tempo").toInt();
            bl.done = b.value("done").toBool();
            bl.tempoReached = b.value("tempoReached").toInt();
            bl.stage = b.value("stage").toInt(-1);
            day.blocks.push_back(bl);
        }
        p.days.push_back(day);
    }
    return p;
}

// ---------------------------------------------------------------- the planner
static QString areaOf(const QString& category)
{
    if (category == "solo" || category == "vocabulary") {
        return "solos";
    }
    if (category == "comping") {
        return "comping";
    }
    if (category == "parts" || category == "form" || category == "performance") {
        return "parts";
    }
    return "harmony";   // harmony, voicings, technique, ear
}

namespace {
struct Sim {
    int stage = 0;
    int scheduled = 0;
    QDate lastDay;
    int bestTempo = 0;
};

int tempoFor(const Item& it, const Sim& s)
{
    if (it.tempoStart <= 0) {
        return 0;
    }
    const int goal = std::max(it.tempoGoal, it.tempoStart);
    static const double FRAC[] = { 0.0, 0.4, 0.75, 1.0, 1.0 };
    int t = int(std::lround(it.tempoStart + (goal - it.tempoStart) * FRAC[std::clamp(s.stage, 0, 4)]));
    if (s.bestTempo > 0) {
        t = std::max(t, std::min(goal, s.bestTempo + 4));
    }
    return std::min(goal, t);
}
}

Plan mu::project::practice::makePlan(const PlanRequest& req, const std::map<QString, Library>& libs, const Progress& progress,
                                     const Plan* existing, const QDate& fromDate)
{
    Plan plan;
    plan.name = req.name;
    plan.kind = req.kind;
    plan.created = existing ? existing->created : QDate::currentDate();
    plan.id = existing ? existing->id : QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    plan.showDate = req.showDate;
    plan.songs = req.songs;

    // the simulated state of every item: real progress, then each scheduled session moves it on a stage
    std::map<QString, Sim> sim;
    auto key = [](const QString& code, const QString& id) { return code + "/" + id; };
    for (const PlanSong& ps : req.songs) {
        auto lib = libs.find(ps.code);
        if (lib == libs.end()) {
            continue;
        }
        for (const Item& it : lib->second.items) {
            const ItemProgress p = progress.get(ps.code, it.id);
            Sim s;
            s.stage = p.stage;
            s.lastDay = p.last;
            s.bestTempo = p.bestTempo;
            sim[key(ps.code, it.id)] = s;
        }
    }
    // kept days (before fromDate) count as practised
    if (existing && fromDate.isValid()) {
        for (const Day& d : existing->days) {
            if (d.date < fromDate) {
                plan.days.push_back(d);
                for (const Block& b : d.blocks) {
                    Sim& s = sim[key(b.code, b.itemId)];
                    if (!b.done) {
                        continue;   // real progress already has what was done
                    }
                    s.scheduled += 1;
                    s.lastDay = d.date;
                }
            }
        }
    }

    // song weights: 1, plus the needs, plus twice the biggest need (so a song with one big need, such as a solo and
    // nothing else, still gets real time)
    std::map<QString, double> weight;
    double totalWeight = 0;
    for (const PlanSong& ps : req.songs) {
        if (!libs.count(ps.code)) {
            continue;
        }
        double w = 1.0;
        int most = 0;
        for (const auto& [k, v] : ps.needs) {
            w += v;
            most = std::max(most, v);
        }
        w += 2 * most;
        weight[ps.code] = w;
        totalWeight += w;
    }
    if (totalWeight <= 0) {
        return plan;
    }
    std::map<QString, double> debt;   // minutes owed to each song
    // what each song has had lately (the progress log, and the kept days of a plan planned again), so a routine made
    // today or a plan planned again doesn't start over with the same songs: owed more the longer since it was played,
    // less the more it had in the last two weeks
    {
        const QDate start = req.days.empty() ? QDate::currentDate() : req.days.front().first;
        std::map<QString, int> recent;
        std::map<QString, QDate> lastPlayed;
        for (const QJsonValue& v : progress.log()) {
            const QJsonObject o = v.toObject();
            const QDate d = QDate::fromString(o.value("date").toString(), Qt::ISODate);
            const QString code = o.value("song").toString();
            if (!d.isValid() || !weight.count(code)) {
                continue;
            }
            if (d.daysTo(start) <= 14 && d <= start) {
                recent[code] += o.value("minutes").toInt();
            }
            if (!lastPlayed[code].isValid() || d > lastPlayed[code]) {
                lastPlayed[code] = d;
            }
        }
        for (const Day& d : plan.days) {   // the kept days (before fromDate)
            for (const Block& b : d.blocks) {
                if (weight.count(b.code)) {
                    if (!b.done) {
                        continue;
                    }
                    if (!lastPlayed[b.code].isValid() || d.date > lastPlayed[b.code]) {
                        lastPlayed[b.code] = d.date;
                    }
                }
            }
        }
        double avgWeight = totalWeight / std::max<size_t>(1, weight.size());
        for (const auto& [code, w] : weight) {
            const int since = lastPlayed[code].isValid() ? std::clamp(int(lastPlayed[code].daysTo(start)), 0, 10) : 10;
            debt[code] = since * 3.0 * w / avgWeight - recent[code] * 0.5;
        }
    }
    // the run-through day: the last day before the show with time on it (a plan of three days or more)
    QDate runThroughDate;
    if (req.kind == "plan" && req.days.size() > 2) {
        for (auto it = req.days.rbegin(); it != req.days.rend(); ++it) {
            const bool show = req.showDate.isValid() && it->first == req.showDate;
            if (!show && it->second >= 5 && (!req.showDate.isValid() || it->first < req.showDate)) {
                runThroughDate = it->first;
                break;
            }
        }
    }

    // set order for the day's blocks
    std::vector<PlanSong> ordered = req.songs;
    std::stable_sort(ordered.begin(), ordered.end(), [](const PlanSong& a, const PlanSong& b) {
        return (a.set ? a.set : 99) < (b.set ? b.set : 99);
    });

    const int nDays = int(req.days.size());
    for (int di = 0; di < nDays; ++di) {
        const QDate date = req.days[di].first;
        const int dayMinutes = req.days[di].second;
        if (fromDate.isValid() && date < fromDate) {
            continue;
        }
        Day day;
        day.date = date;
        day.minutes = dayMinutes;
        const bool isShow = req.kind == "plan" && req.showDate.isValid() && date == req.showDate;
        const bool runThrough = !isShow && runThroughDate.isValid() && date == runThroughDate;
        int left = dayMinutes;
        std::set<QString> today;
        auto schedule = [&](const QString& code, const Item& it, int minutes, const QString& kind) {
            Sim& s = sim[key(code, it.id)];
            Block b;
            b.code = code;
            b.itemId = it.id;
            b.kind = kind;
            b.minutes = minutes;
            b.tempo = tempoFor(it, s);
            day.blocks.push_back(b);
            s.scheduled += 1;
            // an idea to understand (no exercise, no tempo) is usually solid after one session; a skill takes three
            const bool isConcept = !it.hasXml() && it.tempoStart <= 0 && it.category != "ear" && it.category != "performance"
                                 && it.category != "technique";
            s.stage = std::min(3, s.stage + (isConcept ? 2 : 1));
            s.lastDay = date;
            today.insert(key(code, it.id));
            left -= minutes;
        };
        auto prereqsMet = [&](const QString& code, const Item& it) {
            for (const QString& p : it.prereq) {
                auto f = sim.find(key(code, p));
                if (f == sim.end()) {
                    continue;
                }
                if (f->second.stage < 1 && !today.count(key(code, p))) {
                    return false;
                }
            }
            return true;
        };

        if (isShow) {
            day.label = "Show day";
        } else if (runThrough) {
            day.label = "Run-through day";
        }

        // warm-up: a short played harmony, technique, or voicing item (scales, arpeggios, voicings in time), from the
        // songs in turn, the least practised first
        if (left >= 45 && !isShow && !ordered.empty()) {
            const int n = int(ordered.size());
            for (int k = 0; k < n; ++k) {
                const PlanSong& ps = ordered[size_t((date.toJulianDay() + k) % n)];   // (by the date: a new song each day)
                auto lib = libs.find(ps.code);
                if (lib == libs.end()) {
                    continue;
                }
                const Item* best = nullptr;
                int bestKey = 1 << 30;
                for (const Item& it : lib->second.items) {
                    if (!(it.category == "harmony" || it.category == "technique" || it.category == "voicings") || it.level > 2
                        || !it.hasXml() || !prereqsMet(ps.code, it) || today.count(key(ps.code, it.id))) {
                        continue;
                    }
                    const Sim& s = sim[key(ps.code, it.id)];
                    if (s.stage >= 3) {
                        continue;
                    }
                    const int k2 = s.scheduled * 10 + it.level;
                    if (k2 < bestKey) {
                        bestKey = k2;
                        best = &it;
                    }
                }
                if (best) {
                    schedule(ps.code, *best, std::min(10, best->minutes), "warmup");
                    break;
                }
            }
        }

        if (runThrough || isShow) {
            // performance and form items for every song, in set order; then the rest of the time on the weakest items
            const int per = std::max(5, left / std::max(1, int(ordered.size())));   // (the show day: the minutes chosen for it)
            for (const PlanSong& ps : ordered) {
                auto lib = libs.find(ps.code);
                if (lib == libs.end() || left < 5) {
                    continue;
                }
                int budget = std::min(per, left);
                for (const char* cat : { "performance", "form" }) {
                    for (const Item& it : lib->second.items) {
                        if (it.category == cat && budget >= 5 && !today.count(key(ps.code, it.id))) {
                            const int m = std::min(budget, it.minutes);
                            schedule(ps.code, it, m, "runthrough");
                            budget -= m;
                            break;
                        }
                    }
                }
                if (isShow) {
                    continue;
                }
            }
        }

        if (!isShow && left >= 5) {
            // each song is owed minutes every day by its weight; a day works on the songs owed most (a handful, about
            // 25 minutes each, so a song gets a real session and the day isn't 12 songs of a few items), and what a song
            // gets comes off what it's owed
            for (const auto& [code, w] : weight) {
                debt[code] += std::max(0, left) * w / totalWeight;
            }
            const PlanSong* psOf = nullptr;
            auto planSong = [&](const QString& code) -> const PlanSong* {
                psOf = nullptr;
                for (const PlanSong& p : req.songs) {
                    if (p.code == code) {
                        psOf = &p;
                    }
                }
                return psOf;
            };
            // the best item of a song for today (nullptr: nothing left to do)
            std::map<QString, std::map<QString, int> > areaUsed;
            auto bestItem = [&](const QString& code) -> const Item* {
                const Library& lib = libs.at(code);
                const PlanSong* ps = planSong(code);
                const Item* best = nullptr;
                double bestScore = -1e9;
                for (const Item& it : lib.items) {
                    const Sim& s = sim[key(code, it.id)];
                    if (today.count(key(code, it.id)) || (it.category == "performance" && !runThrough && s.stage < 1
                                                          && lib.items.size() > 20 && di < nDays - 2)) {
                        continue;
                    }
                    if (!prereqsMet(code, it)) {
                        continue;
                    }
                    const QString area = areaOf(it.category);
                    const int need = ps && ps->needs.count(area) ? ps->needs.at(area) : 1;
                    if (need <= 0 && it.category != "performance") {
                        continue;
                    }
                    double score = need * 10.0 - it.level * 3.0 - areaUsed[code][area] * 6.0;
                    const int since = s.lastDay.isValid() ? s.lastDay.daysTo(date) : 99;
                    if (s.stage >= 4) {
                        score -= 40;
                    } else if (s.stage >= 3) {
                        score -= since >= 4 ? 8 : 30;   // solid: a review now and then
                    } else if (s.scheduled > 0) {
                        score += since >= 2 ? 4 : -4;   // learning: back every other day or so
                    }
                    score -= s.scheduled * 1.5;
                    if (score > bestScore) {
                        bestScore = score;
                        best = &it;
                    }
                }
                return best;
            };
            std::vector<QString> codes;
            for (const PlanSong& ps : ordered) {
                if (weight.count(ps.code)) {
                    codes.push_back(ps.code);
                }
            }
            std::stable_sort(codes.begin(), codes.end(), [&](const QString& x, const QString& y) { return debt[x] > debt[y] + 1e-9; });
            const int maxSongs = std::clamp(int(std::lround(left / 25.0)), 1, std::max(1, int(codes.size())));
            std::vector<QString> chosen(codes.begin(), codes.begin() + std::min<size_t>(maxSongs, codes.size()));
            size_t nextCandidate = chosen.size();
            while (left >= 5) {
                // the chosen song owed most after what it already got today
                QString pick;
                double most = -1e18;
                for (const QString& c : chosen) {
                    const double owed = debt[c];
                    if (owed > most) {
                        most = owed;
                        pick = c;
                    }
                }
                if (pick.isEmpty()) {
                    if (nextCandidate < codes.size()) {
                        chosen.push_back(codes[nextCandidate++]);
                        continue;
                    }
                    break;
                }
                const Item* it = bestItem(pick);
                if (!it) {
                    chosen.erase(std::remove(chosen.begin(), chosen.end(), pick), chosen.end());
                    continue;
                }
                const int m = std::min(it->minutes, left);
                if (m < 3) {
                    today.insert(key(pick, it->id));   // too long for what's left: another item, or another song
                    continue;
                }
                schedule(pick, *it, m, "item");
                areaUsed[pick][areaOf(it->category)] += 1;
                debt[pick] -= m;
            }
        }
        // the day in set order, warm-up first; within a song, in learning order (the form, then the harmony and
        // voicings, then comping and parts, then the solo, then run-throughs), each item after its prerequisites
        {
            std::map<QString, const Item*> itemOf;
            for (const Block& b : day.blocks) {
                auto lib = libs.find(b.code);
                if (lib != libs.end()) {
                    itemOf[key(b.code, b.itemId)] = lib->second.item(b.itemId);
                }
            }
            auto songRank = [&](const Block& x) {
                if (x.kind == "warmup") {
                    return -1;
                }
                for (size_t i = 0; i < ordered.size(); ++i) {
                    if (ordered[i].code == x.code) {
                        return int(i);
                    }
                }
                return 999;
            };
            auto stepRank = [&](const Block& x) {
                static const QStringList ORDER { "form", "harmony", "technique", "ear", "voicings", "parts", "comping",
                                                 "solo", "vocabulary", "performance" };
                const Item* it = itemOf[key(x.code, x.itemId)];
                if (x.kind == "runthrough" || !it) {
                    return 100;
                }
                const QString c = it->category == "vocabulary" ? QString("solo") : it->category;   // (licks and solo items interleave)
                const int r = int(ORDER.indexOf(c));
                return r < 0 ? 50 : r;
            };
            std::stable_sort(day.blocks.begin(), day.blocks.end(), [&](const Block& a, const Block& b) {
                const int sa = songRank(a), sb = songRank(b);
                return sa != sb ? sa < sb : stepRank(a) < stepRank(b);
            });
            // a prerequisite scheduled the same day goes before the item that needs it (with a limit on moves, in
            // case a library's prerequisites go round in a circle)
            size_t moves = 0;
            const size_t maxMoves = day.blocks.size() * day.blocks.size();
            for (size_t i = 0; i < day.blocks.size() && moves <= maxMoves; ++i) {
                const Item* it = itemOf[key(day.blocks[i].code, day.blocks[i].itemId)];
                if (!it) {
                    continue;
                }
                for (size_t j = i + 1; j < day.blocks.size(); ++j) {
                    if (day.blocks[j].code == day.blocks[i].code && it->prereq.contains(day.blocks[j].itemId)) {
                        const Block pre = day.blocks[j];
                        day.blocks.erase(day.blocks.begin() + j);
                        day.blocks.insert(day.blocks.begin() + i, pre);
                        ++moves;
                        i = size_t(-1);   // (start over: the move can expose another)
                        break;
                    }
                }
            }
        }
        plan.days.push_back(day);
    }
    return plan;
}

// ---------------------------------------------------------------- saved plans
QString mu::project::practice::libraryRoot(const QString& libraryFolder)
{
    const QString test = qEnvironmentVariable("STARSCORE_AUTOTEST_PRACTICE");
    return test.isEmpty() ? libraryFolder : test;
}

QString mu::project::practice::practiceFolder()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/practice";
    QDir().mkpath(dir + "/plans");
    return dir;
}

static QString planFile(const QString& id)
{
    QString safe = id;
    safe.replace(QRegularExpression("[^A-Za-z0-9_-]"), "_");
    return practiceFolder() + "/plans/" + safe + ".json";
}

std::vector<Plan> mu::project::practice::loadPlans()
{
    std::vector<Plan> out;
    QDir dir(practiceFolder() + "/plans");
    for (const QFileInfo& fi : dir.entryInfoList({ "*.json" }, QDir::Files, QDir::Time)) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (doc.isObject()) {
            Plan p = Plan::fromJson(doc.object());
            if (!p.id.isEmpty()) {
                out.push_back(p);
            }
        }
    }
    return out;
}

Plan mu::project::practice::loadPlan(const QString& id)
{
    QFile f(planFile(id));
    if (id.isEmpty() || !f.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    return doc.isObject() ? Plan::fromJson(doc.object()) : Plan();
}

bool mu::project::practice::savePlan(const Plan& plan)
{
    if (plan.id.isEmpty()) {
        return false;
    }
    QSaveFile f(planFile(plan.id));
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(QJsonDocument(plan.toJson()).toJson());
    return f.commit();
}

bool mu::project::practice::deletePlan(const QString& id)
{
    return !id.isEmpty() && QFile::remove(planFile(id));
}

// ---------------------------------------------------------------- iReal
static QString urlQuote(const QString& s)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(s, "/:=*[]|,^+-.()"));   // ("#" encoded: a sharp isn't a fragment)
}

static QString esc(const QString& s)
{
    return s.toHtmlEscaped();
}

QString mu::project::practice::irealHtml(const Plan& plan, const std::map<QString, Library>& libs, int dayIndex)
{
    QString body;
    for (int di = 0; di < int(plan.days.size()); ++di) {
        if (dayIndex >= 0 && di != dayIndex) {
            continue;
        }
        const Day& d = plan.days[di];
        QStringList all;
        QString links;
        int n = 0;
        for (const Block& b : d.blocks) {
            auto lib = libs.find(b.code);
            const Item* it = lib == libs.end() ? nullptr : lib->second.item(b.itemId);
            if (!it || it->irealProg.isEmpty()) {
                continue;
            }
            ++n;
            QString title = QString("%1 %2 - %3").arg(n, 2, 10, QChar('0')).arg(lib->second.title, it->title);
            title.replace('=', '-');
            const QString song = QStringList { title, "Starsign Practice", it->irealStyle.isEmpty() ? "Medium Swing" : it->irealStyle,
                                               it->irealKey.isEmpty() ? "C" : it->irealKey, "n", it->irealProg }.join('=');
            all << song;
            links += QString("<li><a href=\"%1\">%2</a>%3</li>").arg(urlQuote("irealbook://" + song), esc(title),
                                                                     b.tempo > 0 ? QString(" <small>(&#9833;=%1)</small>").arg(b.tempo) : QString());
        }
        if (all.isEmpty()) {
            continue;
        }
        const QString dayTitle = d.date.isValid() ? d.date.toString("dddd d MMMM") : QString("Day %1").arg(di + 1);
        body += QString("<h3>%1</h3><p><a href=\"%2\"><b>All %3 charts as one playlist</b></a></p><ol>%4</ol>")
                .arg(esc(dayTitle), urlQuote("irealbook://" + all.join('=')), QString::number(all.size()), links);
    }
    return QString("<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width'>"
                   "<title>%1 - iReal Pro</title></head><body style='font-family:-apple-system,sans-serif;padding:16px;"
                   "background:#16130f;color:#ede6d6'><style>a{color:#d4a85a}</style><h2>%1</h2>"
                   "<p>Tap a link to open it in iReal Pro. Each chart is the chord progression of one practice item.</p>%2</body></html>")
           .arg(esc(plan.name), body);
}

QString mu::project::practice::prettyText(const QString& s)
{
    QString out = s;
    static const QRegularExpression flat("(?<![A-Za-z])([A-G])b(?![a-ln-z])");
    static const QRegularExpression sharp("(?<![A-Za-z])([A-G])#");
    static const QRegularExpression halfDim("m7b5");
    static const QRegularExpression major7(QString::fromUtf8("(?<=[A-G\u266D\u266F])\\^(?=\\d)"));
    static const QRegularExpression ext("(?<=[0-9)])([b#])(?=\\d)");
    out.replace(halfDim, "\u00F87");
    out.replace(flat, "\\1\u266D");
    out.replace(sharp, "\\1\u266F");
    out.replace(major7, "\u0394");
    QString r;
    int last = 0;
    for (auto it = ext.globalMatch(out); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        r += out.mid(last, m.capturedStart(1) - last);
        r += m.captured(1) == "b" ? QString(QChar(0x266D)) : QString(QChar(0x266F));
        last = m.capturedEnd(1);
    }
    r += out.mid(last);
    return r;
}

QString mu::project::practice::categoryName(const QString& c)
{
    static const std::map<QString, QString> NAMES {
        { "harmony", "Harmony" }, { "voicings", "Voicings" }, { "comping", "Comping" }, { "parts", "Written parts" },
        { "solo", "Solo" }, { "vocabulary", "Vocabulary" }, { "form", "Form and memory" }, { "technique", "Technique" },
        { "ear", "Ear" }, { "performance", "Performance" }
    };
    auto it = NAMES.find(c);
    return it == NAMES.end() ? c : it->second;
}

QString mu::project::practice::formatMinutes(int m)
{
    if (m < 60) {
        return QString("%1 min").arg(m);
    }
    const int h = m / 60;
    const int r = m % 60;
    return r ? QString("%1 h %2 min").arg(h).arg(r) : QString("%1 h").arg(h);
}

QString mu::project::practice::stageName(int stage)
{
    switch (stage) {
    case 0: return "New";
    case 1: return "Started";
    case 2: return "Getting there";
    case 3: return "Solid";
    case 4: return "Mastered";
    }
    return QString();
}
