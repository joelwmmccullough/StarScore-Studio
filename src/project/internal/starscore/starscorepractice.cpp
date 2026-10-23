/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice: libraries, progress, and the planner (see starscorepractice.h)
 */
#include "starscorepractice.h"

#include <algorithm>
#include <cmath>
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

Library mu::project::practice::parseLibrary(const QJsonObject& root)
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
        it.xml = o.value("xml").toString();
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

Library mu::project::practice::decodeLibrary(const QByteArray& blob)
{
    const QByteArray packed = QByteArray::fromBase64(blob.trimmed());
    const QByteArray json = qUncompress(packed);
    if (json.isEmpty()) {
        return {};
    }
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    return doc.isObject() ? parseLibrary(doc.object()) : Library();
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
            return zip.fileData(name.toStdString()).toQByteArrayNoCopy();
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

Library mu::project::practice::readLibrary(const QString& starscorePath)
{
    const QByteArray mscx = mainMscx(starscorePath);
    const QByteArray blob = metaTagValue(mscx, PRACTICE_META_TAG);
    if (blob.isEmpty()) {
        return {};
    }
    Library lib = decodeLibrary(blob);
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
        if (path.contains("/Version History/") || path.contains("/MuseScore Files/") || path.contains("Autosave")
            || path.contains("/5 Quarantine") || path.contains("/Z Empty")) {
            continue;
        }
        const QFileInfo fi(path);
        const QString stamp = QString("%1|%2").arg(fi.lastModified().toMSecsSinceEpoch()).arg(fi.size());
        QJsonObject entry = cache.value(path).toObject();
        if (entry.value("stamp").toString() != stamp) {
            const Library lib = readLibrary(path);
            int minutes = 0;
            for (const Item& item : lib.items) {
                minutes += item.minutes;
            }
            entry = QJsonObject { { "stamp", stamp }, { "code", lib.code }, { "title", lib.title },
                                  { "items", int(lib.items.size()) }, { "solos", int(lib.solos.size()) }, { "minutes", minutes } };
        }
        newCache[path] = entry;
        if (entry.value("items").toInt() > 0) {
            out.push_back({ entry.value("code").toString(), entry.value("title").toString(), path, entry.value("items").toInt(),
                            entry.value("solos").toInt(), entry.value("minutes").toInt() });
        }
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

    // song weights: the needs, at least 1
    std::map<QString, double> weight;
    double totalWeight = 0;
    for (const PlanSong& ps : req.songs) {
        if (!libs.count(ps.code)) {
            continue;
        }
        double w = 1.0;
        for (const auto& [k, v] : ps.needs) {
            w += v;
        }
        weight[ps.code] = w;
        totalWeight += w;
    }
    if (totalWeight <= 0) {
        return plan;
    }
    std::map<QString, double> debt;   // minutes owed to each song

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
        const bool runThrough = !isShow && req.kind == "plan" && nDays > 2 && (di == nDays - 1 || (req.showDate.isValid()
                                                                                                  && date.daysTo(req.showDate) == 1));
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
            s.stage = std::min(3, s.stage + 1);
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

        // warm-up: a short harmony or technique item from the day's first song with harmony work left
        if (left >= 45 && !isShow) {
            for (const PlanSong& ps : ordered) {
                auto lib = libs.find(ps.code);
                if (lib == libs.end()) {
                    continue;
                }
                const Item* best = nullptr;
                for (const Item& it : lib->second.items) {
                    if ((it.category == "harmony" || it.category == "technique") && it.level == 1 && prereqsMet(ps.code, it)
                        && sim[key(ps.code, it.id)].stage < 3 && !today.count(key(ps.code, it.id))) {
                        best = &it;
                        break;
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
            const int per = std::max(5, (isShow ? std::min(left, 45) : left) / std::max(1, int(ordered.size())));
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

        if (!isShow) {
            // owed minutes for the day
            for (const auto& [code, w] : weight) {
                debt[code] += std::max(0, left) * w / totalWeight;
            }
            // songs in order of what they're owed; each gets its owed minutes now when that's at least a block
            std::vector<QString> codes;
            for (const PlanSong& ps : ordered) {
                if (weight.count(ps.code)) {
                    codes.push_back(ps.code);
                }
            }
            const int dayLeft = left;
            for (const QString& code : codes) {
                const auto& lib = libs.at(code);
                const PlanSong* ps = nullptr;
                for (const PlanSong& p : req.songs) {
                    if (p.code == code) {
                        ps = &p;
                    }
                }
                int budget = int(std::floor(debt[code] / 5.0)) * 5;
                if (budget < 8 && dayLeft >= 60) {
                    continue;   // its turn comes another day
                }
                budget = std::min(budget, left);
                if (budget < 5) {
                    continue;
                }
                std::map<QString, int> areaUsed;
                int spent = 0;
                while (budget - spent >= 5) {
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
                        double score = need * 10.0 - it.level * 3.0 - areaUsed[area] * 6.0;
                        if (s.stage >= 4) {
                            score -= 40;
                        } else if (s.stage >= 3) {
                            // solid: a review now and then
                            const int since = s.lastDay.isValid() ? s.lastDay.daysTo(date) : 99;
                            score -= since >= 4 ? 8 : 30;
                        } else if (s.scheduled > 0) {
                            // learning: back every other day or so
                            const int since = s.lastDay.isValid() ? s.lastDay.daysTo(date) : 99;
                            score += since >= 2 ? 6 : (since == 1 ? 2 : -10);
                        }
                        score -= s.scheduled * 1.5;
                        if (score > bestScore) {
                            bestScore = score;
                            best = &it;
                        }
                    }
                    if (!best) {
                        break;
                    }
                    const int m = std::min(best->minutes, budget - spent);
                    if (m < 5) {
                        break;
                    }
                    schedule(code, *best, m, "item");
                    areaUsed[areaOf(best->category)] += 1;
                    spent += m;
                }
                debt[code] -= spent;
            }
        }
        // the day in set order, warm-up first
        std::stable_sort(day.blocks.begin(), day.blocks.end(), [&](const Block& a, const Block& b) {
            auto rank = [&](const Block& x) {
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
            return rank(a) < rank(b);
        });
        plan.days.push_back(day);
    }
    return plan;
}

// ---------------------------------------------------------------- saved plans
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
    return QString::fromLatin1(QUrl::toPercentEncoding(s, "/:=*[]|,^#+-.()"));
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
