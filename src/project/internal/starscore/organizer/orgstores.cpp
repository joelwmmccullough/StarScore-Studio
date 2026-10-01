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

Roster Roster::defaults()
{
    // The band itself lives in roster.json (Sheets and Demos), never in the program
    return Roster();
}

void Roster::load(const Paths& paths)
{
    const QJsonObject o = readJsonObject(paths.toolkit + "/roster.json");
    if (o.isEmpty()) {
        *this = defaults();
        return;
    }
    players.clear();
    for (const QJsonValue& v : o.value("players").toArray()) {
        players.push_back(Player::fromJson(v.toObject()));
    }
    fileNameHints.clear();
    const QJsonObject hints = o.value("fileNameHints").toObject();
    for (auto it = hints.begin(); it != hints.end(); ++it) {
        fileNameHints[it.key()] = it.value().toString();
    }
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
void Codes::load(const Paths& paths)
{
    band.clear();
    projects.clear();
    const QJsonObject b = readJsonObject(paths.toolkit + "/codes.json");
    for (auto it = b.begin(); it != b.end(); ++it) {
        band[it.key()] = it.value().toString();
    }
    if (!paths.projToolkit.isEmpty()) {
        const QJsonObject p = readJsonObject(paths.projToolkit + "/codes_proj.json");
        for (auto it = p.begin(); it != p.end(); ++it) {
            projects[it.key()] = it.value().toString();
        }
    }
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
bool JsonStore::load(const QString& folder)
{
    bool ok = false;
    doc = readJson(folder + "/" + file, &ok);
    changed = false;
    return ok;
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
}
