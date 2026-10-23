/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Practice (see practicemodel.h)
 */
#include "practicemodel.h"

#include <algorithm>
#include <cmath>
#include <set>

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "project/internal/starscore/starscorepracticebook.h"
#include "types/uri.h"

#include "translation.h"

#include "log.h"

using namespace mu::project;

static const char* PRACTICE_DEFAULT_SETLIST
    = "Set 1\nHit List\nFeed Your Kids Bugs\nDouble Entendre\nDream of Mushroom\nFish Oil\nUpdog\n\n"
      "Set 2\nBet\nThe Courier\nIndustrial Park Driveway\nAmplitudes\nG.I. Jorge\nCiao Ferrari\n\n"
      "Set 3\nGhostbusters\nThe Trip\nSeagrass\nLive Strong / Strasbourg / St. Denis\nLittle Louie\nOkane\n";

static const QStringList PRACTICE_AREAS { "parts", "comping", "solos" };

static QString practiceNormalized(const QString& s)
{
    QString n = s.toLower();
    n.replace("&", "and");
    n.remove(QRegularExpression("[^a-z0-9]"));
    return n;
}

PracticeModel::PracticeModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

// ---------------------------------------------------------------- properties
QVariantList PracticeModel::songs() const
{
    QVariantList out;
    for (const practice::SongEntry& s : m_songs) {
        out << QVariantMap { { "code", s.code }, { "title", s.title }, { "path", s.path }, { "items", s.items },
                             { "solos", s.solos }, { "minutes", s.minutes }, { "time", practice::formatMinutes(s.minutes) } };
    }
    return out;
}

QString PracticeModel::setlistText() const
{
    return m_setlistText;
}

QVariantList PracticeModel::setlist() const
{
    QVariantList out;
    for (const SetSong& s : m_setlist) {
        QVariantMap needs;
        int items = 0;
        int solos = 0;
        if (!s.code.isEmpty()) {
            for (const auto& [k, v] : needsFor(s.code)) {
                needs[k] = v;
            }
            for (const practice::SongEntry& e : m_songs) {
                if (e.code == s.code) {
                    items = e.items;
                    solos = e.solos;
                }
            }
        }
        out << QVariantMap { { "line", s.line }, { "code", s.code }, { "title", s.title.isEmpty() ? s.line : s.title },
                             { "set", s.set }, { "found", !s.code.isEmpty() }, { "needs", needs }, { "items", items },
                             { "solos", solos }, { "changed", m_needs.count(s.code) > 0 } };
    }
    return out;
}

QString PracticeModel::showDate() const
{
    return m_showDate.toString(Qt::ISODate);
}

QString PracticeModel::startDate() const
{
    return m_startDate.toString(Qt::ISODate);
}

int PracticeModel::defaultMinutes() const
{
    return m_defaultMinutes;
}

int PracticeModel::routineMinutes() const
{
    return m_routineMinutes;
}

QVariantList PracticeModel::days() const
{
    QVariantList out;
    if (!m_startDate.isValid() || !m_showDate.isValid() || m_showDate < m_startDate) {
        return out;
    }
    for (QDate d = m_startDate; d <= m_showDate && out.size() < 120; d = d.addDays(1)) {
        auto it = m_dayMinutes.find(d);
        const int m = it != m_dayMinutes.end() ? it->second : m_defaultMinutes;
        out << QVariantMap { { "date", d.toString(Qt::ISODate) }, { "label", d.toString("ddd d MMM") }, { "minutes", m },
                             { "isShow", d == m_showDate }, { "changed", it != m_dayMinutes.end() } };
    }
    return out;
}

int PracticeModel::totalMinutes() const
{
    int total = 0;
    for (const QVariant& v : days()) {
        total += v.toMap().value("minutes").toInt();
    }
    return total;
}

QVariantList PracticeModel::plans() const
{
    QVariantList out;
    for (const practice::Plan& p : m_plans) {   // (only the current setlist's: see reloadPlans)
        int done = 0;
        int total = 0;
        for (const practice::Day& d : p.days) {
            for (const practice::Block& b : d.blocks) {
                ++total;
                done += b.done ? 1 : 0;
            }
        }
        QString sub;
        if (p.kind == "routine") {
            sub = QString("Routine · %1").arg(practice::formatMinutes(p.days.empty() ? 0 : p.days.front().minutes));
        } else {
            sub = QString("%1 days").arg(p.days.size());
            if (p.showDate.isValid()) {
                sub += QString(" · show %1").arg(p.showDate.toString("ddd d MMM"));
            }
        }
        sub += QString(" · %1 of %2 done").arg(done).arg(total);
        out << QVariantMap { { "id", p.id }, { "name", p.name }, { "kind", p.kind }, { "subtitle", sub } };
    }
    return out;
}

QString PracticeModel::planId() const
{
    return m_planId;
}

QVariantMap PracticeModel::plan() const
{
    const practice::Plan* p = nullptr;
    for (const practice::Plan& x : m_plans) {
        if (x.id == m_planId) {
            p = &x;
        }
    }
    if (!p) {
        return {};
    }
    const std::map<QString, practice::Library> libs = librariesFor(p->songs);
    const QDate today = QDate::currentDate();
    QVariantList days;
    int todayIndex = -1;
    int doneAll = 0;
    int totalAll = 0;
    for (int di = 0; di < int(p->days.size()); ++di) {
        const practice::Day& d = p->days[di];
        QVariantList blocks;
        int done = 0;
        int planned = 0;
        for (int bi = 0; bi < int(d.blocks.size()); ++bi) {
            const practice::Block& b = d.blocks[bi];
            auto lib = libs.find(b.code);
            const practice::Item* it = lib == libs.end() ? nullptr : lib->second.item(b.itemId);
            done += b.done ? 1 : 0;
            planned += b.minutes;
            QString song = b.code;
            for (const practice::PlanSong& ps : p->songs) {
                if (ps.code == b.code) {
                    song = ps.title;
                }
            }
            blocks << QVariantMap {
                { "index", bi }, { "code", b.code }, { "song", song }, { "title", it ? practice::prettyText(it->title) : b.itemId },
                { "category", it ? it->category : QString() }, { "categoryName", it ? practice::categoryName(it->category) : QString() },
                { "solo", it && lib != libs.end() && lib->second.solo(it->solo) ? lib->second.solo(it->solo)->name : QString() },
                { "minutes", b.minutes }, { "tempo", b.tempo }, { "done", b.done }, { "kind", b.kind },
                { "tempoReached", b.tempoReached }, { "stage", b.stage }, { "stageName", b.stage >= 0 ? practice::stageName(b.stage) : QString() },
                { "missing", it == nullptr }
            };
        }
        doneAll += done;
        totalAll += int(d.blocks.size());
        if (todayIndex < 0 && d.date.isValid() && d.date >= today) {
            todayIndex = di;
        }
        QString title = d.date.isValid() ? d.date.toString("dddd d MMMM") : QString("Session");
        if (p->kind == "plan") {
            title = QString("Day %1 · %2").arg(di + 1).arg(title);
        }
        days << QVariantMap { { "index", di }, { "date", d.date.toString(Qt::ISODate) }, { "title", title }, { "label", d.label },
                              { "minutes", d.minutes }, { "planned", planned }, { "plannedText", practice::formatMinutes(planned) },
                              { "done", done }, { "total", int(d.blocks.size()) }, { "isToday", d.date == today },
                              { "isPast", d.date.isValid() && d.date < today }, { "blocks", blocks } };
    }
    if (todayIndex < 0) {
        todayIndex = p->days.empty() ? -1 : 0;
    }
    return QVariantMap { { "id", p->id }, { "name", p->name }, { "kind", p->kind }, { "showDate", p->showDate.toString(Qt::ISODate) },
                         { "days", days }, { "todayIndex", todayIndex }, { "done", doneAll }, { "total", totalAll } };
}

bool PracticeModel::busy() const
{
    return m_busy;
}

QString PracticeModel::status() const
{
    return m_status;
}

QString PracticeModel::libraryFolder() const
{
    return m_root;
}

// ---------------------------------------------------------------- loading
void PracticeModel::load()
{
    m_root = practice::libraryRoot(starScore()->auditLibraryFolder());
    loadSettings();
    m_busy = true;
    setStatus(QStringLiteral("Reading the song libraries…"));
    QTimer::singleShot(30, this, [this]() {
        m_songs = practice::scanLibraries(m_root, practice::practiceFolder() + "/libraries.json");
        m_busy = false;
        parseSetlist();
        int withLib = 0;
        for (const SetSong& s : m_setlist) {
            withLib += s.code.isEmpty() ? 0 : 1;
        }
        setStatus(m_songs.empty()
                  ? QStringLiteral("No songs with practice items were found in %1.").arg(m_root.isEmpty() ? QStringLiteral("the library")
                                                                                         : m_root)
                  : QStringLiteral("%1 songs have practice items. %2 of the %3 setlist songs do.").arg(m_songs.size()).arg(withLib)
                  .arg(m_setlist.size()));
        reloadPlans();
        watchPlans();
    });
}

void PracticeModel::rescan()
{
    load();
}

void PracticeModel::watchPlans()
{
    if (!m_watcher) {
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this]() { reloadPlans(); });
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this]() { reloadPlans(); });
    }
    const QString dir = practice::practiceFolder() + "/plans";
    if (!m_watcher->directories().contains(dir)) {
        m_watcher->addPath(dir);
    }
    for (const practice::Plan& p : m_plans) {
        const QString f = dir + "/" + p.id + ".json";
        if (!m_watcher->files().contains(f) && QFileInfo::exists(f)) {
            m_watcher->addPath(f);
        }
    }
}

void PracticeModel::reloadPlans()
{
    // the current setlist's plans (plans from before saved setlists belong to the first one)
    bool legacy = false;
    for (const SavedSetlist& sl : m_setlists) {
        legacy = legacy || (sl.id == m_setlistId && sl.legacy);
    }
    m_plans.clear();
    for (const practice::Plan& p : practice::loadPlans()) {
        if (m_setlistId.isEmpty() || p.setlistId == m_setlistId || (legacy && p.setlistId.isEmpty())) {
            m_plans.push_back(p);
        }
    }
    bool found = false;
    for (const practice::Plan& p : m_plans) {
        found = found || p.id == m_planId;
    }
    if (!found) {
        m_planId = m_plans.empty() ? QString() : m_plans.front().id;
    }
    if (m_watcher) {
        watchPlans();
    }
    emit plansChanged();
}

// ---------------------------------------------------------------- saved setlists
// setlists.json: { "current": id, "setlists": [ { "id", "name", "legacy", "data": { setlist, showDate, ... } } ] }.
// 1.19 kept one setlist in setlist.json; it becomes the first saved setlist.
static QString setlistsFile()
{
    return practice::practiceFolder() + "/setlists.json";
}

QJsonObject PracticeModel::currentData() const
{
    QJsonObject dm;
    for (const auto& [d, m] : m_dayMinutes) {
        dm[d.toString(Qt::ISODate)] = m;
    }
    QJsonObject needs;
    for (const auto& [code, n] : m_needs) {
        QJsonObject o;
        for (const auto& [a, v] : n) {
            o[a] = v;
        }
        needs[code] = o;
    }
    return QJsonObject { { "setlist", m_setlistText }, { "showDate", m_showDate.toString(Qt::ISODate) },
                         { "defaultMinutes", m_defaultMinutes }, { "routineMinutes", m_routineMinutes }, { "dayMinutes", dm },
                         { "needs", needs }, { "breakEvery", m_breakEvery }, { "breakMinutes", m_breakMinutes } };
}

void PracticeModel::applyData(const QJsonObject& o)
{
    m_setlistText = o.value("setlist").toString(QString::fromUtf8(PRACTICE_DEFAULT_SETLIST));
    m_showDate = QDate::fromString(o.value("showDate").toString(), Qt::ISODate);
    const QDate today = QDate::currentDate();
    if (!m_showDate.isValid()) {
        const QDate oct31(2026, 10, 31);
        m_showDate = oct31 >= today ? oct31 : today.addDays(14);
    }
    m_startDate = today;
    m_defaultMinutes = std::clamp(o.value("defaultMinutes").toInt(120), 15, 600);
    m_routineMinutes = std::clamp(o.value("routineMinutes").toInt(120), 15, 600);
    m_breakEvery = std::clamp(o.value("breakEvery").toInt(0), 0, 180);
    m_breakMinutes = std::clamp(o.value("breakMinutes").toInt(5), 1, 30);
    m_dayMinutes.clear();
    const QJsonObject dm = o.value("dayMinutes").toObject();
    for (const QString& k : dm.keys()) {
        const QDate d = QDate::fromString(k, Qt::ISODate);
        if (d.isValid() && d >= today) {
            m_dayMinutes[d] = dm.value(k).toInt();
        }
    }
    m_needs.clear();
    const QJsonObject needs = o.value("needs").toObject();
    for (const QString& code : needs.keys()) {
        const QJsonObject n = needs.value(code).toObject();
        for (const QString& a : n.keys()) {
            if (PRACTICE_AREAS.contains(a)) {   // (1.19's "harmony" is gone)
                m_needs[code][a] = std::clamp(n.value(a).toInt(), 0, 3);
            }
        }
    }
}

void PracticeModel::loadSettings()
{
    m_setlists.clear();
    QFile f(setlistsFile());
    QJsonObject root;
    if (f.open(QIODevice::ReadOnly)) {
        root = QJsonDocument::fromJson(f.readAll()).object();
    }
    for (const QJsonValue& v : root.value("setlists").toArray()) {
        const QJsonObject o = v.toObject();
        SavedSetlist sl;
        sl.id = o.value("id").toString();
        sl.name = o.value("name").toString();
        sl.data = o.value("data").toObject();
        sl.legacy = o.value("legacy").toBool();
        if (!sl.id.isEmpty()) {
            m_setlists.push_back(sl);
        }
    }
    if (m_setlists.empty()) {
        // the 1.19 setlist (or the default one)
        QFile old(practice::practiceFolder() + "/setlist.json");
        QJsonObject o;
        if (old.open(QIODevice::ReadOnly)) {
            o = QJsonDocument::fromJson(old.readAll()).object();
        }
        SavedSetlist sl;
        sl.id = QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
        sl.data = o;
        sl.legacy = true;
        const QDate d = QDate::fromString(o.value("showDate").toString(), Qt::ISODate);
        sl.name = QString("Show %1").arg((d.isValid() ? d : QDate(2026, 10, 31)).toString("ddd d MMM yyyy"));
        m_setlists.push_back(sl);
    }
    m_setlistId = root.value("current").toString();
    const SavedSetlist* cur = nullptr;
    for (const SavedSetlist& sl : m_setlists) {
        if (sl.id == m_setlistId) {
            cur = &sl;
        }
    }
    if (!cur) {
        cur = &m_setlists.front();
        m_setlistId = cur->id;
    }
    applyData(cur->data);
    saveSetlists();
}

void PracticeModel::saveSetlists() const
{
    QJsonArray arr;
    for (const SavedSetlist& sl : m_setlists) {
        arr.append(QJsonObject { { "id", sl.id }, { "name", sl.name }, { "legacy", sl.legacy },
                                 { "data", sl.id == m_setlistId ? currentData() : sl.data } });
    }
    QSaveFile f(setlistsFile());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonObject { { "current", m_setlistId }, { "setlists", arr } }).toJson());
        f.commit();
    }
}

void PracticeModel::saveSettings() const
{
    saveSetlists();
}

QVariantList PracticeModel::setlists() const
{
    QVariantList out;
    for (const SavedSetlist& sl : m_setlists) {
        const QDate d = QDate::fromString((sl.id == m_setlistId ? currentData() : sl.data).value("showDate").toString(), Qt::ISODate);
        out << QVariantMap { { "id", sl.id }, { "name", sl.name }, { "date", d.toString(Qt::ISODate) },
                             { "dateText", d.isValid() ? d.toString("ddd d MMM yyyy") : QString() } };
    }
    return out;
}

QString PracticeModel::setlistId() const
{
    return m_setlistId;
}

QString PracticeModel::setlistName() const
{
    for (const SavedSetlist& sl : m_setlists) {
        if (sl.id == m_setlistId) {
            return sl.name;
        }
    }
    return {};
}

int PracticeModel::breakEvery() const
{
    return m_breakEvery;
}

int PracticeModel::breakMinutes() const
{
    return m_breakMinutes;
}

void PracticeModel::selectSetlist(const QString& id)
{
    if (id == m_setlistId) {
        return;
    }
    for (SavedSetlist& sl : m_setlists) {
        if (sl.id == m_setlistId) {
            sl.data = currentData();
        }
    }
    for (const SavedSetlist& sl : m_setlists) {
        if (sl.id == id) {
            m_setlistId = id;
            applyData(sl.data);
            break;
        }
    }
    saveSetlists();
    parseSetlist();
    m_planId.clear();
    reloadPlans();
    emit changed();
}

void PracticeModel::newSetlist(const QString& name, const QString& isoDate)
{
    for (SavedSetlist& sl : m_setlists) {
        if (sl.id == m_setlistId) {
            sl.data = currentData();
        }
    }
    SavedSetlist sl;
    sl.id = QString::number(QDateTime::currentMSecsSinceEpoch(), 36);
    sl.data = currentData();
    const QDate d = QDate::fromString(isoDate, Qt::ISODate);
    if (d.isValid()) {
        sl.data["showDate"] = d.toString(Qt::ISODate);
        sl.data["dayMinutes"] = QJsonObject();
    }
    sl.name = name.trimmed().isEmpty() ? QString("Show %1").arg((d.isValid() ? d : m_showDate).toString("ddd d MMM yyyy")) : name.trimmed();
    m_setlists.push_back(sl);
    m_setlistId = sl.id;
    applyData(sl.data);
    saveSetlists();
    parseSetlist();
    m_planId.clear();
    reloadPlans();
    setStatus(QStringLiteral("Made the setlist “%1”.").arg(sl.name));
}

void PracticeModel::renameSetlist(const QString& name)
{
    if (name.trimmed().isEmpty()) {
        return;
    }
    for (SavedSetlist& sl : m_setlists) {
        if (sl.id == m_setlistId) {
            sl.name = name.trimmed();
        }
    }
    saveSetlists();
    emit changed();
}

void PracticeModel::deleteSetlist()
{
    if (m_setlists.size() < 2) {
        setStatus(QStringLiteral("This is the only setlist, so it stays."));
        return;
    }
    const muse::IInteractive::Result r = interactive()->questionSync(
        muse::trc("starscore", "Delete this setlist?"),
        QString("“%1” and its plans. Your progress on each item is kept.").arg(setlistName()).toStdString(),
        { muse::IInteractive::Button::Cancel, muse::IInteractive::Button::Yes }, muse::IInteractive::Button::Cancel);
    if (r.standardButton() != muse::IInteractive::Button::Yes) {
        return;
    }
    const bool legacy = std::any_of(m_setlists.begin(), m_setlists.end(), [this](const SavedSetlist& sl) {
        return sl.id == m_setlistId && sl.legacy;
    });
    for (const practice::Plan& p : practice::loadPlans()) {
        if (p.setlistId == m_setlistId || (legacy && p.setlistId.isEmpty())) {
            practice::deletePlan(p.id);
        }
    }
    m_setlists.erase(std::remove_if(m_setlists.begin(), m_setlists.end(), [this](const SavedSetlist& sl) {
        return sl.id == m_setlistId;
    }), m_setlists.end());
    m_setlistId = m_setlists.front().id;
    applyData(m_setlists.front().data);
    saveSetlists();
    parseSetlist();
    m_planId.clear();
    reloadPlans();
}

void PracticeModel::setBreakEvery(int minutes)
{
    m_breakEvery = std::clamp(minutes, 0, 180);
    saveSettings();
    emit changed();
}

void PracticeModel::setBreakMinutes(int minutes)
{
    m_breakMinutes = std::clamp(minutes, 1, 30);
    saveSettings();
    emit changed();
}

// ---------------------------------------------------------------- skills
QVariantList PracticeModel::skills() const
{
    return m_skills;
}

void PracticeModel::refreshSkills()
{
    practice::Progress progress;
    progress.load();
    // the skills of items done before 1.20 (the log didn't keep them): from the songs' libraries
    std::set<QString> codes;
    for (const QJsonValue& v : progress.log()) {
        codes.insert(v.toObject().value("song").toString());
    }
    std::map<QString, practice::Library> libs;
    for (const practice::SongEntry& e : m_songs) {
        if (codes.count(e.code)) {
            libs[e.code] = practice::cachedLibrary(e.path);
        }
    }
    const practice::SkillLookup lookup = practice::skillLookup(libs);
    const auto now = practice::skillRatings(progress, lookup);
    const auto before = practice::skillRatings(progress, lookup, QDate::currentDate().addDays(-14));
    QVariantList out;
    for (const QString& group : practice::skillGroups()) {
        std::vector<const practice::SkillRating*> rs;
        for (const auto& [id, r] : now) {
            if (practice::skillGroup(id) == group) {
                rs.push_back(&r);
            }
        }
        std::sort(rs.begin(), rs.end(), [](const practice::SkillRating* a, const practice::SkillRating* b) {
            return a->rating > b->rating;
        });
        for (const practice::SkillRating* r : rs) {
            auto b = before.find(r->id);
            const double was = b == before.end() ? 0.0 : b->second.rating;
            // "Level 2, solid": the level of his best item, and how far along it he is
            const int level = std::max(1, int(std::ceil(r->rating - 0.001)));
            const double frac = r->rating - (level - 1);
            const QString how = frac >= 0.99 ? QStringLiteral("mastered") : frac >= 0.8 ? QStringLiteral("solid")
                                : frac >= 0.45 ? QStringLiteral("getting there") : QStringLiteral("started");
            out << QVariantMap { { "id", r->id }, { "name", practice::skillName(r->id) }, { "group", group },
                                 { "rating", r->rating }, { "levelText", QString("Level %1, %2").arg(level).arg(how) },
                                 { "items", r->items }, { "solidItems", r->solidItems }, { "minutes", r->minutes },
                                 { "last", r->last.isValid() ? r->last.toString("d MMM") : QString() },
                                 { "trend", r->rating - was } };
        }
    }
    m_skills = out;
    emit skillsChanged();
}

void PracticeModel::parseSetlist()
{
    m_setlist.clear();
    int set = 0;
    static const QRegularExpression setLine("^\\s*set\\s*(\\d+)\\s*:?\\s*$", QRegularExpression::CaseInsensitiveOption);
    for (QString line : m_setlistText.split('\n')) {
        line = line.trimmed();
        line.remove(QRegularExpression("^\\d+[.)]\\s*"));   // "1. Hit List"
        if (line.isEmpty()) {
            continue;
        }
        const QRegularExpressionMatch m = setLine.match(line);
        if (m.hasMatch()) {
            set = m.captured(1).toInt();
            continue;
        }
        SetSong s;
        s.line = line;
        s.set = set;
        const QString n = practiceNormalized(line);
        const practice::SongEntry* best = nullptr;
        for (const practice::SongEntry& e : m_songs) {
            if (practiceNormalized(e.title) == n || e.code.compare(line, Qt::CaseInsensitive) == 0) {
                best = &e;
                break;
            }
        }
        if (!best && n.size() >= 4) {
            for (const practice::SongEntry& e : m_songs) {
                const QString t = practiceNormalized(e.title);
                if (t.startsWith(n) || n.startsWith(t)) {
                    best = &e;
                    break;
                }
            }
        }
        if (best) {
            s.code = best->code;
            s.title = best->title;
            s.path = best->path;
        }
        m_setlist.push_back(s);
    }
    emit changed();
}

std::map<QString, int> PracticeModel::needsFor(const QString& code) const
{
    std::map<QString, int> out;
    for (const practice::SongEntry& e : m_songs) {
        if (e.code == code) {
            for (const QString& a : PRACTICE_AREAS) {
                auto it = e.needs.find(a);
                out[a] = it == e.needs.end() ? 1 : std::clamp(it->second, 0, 3);
            }
            break;
        }
    }
    auto ch = m_needs.find(code);
    if (ch != m_needs.end()) {
        for (const auto& [a, v] : ch->second) {
            out[a] = v;
        }
    }
    return out;
}

std::vector<practice::PlanSong> PracticeModel::planSongs() const
{
    std::vector<practice::PlanSong> out;
    std::set<QString> seen;
    for (const SetSong& s : m_setlist) {
        if (s.code.isEmpty() || seen.count(s.code)) {
            continue;
        }
        seen.insert(s.code);
        practice::PlanSong ps;
        ps.code = s.code;
        ps.title = s.title;
        ps.set = s.set;
        ps.needs = needsFor(s.code);
        out.push_back(ps);
    }
    return out;
}

std::map<QString, practice::Library> PracticeModel::librariesFor(const std::vector<practice::PlanSong>& songs) const
{
    std::map<QString, practice::Library> out;
    for (const practice::PlanSong& ps : songs) {
        for (const practice::SongEntry& e : m_songs) {
            if (e.code == ps.code) {
                out[ps.code] = practice::cachedLibrary(e.path);
                break;
            }
        }
    }
    return out;
}

void PracticeModel::setStatus(const QString& s)
{
    m_status = s;
    emit changed();
}

// ---------------------------------------------------------------- editing
void PracticeModel::setSetlistText(const QString& text)
{
    m_setlistText = text;
    parseSetlist();
    saveSettings();
}

void PracticeModel::setNeed(const QString& code, const QString& area, int value)
{
    if (code.isEmpty() || !PRACTICE_AREAS.contains(area)) {
        return;
    }
    m_needs[code][area] = std::clamp(value, 0, 3);
    saveSettings();
    emit changed();
}

void PracticeModel::resetNeeds(const QString& code)
{
    m_needs.erase(code);
    saveSettings();
    emit changed();
}

void PracticeModel::setShowDate(const QString& isoDate)
{
    const QDate d = QDate::fromString(isoDate, Qt::ISODate);
    if (d.isValid()) {
        m_showDate = d;
        saveSettings();
        emit changed();
    }
}

void PracticeModel::setStartDate(const QString& isoDate)
{
    const QDate d = QDate::fromString(isoDate, Qt::ISODate);
    if (d.isValid()) {
        m_startDate = d;
        emit changed();
    }
}

void PracticeModel::setDefaultMinutes(int minutes)
{
    m_defaultMinutes = std::clamp(minutes, 15, 600);
    saveSettings();
    emit changed();
}

void PracticeModel::setRoutineMinutes(int minutes)
{
    m_routineMinutes = std::clamp(minutes, 15, 600);
    saveSettings();
    emit changed();
}

void PracticeModel::setDayMinutes(const QString& isoDate, int minutes)
{
    const QDate d = QDate::fromString(isoDate, Qt::ISODate);
    if (!d.isValid()) {
        return;
    }
    if (minutes == m_defaultMinutes) {
        m_dayMinutes.erase(d);
    } else {
        m_dayMinutes[d] = std::clamp(minutes, 0, 600);
    }
    saveSettings();
    emit changed();
}

// ---------------------------------------------------------------- plans
void PracticeModel::makeRoutine()
{
    const std::vector<practice::PlanSong> songs = planSongs();
    if (songs.empty()) {
        setStatus(QStringLiteral("None of the setlist songs have practice items yet."));
        return;
    }
    practice::Progress progress;
    progress.load();
    practice::PlanRequest req;
    req.kind = "routine";
    req.name = QString("Routine · %1 · %2").arg(QDate::currentDate().toString("ddd d MMM"), practice::formatMinutes(m_routineMinutes));
    req.songs = songs;
    req.days = { { QDate::currentDate(), m_routineMinutes } };
    practice::Plan p = practice::makePlan(req, librariesFor(songs), progress);
    p.setlistId = m_setlistId;
    p.breakEvery = m_breakEvery;
    p.breakMinutes = m_breakMinutes;
    practice::savePlan(p);
    m_planId = p.id;
    reloadPlans();
    setStatus(QStringLiteral("Made “%1”.").arg(p.name));
}

void PracticeModel::makePlan()
{
    const std::vector<practice::PlanSong> songs = planSongs();
    if (songs.empty()) {
        setStatus(QStringLiteral("None of the setlist songs have practice items yet."));
        return;
    }
    practice::PlanRequest req;
    req.kind = "plan";
    req.showDate = m_showDate;
    req.name = QString("Show %1").arg(m_showDate.toString("ddd d MMM yyyy"));
    req.songs = songs;
    for (const QVariant& v : days()) {
        const QVariantMap d = v.toMap();
        req.days.push_back({ QDate::fromString(d.value("date").toString(), Qt::ISODate), d.value("minutes").toInt() });
    }
    if (req.days.empty()) {
        setStatus(QStringLiteral("The show date is before the first day."));
        return;
    }
    practice::Progress progress;
    progress.load();
    practice::Plan p = practice::makePlan(req, librariesFor(songs), progress);
    p.setlistId = m_setlistId;
    p.breakEvery = m_breakEvery;
    p.breakMinutes = m_breakMinutes;
    practice::savePlan(p);
    m_planId = p.id;
    reloadPlans();
    setStatus(QStringLiteral("Made “%1”: %2 days.").arg(p.name).arg(p.days.size()));
}

void PracticeModel::selectPlan(const QString& id)
{
    m_planId = id;
    emit plansChanged();
}

void PracticeModel::deletePlan(const QString& id)
{
    QString name;
    for (const practice::Plan& p : m_plans) {
        if (p.id == id) {
            name = p.name;
        }
    }
    const muse::IInteractive::Result r = interactive()->questionSync(
        muse::trc("starscore", "Delete this plan?"),
        QString("\u201C%1\u201D and the days it has marked done. Your progress on each item is kept.").arg(name).toStdString(),
        { muse::IInteractive::Button::Cancel, muse::IInteractive::Button::Yes }, muse::IInteractive::Button::Cancel);
    if (r.standardButton() != muse::IInteractive::Button::Yes) {
        return;
    }
    practice::deletePlan(id);
    if (m_planId == id) {
        m_planId.clear();
    }
    reloadPlans();
}

void PracticeModel::rebuildPlan()
{
    practice::Plan old = practice::loadPlan(m_planId);
    if (old.id.isEmpty()) {
        return;
    }
    practice::PlanRequest req;
    req.kind = old.kind;
    req.name = old.name;
    req.showDate = old.showDate;
    // the plan's songs, with today's needs (the user may have changed them since)
    for (practice::PlanSong ps : old.songs) {
        bool known = false;
        for (const practice::SongEntry& e : m_songs) {
            known = known || e.code == ps.code;
        }
        if (known) {
            ps.needs = needsFor(ps.code);
        }
        req.songs.push_back(ps);
    }
    for (const practice::Day& d : old.days) {
        req.days.push_back({ d.date, d.minutes });
    }
    practice::Progress progress;
    progress.load();
    const QDate today = QDate::currentDate();
    // a routine is rebuilt whole; a plan keeps its days before today, and today's done blocks
    practice::Plan p;
    if (old.kind == "routine") {
        p = practice::makePlan(req, librariesFor(req.songs), progress, &old);
    } else {
        p = practice::makePlan(req, librariesFor(req.songs), progress, &old, today);
        // today's blocks that are done stay at the top of today
        for (const practice::Day& od : old.days) {
            if (od.date != today) {
                continue;
            }
            for (practice::Day& nd : p.days) {
                if (nd.date != today) {
                    continue;
                }
                std::vector<practice::Block> kept;
                int doneMinutes = 0;
                for (const practice::Block& b : od.blocks) {
                    if (b.done) {
                        kept.push_back(b);
                        doneMinutes += b.minutes;
                    }
                }
                std::vector<practice::Block> rest;
                int left = nd.minutes - doneMinutes;
                for (const practice::Block& b : nd.blocks) {
                    bool dup = false;
                    for (const practice::Block& k : kept) {
                        dup = dup || (k.code == b.code && k.itemId == b.itemId);
                    }
                    if (!dup && left - b.minutes >= 0) {
                        rest.push_back(b);
                        left -= b.minutes;
                    }
                }
                kept.insert(kept.end(), rest.begin(), rest.end());
                nd.blocks = kept;
            }
        }
    }
    p.setlistId = old.setlistId;
    p.breakEvery = old.breakEvery;
    p.breakMinutes = old.breakMinutes;
    practice::savePlan(p);
    reloadPlans();
    setStatus(QStringLiteral("Planned again from today, from your progress so far."));
}

void PracticeModel::addMinutes(int dayIndex, int minutes)
{
    practice::Plan p = practice::loadPlan(m_planId);
    if (p.id.isEmpty()) {
        return;
    }
    for (practice::PlanSong& ps : p.songs) {   // (today's needs: they may have changed since the plan was made)
        for (const practice::SongEntry& e : m_songs) {
            if (e.code == ps.code) {
                ps.needs = needsFor(ps.code);
            }
        }
    }
    practice::Progress progress;
    progress.load();
    const int before = dayIndex >= 0 && dayIndex < int(p.days.size()) ? int(p.days[dayIndex].blocks.size()) : 0;
    const int added = practice::addToDay(p, dayIndex, minutes, librariesFor(p.songs), progress);
    if (added == 0) {
        setStatus(QStringLiteral("Nothing more to add for these songs today."));
        return;
    }
    practice::savePlan(p);
    reloadPlans();
    setStatus(QStringLiteral("Added %1 items (%2) to the end of the day.").arg(int(p.days[dayIndex].blocks.size()) - before)
              .arg(practice::formatMinutes(added)));
}

QString PracticeModel::outputFolder() const
{
    QString dir = !m_root.isEmpty() && QFileInfo(m_root).isDir() ? m_root + "/Practice"
                  : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/StarScore Practice";
    QDir().mkpath(dir);
    return dir;
}

static QString practiceFileSafe(QString s)
{
    s.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "-");
    return s.trimmed();
}

void PracticeModel::makeBook(int dayIndex)
{
    const practice::Plan p = practice::loadPlan(m_planId);
    if (p.id.isEmpty() || m_busy) {
        return;
    }
    const std::map<QString, practice::Library> libs = librariesFor(p.songs);
    QString name = p.name;
    if (dayIndex >= 0 && dayIndex < int(p.days.size())) {
        name += " · " + (p.days[dayIndex].date.isValid() ? p.days[dayIndex].date.toString("ddd d MMM") : QString("Day %1").arg(dayIndex + 1));
    }
    const QString pdf = outputFolder() + "/" + practiceFileSafe(name) + ".pdf";
    m_busy = true;
    setStatus(QStringLiteral("Making the practice book…"));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    std::map<QString, QList<QImage> > cache;
    IStarScoreService* ss = starScore().get();
    practice::NotationRenderer renderer = [&cache, ss](const practice::Item& item, int) -> QList<QImage> {
        if (!item.hasXml() || !ss) {
            return {};
        }
        const QString key = item.id + "|" + QString::number(qHash(item.xmlPacked));
        auto it = cache.find(key);
        if (it != cache.end()) {
            return it->second;
        }
        QList<QImage> images = ss->renderPracticeExercise(item.xml(), 160);
        cache[key] = images;
        return images;
    };
    const bool ok = practice::writePracticeBook(p, libs, pdf, renderer, dayIndex, [this](int done, int total) {
        m_status = QStringLiteral("Making the practice book… %1 of %2").arg(done).arg(total);
        emit changed();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    });
    m_busy = false;
    if (ok) {
        m_lastOutput = pdf;
        setStatus(QStringLiteral("Saved %1").arg(QFileInfo(pdf).fileName()));
        QDesktopServices::openUrl(QUrl::fromLocalFile(pdf));
    } else {
        setStatus(QStringLiteral("Couldn't write %1").arg(pdf));
    }
}

void PracticeModel::exportIreal(int dayIndex)
{
    const practice::Plan p = practice::loadPlan(m_planId);
    if (p.id.isEmpty()) {
        return;
    }
    const QString html = practice::irealHtml(p, librariesFor(p.songs), dayIndex);
    QString name = p.name + " · iReal Pro";
    if (dayIndex >= 0 && dayIndex < int(p.days.size()) && p.days[dayIndex].date.isValid()) {
        name = p.name + " · " + p.days[dayIndex].date.toString("ddd d MMM") + " · iReal Pro";
    }
    const QString file = outputFolder() + "/" + practiceFileSafe(name) + ".html";
    QSaveFile f(file);
    if (!f.open(QIODevice::WriteOnly)) {
        setStatus(QStringLiteral("Couldn't write %1").arg(file));
        return;
    }
    f.write(html.toUtf8());
    f.commit();
    m_lastOutput = file;
    setStatus(QStringLiteral("Saved %1. Open it on a device with iReal Pro and tap a chart.").arg(QFileInfo(file).fileName()));
    QDesktopServices::openUrl(QUrl::fromLocalFile(file));
}

void PracticeModel::openRunner(int dayIndex, int blockIndex, bool handsFree)
{
    if (m_planId.isEmpty()) {
        return;
    }
    const QString uri = QString("musescore://starscore/practicerunner?planId=%1&dayIndex=%2&blockIndex=%3&handsFree=%4&modal=false&floating=true")
                        .arg(m_planId).arg(dayIndex).arg(std::max(0, blockIndex)).arg(handsFree ? 1 : 0);
    interactive()->open(muse::UriQuery(uri.toStdString()));
}

void PracticeModel::chooseLibraryFolder()
{
    const muse::io::path_t dir = interactive()->selectDirectory(
        muse::trc("starscore", "Choose the folder with your Starsign songs"),
        muse::io::path_t(m_root.isEmpty() ? QDir::homePath() : m_root));
    if (dir.empty()) {
        return;
    }
    starScore()->setAuditLibraryFolder(dir.toQString());
    load();
}

void PracticeModel::openOutputFolder()
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(outputFolder()));
}
