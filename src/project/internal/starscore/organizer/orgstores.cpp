/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: the data files it keeps
 */
#include "orgstores.h"

#include <set>

#include <QFile>
#include <QRegularExpression>

namespace mu::project::starscore::org {
// ------------------------------------------------------------------ roster
QJsonObject Player::toJson() const
{
    QJsonObject o;
    o["name"] = name;
    o["instruments"] = QJsonArray::fromStringList(instruments);
    o["blurb"] = blurb;
    o["current"] = current;
    o["horn"] = horn;
    QJsonObject ch;
    for (const auto& [n, c] : chairs) {
        ch[QString::number(n)] = QJsonObject { { "chair", c.chair }, { "key", c.key }, { "instrument", c.instrument } };
    }
    if (!ch.isEmpty()) {
        o["chairs"] = ch;
    }
    return o;
}

Player Player::fromJson(const QJsonObject& o)
{
    Player p;
    p.name = o.value("name").toString();
    for (const QJsonValue& v : o.value("instruments").toArray()) {
        p.instruments << v.toString();
    }
    p.blurb = o.value("blurb").toString();
    p.current = o.value("current").toBool(true);
    p.horn = o.value("horn").toBool(false);
    const QJsonObject ch = o.value("chairs").toObject();
    for (auto it = ch.begin(); it != ch.end(); ++it) {
        const QJsonObject c = it.value().toObject();
        p.chairs[it.key().toInt()] = { c.value("chair").toInt(), c.value("key").toString(), c.value("instrument").toString() };
    }
    return p;
}

QString unreadableMessage(const QString& fileName, const JsonRead& read)
{
    if (!read.unreadable()) {
        return QString();
    }
    return QString("%1 is there but can't be read (%2).").arg(fileName, read.error);
}

QString Roster::load(const Paths& paths)
{
    players.clear();
    fileNameHints.clear();
    const JsonRead read = readJsonChecked(paths.toolkit + "/roster.json");
    if (read.unreadable()) {
        return unreadableMessage("roster.json", read);
    }
    const QJsonObject o = read.doc.object();
    if (o.isEmpty()) {
        // no roster yet: the band lives in roster.json (Dashboard > Band roster), never in the program
        return QString();
    }
    for (const QJsonValue& v : o.value("players").toArray()) {
        players.push_back(Player::fromJson(v.toObject()));
    }
    const QJsonObject hints = o.value("fileNameHints").toObject();
    for (auto it = hints.begin(); it != hints.end(); ++it) {
        fileNameHints[it.key()] = it.value().toString();
    }
    return QString();
}

bool Roster::save(const Paths& paths) const
{
    QJsonArray a;
    for (const Player& p : players) {
        a.append(p.toJson());
    }
    QJsonObject o;
    o["version"] = 1;
    o["about"] = "The band, for changelogs and Horn Part Guides. Edit it in StarScore: Dashboard › Band roster.";
    o["players"] = a;
    QJsonObject hints;
    for (const auto& [name, inst] : fileNameHints) {
        hints[name] = inst;
    }
    o["fileNameHints"] = hints;
    return writeJson(paths.toolkit + "/roster.json", QJsonDocument(o));
}

std::vector<const Player*> Roster::current() const
{
    std::vector<const Player*> out;
    for (const Player& p : players) {
        if (p.current) {
            out.push_back(&p);
        }
    }
    return out;
}

std::vector<const Player*> Roster::currentHorns() const
{
    std::vector<const Player*> out;
    for (const Player* p : current()) {
        if (p->horn) {
            out.push_back(p);
        }
    }
    return out;
}

static QString partBase(const QString& part)
{
    static const QRegularExpression paren("\\s*\\(.*?\\)$");
    return QString(part).remove(paren).trimmed();
}

QStringList Roster::readersOf(const QString& part) const
{
    const QString lab = part.trimmed();
    const QString base = partBase(lab);
    QStringList out;
    for (const Player* p : current()) {
        if (p->instruments.contains(lab) || p->instruments.contains(base)) {
            out << p->name;
        }
    }
    return out;
}

QString Roster::ownerOf(const QString& part) const
{
    const QStringList r = readersOf(part);
    return r.isEmpty() ? QString() : r.first();
}

// ------------------------------------------------------------------ codes
static std::map<QString, QString> toCodeMap(const QJsonObject& o)
{
    std::map<QString, QString> m;
    for (auto it = o.begin(); it != o.end(); ++it) {
        m[it.key()] = it.value().toString();
    }
    return m;
}

QString Codes::load(const Paths& paths)
{
    band.clear();
    projects.clear();
    bandChanged = projectsChanged = false;
    const JsonRead b = readJsonChecked(paths.toolkit + "/codes.json");
    if (b.unreadable()) {
        return unreadableMessage("codes.json", b);
    }
    band = bandLoaded = toCodeMap(b.doc.object());
    if (!paths.projToolkit.isEmpty()) {
        const JsonRead p = readJsonChecked(paths.projToolkit + "/codes_proj.json");
        if (p.unreadable()) {
            return unreadableMessage("codes_proj.json", p);
        }
        projects = projectsLoaded = toCodeMap(p.doc.object());
    }
    return QString();
}

static QJsonObject toObject(const std::map<QString, QString>& m)
{
    QJsonObject o;
    for (const auto& [k, v] : m) {
        o[k] = v;
    }
    return o;
}

bool Codes::saveBand(const Paths& paths) const
{
    return writeText(paths.toolkit + "/codes.json", pythonStyleJson(toObject(band)));
}

bool Codes::saveProjects(const Paths& paths) const
{
    return !paths.projToolkit.isEmpty() && writeText(paths.projToolkit + "/codes_proj.json", pythonStyleJson(toObject(projects)));
}

//! The file as it is now, with this run's additions and changes on top. Until Oct 2026 the run wrote back the map
//! it had loaded at the start, so a code added in StarScore ("Add song") during a long run was lost.
static bool saveCodesMerged(const QString& path, const QString& fileName, const std::map<QString, QString>& loaded,
                            const std::map<QString, QString>& ours, QString* problem)
{
    const JsonRead now = readJsonChecked(path);
    if (now.unreadable()) {
        if (problem) {
            *problem = unreadableMessage(fileName, now) + " It was left as it is.";
        }
        return true;
    }
    std::map<QString, QString> merged = now.exists ? toCodeMap(now.doc.object()) : loaded;
    for (const auto& [key, code] : ours) {
        auto was = loaded.find(key);
        if (was == loaded.end() || was->second != code) {
            merged[key] = code;        // added or changed by this run
        }
    }
    for (const auto& [key, code] : loaded) {
        if (!ours.count(key)) {
            merged.erase(key);         // removed by this run (nothing does this today, but keep the merge honest)
        }
    }
    if (now.exists ? merged == toCodeMap(now.doc.object()) : merged.empty()) {
        return true;                   // unchanged (or nothing to write): don't touch the file
    }
    return writeText(path, pythonStyleJson(toObject(merged)));
}

bool Codes::saveBandMerged(const Paths& paths, QString* problem) const
{
    return saveCodesMerged(paths.toolkit + "/codes.json", "codes.json", bandLoaded, band, problem);
}

bool Codes::saveProjectsMerged(const Paths& paths, QString* problem) const
{
    if (paths.projToolkit.isEmpty()) {
        return true;
    }
    return saveCodesMerged(paths.projToolkit + "/codes_proj.json", "codes_proj.json", projectsLoaded, projects, problem);
}

QString Codes::rootOf(const QString& code) const
{
    for (const auto& [root, c] : band) {
        if (c == code) {
            return root;
        }
    }
    return QString();
}

QStringList Codes::allCodes() const
{
    std::set<QString> s;
    for (const auto& [k, v] : band) {
        s.insert(v);
    }
    for (const auto& [k, v] : projects) {
        s.insert(v);
    }
    return QStringList(s.begin(), s.end());
}

QString suggestCode(const QString& title, const QStringList& takenList)
{
    const std::set<QString> taken(takenList.begin(), takenList.end());
    QStringList words;
    for (const QString& w : title.normalized(QString::NormalizationForm_D).toUpper().split(QRegularExpression("[^A-Z0-9]+"),
                                                                                             Qt::SkipEmptyParts)) {
        words << w;
    }
    if (words.isEmpty()) {
        words << "SONG";
    }
    const QString letters = words.join("");
    QStringList candidates;
    if (words.size() >= 4) {
        QString c;
        for (int i = 0; i < 4; ++i) {
            c += words[i].at(0);
        }
        candidates << c;
    }
    if (words.size() == 1) {
        candidates << words[0].left(4);
    }
    if (words.size() >= 2) {
        candidates << words[0].left(2) + words[1].left(2) << words[0].left(1) + words[1].left(3)
                   << words[0].left(3) + words[1].left(1) << words.last().left(4) << words[0].left(4);
    }
    if (words.size() == 3) {
        candidates << words[0].left(2) + words[1].left(1) + words[2].left(1)
                   << words[0].left(1) + words[1].left(1) + words[2].left(2);
    }
    for (int a = 1; a < letters.size(); ++a) {
        for (int b = a + 1; b < letters.size(); ++b) {
            for (int c = b + 1; c < letters.size(); ++c) {
                candidates << QString(letters.at(0)) + letters.at(a) + letters.at(b) + letters.at(c);
            }
        }
    }
    for (const QString& c : candidates) {
        if (c.size() == 4 && !taken.count(c)) {
            return c;
        }
    }
    const QString stem = (letters + "XXX").left(3);
    for (char ch = 'A'; ch <= 'Z'; ++ch) {
        if (!taken.count(stem + QChar(ch))) {
            return stem + QChar(ch);
        }
    }
    return QString();
}

// ------------------------------------------------------------------ plain JSON files
QString JsonStore::load(const QString& folder)
{
    const JsonRead read = readJsonChecked(folder + "/" + file);
    changed = false;
    if (read.unreadable()) {
        doc = loaded = QJsonDocument();
        return unreadableMessage(file, read);
    }
    doc = loaded = read.doc;
    return QString();
}

bool JsonStore::save(const QString& folder)
{
    if (!changed) {
        return true;
    }
    const bool ok = writeJson(folder + "/" + file, doc);
    changed = !ok;
    return ok;
}

JsonRead JsonStore::reread(const QString& folder) const
{
    return readJsonChecked(folder + "/" + file);
}

bool JsonStore::changedOnDisk(const JsonRead& now) const
{
    if (!now.exists) {
        return !loaded.isNull() && !loaded.isEmpty();   // it was there at the start and is gone now
    }
    return now.ok && now.doc != loaded;
}
}
