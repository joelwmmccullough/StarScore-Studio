/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Dashboard: every Starsign song's arrangements, what's done, and what to work on next
 */
#include "dashboardmodel.h"

#include <algorithm>
#include <map>
#include <set>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QTimer>
#include <QUrl>

#include "translation.h"

using namespace mu::project;

namespace {
struct Column {
    const char* key;
    const char* title;
    const char* shortTitle;
};

//! Dashboard columns, in the order they're shown
const std::vector<Column> COLUMNS {
    { "3H", "3-Horn", "3H" }, { "2H", "2-Horn", "2H" }, { "2F", "2-Horn Flexible", "2F" }, { "3F", "3-Horn Flexible", "3F" },
    { "4H", "4-Horn", "4H" }, { "5H", "5-Horn", "5H" }, { "6H", "6-Horn", "6H" }, { "7H", "7-Horn", "7H" },
};

//! Joel's priority order (29 Sep 2026): which arrangements of which songs come first
struct Tier {
    bool cover;
    const char* column;
};
const std::vector<Tier> TIERS {
    { false, "3H" }, { false, "2H" }, { false, "2F" }, { false, "3F" },
    { true, "3H" }, { true, "2H" }, { true, "2F" }, { true, "3F" },
    { false, "4H" }, { false, "5H" }, { false, "6H" }, { false, "7H" },
    { true, "4H" }, { true, "5H" }, { true, "6H" }, { true, "7H" },
};

QString columnTitle(const QString& key)
{
    for (const Column& c : COLUMNS) {
        if (key == c.key) {
            return QString::fromUtf8(c.title);
        }
    }
    return key;
}

QString normalName(const QString& title)
{
    QString n = title.toLower();
    n.remove(QRegularExpression("[^a-z0-9]"));
    if (n == "feedyourkidsbugs") {
        n = "feedyourkidbugs";
    }
    return n;
}

//! Times played in 2026, from setlist.fm (the band folder's playcounts.py, fetched 19 Aug 2026)
int plays2026(const QString& title)
{
    static const std::map<QString, int> PLAYS = [] {
        const std::vector<std::pair<const char*, int> > raw {
            { "Amplitudes", 6 }, { "Dream of Mushroom", 6 }, { "Feed Your Kid Bugs", 6 }, { "The Courier", 6 },
            { "Hit List", 5 }, { "Seagrass", 5 }, { "Updog", 5 }, { "Cumulonimbus", 4 }, { "Double Entendre", 4 },
            { "Everpresent", 4 }, { "Industrial Park Driveway", 4 }, { "Okane", 4 }, { "Always There", 3 },
            { "Branston Pickle", 3 }, { "Fish Oil", 3 }, { "Shofukan", 3 }, { "Ciao Ferrari", 2 }, { "February", 2 },
            { "Last Pint", 2 }, { "Balkan Wedding", 1 }, { "Break Out", 1 }, { "Chameleon", 1 }, { "Little Louie", 1 },
            { "Live Strong + Strasbourg", 1 }, { "Pick Up The Pieces", 1 }, { "Royal", 1 }, { "Two", 1 },
            { "Up From the South", 1 },
        };
        std::map<QString, int> m;
        for (const auto& [name, n] : raw) {
            m[normalName(QString::fromUtf8(name))] = n;
        }
        return m;
    }();
    auto it = PLAYS.find(normalName(title));
    return it == PLAYS.end() ? 0 : it->second;
}

//! Songs in 1 Starsign Originals and 2 Starsign Covers on 29 Sep 2026, other than Top Hat, Bet and Another One:
//! they were converted from older files, so an arrangement counts as done once it's audited. Any other song
//! (the three above and every new one) counts as done once it's marked Finished.
bool isLegacy(const QString& title)
{
    static const std::set<QString> LEGACY = [] {
        const char* names[] = {
            "Amplitudes", "Branston Pickle", "Ciao Ferrari", "Cumulonimbus", "Deep Speech", "Deimos", "Double Entendre",
            "Dream of Mushroom", "Everpresent", "February", "Feed Your Kids Bugs", "Fish Oil", "G.I. Jorge", "Hit List",
            "Industrial Park Driveway", "Intern", "Last Pint", "Mxter Shirts", "Okane", "Porcupine", "Royal", "Seagrass",
            "Shatter", "The Courier", "Updog",
            "Always There", "Anarchy Rainbow", "Another Star", "Balkan Wedding", "Break Out", "Chameleon", "I'm A Spy",
            "Little Louie", "Move On Up", "Peasant Funk", "Pick Up The Pieces", "Playground", "Shofukan",
            "Standing Next to You", "The Chicken", "The Essential", "Two", "Up From the South",
        };
        std::set<QString> s;
        for (const char* n : names) {
            s.insert(normalName(QString::fromUtf8(n)));
        }
        return s;
    }();
    return LEGACY.count(normalName(title)) > 0;
}

QString statusName(int status)
{
    switch (status) {
    case 0: return muse::qtrc("starscore", "Empty");
    case 1: return muse::qtrc("starscore", "Sketch");
    case 2: return muse::qtrc("starscore", "In progress");
    case 3: return muse::qtrc("starscore", "Needs review");
    default: return muse::qtrc("starscore", "Finished");
    }
}
}

DashboardModel::DashboardModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QString DashboardModel::folder() const
{
    return m_folder;
}

bool DashboardModel::scanning() const
{
    return m_scanning;
}

int DashboardModel::doneCount() const
{
    return m_done;
}

int DashboardModel::totalCount() const
{
    return m_cells;
}

int DashboardModel::unscanned() const
{
    return int(std::count_if(m_songs.begin(), m_songs.end(), [](const Song& s) { return !s.scanned; }));
}

QString DashboardModel::status() const
{
    if (m_folder.isEmpty()) {
        return muse::qtrc("starscore", "Choose the folder that holds your Starsign songs (“Projects and Sheets”).");
    }
    if (m_scanning) {
        return muse::qtrc("starscore", "Reading %1 of %2…").arg(m_total - m_queue.size()).arg(m_total);
    }
    const int left = unscanned();
    if (left > 0) {
        return muse::qtrc("starscore", "%n song(s) not read yet or changed since: press Refresh.", nullptr, left);
    }
    return muse::qtrc("starscore", "%1 of %2 arrangements done").arg(m_done).arg(m_cells);
}

QVariantList DashboardModel::columns() const
{
    QVariantList out;
    for (const Column& c : COLUMNS) {
        out << QVariantMap { { "key", QString::fromUtf8(c.key) }, { "title", QString::fromUtf8(c.title) },
                             { "short", QString::fromUtf8(c.shortTitle) } };
    }
    return out;
}

QVariantList DashboardModel::tiers() const
{
    return m_tiers;
}

QVariantList DashboardModel::tasks() const
{
    return m_tasks;
}

QVariantList DashboardModel::songs() const
{
    return m_rows;
}

void DashboardModel::load()
{
    m_folder = starScore()->auditLibraryFolder();
    m_songs.clear();
    if (!m_folder.isEmpty()) {
        std::map<QString, StarScoreAuditFileSummary> cached;
        for (const StarScoreAuditFileSummary& s : starScore()->cachedLibraryAudit(m_folder)) {
            cached[s.path] = s;
        }
        for (const QString& path : starScore()->auditLibraryFiles(m_folder)) {
            Song song;
            song.path = path;
            song.title = QFileInfo(path).absoluteDir().dirName();
            song.cover = path.contains("/2 Starsign Covers/");
            song.legacy = isLegacy(song.title);
            song.plays = plays2026(song.title);
            auto it = cached.find(path);
            if (it != cached.end()) {
                song.summary = it->second;
                song.scanned = true;
            }
            m_songs.push_back(song);
        }
    }
    rebuild();
}

void DashboardModel::chooseFolder()
{
    const muse::io::path_t dir = interactive()->selectDirectory(
        muse::trc("starscore", "Choose the folder with your Starsign songs"),
        muse::io::path_t(m_folder.isEmpty() ? QDir::homePath() : m_folder));
    if (dir.empty()) {
        return;
    }
    starScore()->setAuditLibraryFolder(dir.toQString());
    load();
}

void DashboardModel::scan(bool force)
{
    if (m_folder.isEmpty() || m_scanning) {
        return;
    }
    m_queue.clear();
    for (const Song& s : m_songs) {
        if (force || !s.scanned) {
            m_queue << s.path;
        }
    }
    if (m_queue.isEmpty()) {
        load();
        return;
    }
    m_total = m_queue.size();
    m_force = force;
    m_scanning = true;
    emit changed();
    QTimer::singleShot(0, this, [this]() { scanNext(); });
}

void DashboardModel::scanNext()
{
    if (!m_scanning) {
        return;
    }
    if (m_queue.isEmpty()) {
        m_scanning = false;
        rebuild();
        return;
    }
    const QString path = m_queue.takeFirst();
    const StarScoreAuditFileSummary summary = starScore()->auditFile(path, m_force);
    for (Song& s : m_songs) {
        if (s.path == path) {
            s.summary = summary;
            s.scanned = true;
        }
    }
    rebuild();
    // one file at a time, so the window stays responsive
    QTimer::singleShot(10, this, [this]() { scanNext(); });
}

void DashboardModel::cancel()
{
    m_scanning = false;
    m_queue.clear();
    emit changed();
}

DashboardModel::Cell DashboardModel::cellFor(const Song& song, const QString& column) const
{
    Cell cell;
    if (!song.scanned) {
        cell.state = "unscanned";
        cell.text = "·";
        return cell;
    }
    if (!song.summary.error.isEmpty()) {
        cell.state = "unscanned";
        cell.text = "!";
        cell.detail = song.summary.error;
        return cell;
    }

    const QString title = columnTitle(column);
    std::vector<const StarScoreFileArrangement*> found;
    for (const StarScoreFileArrangement& a : song.summary.arrangementList) {
        if (a.column == column) {
            found.push_back(&a);
        }
    }
    if (found.empty()) {
        cell.state = "missing";
        cell.text = "—";
        cell.action = muse::qtrc("starscore", "Write the %1 arrangement").arg(title);
        cell.detail = muse::qtrc("starscore", "There's no %1 arrangement in this song yet.").arg(title);
        cell.closeness = -1;
        return cell;
    }

    // Several arrangements for one column (e.g. an older "all keys" copy): the one closest to done counts
    const StarScoreFileArrangement* best = found.front();
    for (const StarScoreFileArrangement* a : found) {
        const bool doneA = song.legacy ? a->audited : a->status == int(StarScoreStatus::Finished);
        const bool doneB = song.legacy ? best->audited : best->status == int(StarScoreStatus::Finished);
        if ((doneA && !doneB) || (doneA == doneB && a->status > best->status)) {
            best = a;
        }
    }
    cell.status = best->status;

    if (song.legacy) {
        if (best->audited) {
            cell.state = "done";
            cell.text = "✓";
            cell.detail = muse::qtrc("starscore", "Audited on %1").arg(best->auditedDate);
            return cell;
        }
        cell.state = "todo";
        cell.audit = true;
        cell.action = muse::qtrc("starscore", "Audit the %1 arrangement").arg(title);
        if (best->changedSinceAudit) {
            cell.text = muse::qtrc("starscore", "changed");
            cell.detail = muse::qtrc("starscore", "Audited on %1, but the music has changed since.").arg(best->auditedDate);
        } else if (best->openIssues > 0) {
            cell.text = QString::number(best->openIssues);
            cell.detail = muse::qtrc("starscore", "%n thing(s) to look at, then mark it audited.", nullptr, best->openIssues);
        } else {
            cell.text = muse::qtrc("starscore", "audit");
            cell.detail = muse::qtrc("starscore", "No issues found: listen through it and mark it audited.");
        }
        cell.closeness = 1000 - std::min(best->openIssues, 999);
        return cell;
    }

    if (best->status == int(StarScoreStatus::Finished)) {
        cell.state = "done";
        cell.text = "✓";
        cell.detail = muse::qtrc("starscore", "Finished");
        return cell;
    }
    cell.state = "todo";
    cell.text = statusName(best->status);
    cell.action = muse::qtrc("starscore", "Finish the %1 arrangement").arg(title);
    cell.detail = best->unfinished.isEmpty() ? statusName(best->status) : best->unfinished.join("; ");
    cell.closeness = best->status * 100;
    return cell;
}

void DashboardModel::rebuild()
{
    m_tiers.clear();
    m_tasks.clear();
    m_rows.clear();
    m_taskPaths.clear();
    m_taskAudit.clear();
    m_done = 0;
    m_cells = 0;

    // Grid rows: originals, then covers; each by how often it's played, then by name
    std::vector<const Song*> ordered;
    for (const Song& s : m_songs) {
        ordered.push_back(&s);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const Song* a, const Song* b) {
        if (a->cover != b->cover) {
            return !a->cover;
        }
        if (a->plays != b->plays) {
            return a->plays > b->plays;
        }
        return QString::localeAwareCompare(a->title, b->title) < 0;
    });
    for (const Song* s : ordered) {
        QVariantList cells;
        int doneHere = 0;
        for (const Column& c : COLUMNS) {
            const Cell cell = cellFor(*s, QString::fromUtf8(c.key));
            doneHere += cell.state == "done" ? 1 : 0;
            cells << QVariantMap {
                { "key", QString::fromUtf8(c.key) }, { "state", cell.state }, { "text", cell.text },
                { "tip", cell.action.isEmpty() ? cell.detail : cell.action + " — " + cell.detail },
                { "status", cell.status }, { "audit", cell.audit },
            };
        }
        const StarScoreAuditFileSummary& sum = s->summary;
        m_rows << QVariantMap {
            { "title", s->title }, { "path", s->path }, { "cover", s->cover }, { "legacy", s->legacy }, { "plays", s->plays },
            { "scanned", s->scanned }, { "cells", cells }, { "done", doneHere }, { "openIssues", sum.openIssues },
            { "likely", sum.likelyErrors },
            { "listen", sum.listenSteps > 0 ? QString("%1/%2").arg(sum.listenApproved).arg(sum.listenSteps) : QString() },
            { "error", sum.error },
        };
    }

    // Tiers in priority order; within one, the most-played songs first, then the ones closest to done
    int rank = 0;
    for (const Tier& t : TIERS) {
        const QString column = QString::fromUtf8(t.column);
        struct Item {
            const Song* song;
            Cell cell;
        };
        std::vector<Item> items;
        for (const Song& s : m_songs) {
            if (s.cover == t.cover) {
                items.push_back({ &s, cellFor(s, column) });
            }
        }
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            if (a.song->plays != b.song->plays) {
                return a.song->plays > b.song->plays;
            }
            if (a.cell.closeness != b.cell.closeness) {
                return a.cell.closeness > b.cell.closeness;
            }
            return QString::localeAwareCompare(a.song->title, b.song->title) < 0;
        });

        int done = 0;
        QString next;
        for (const Item& it : items) {
            if (it.cell.state == "done") {
                ++done;
                continue;
            }
            if (it.cell.state == "unscanned") {
                continue;
            }
            if (next.isEmpty()) {
                next = it.song->title;
            }
            ++rank;
            m_taskPaths.push_back(it.song->path);
            m_taskAudit.push_back(it.cell.audit);
            m_tasks << QVariantMap {
                { "rank", rank }, { "song", it.song->title }, { "action", it.cell.action }, { "detail", it.cell.detail },
                { "tier", QString("%1 · %2").arg(t.cover ? muse::qtrc("starscore", "Covers") : muse::qtrc("starscore", "Originals"),
                                                  columnTitle(column)) },
                { "state", it.cell.state }, { "status", it.cell.status }, { "audit", it.cell.audit },
                { "plays", it.song->plays },
            };
        }
        m_done += done;
        m_cells += int(items.size());
        m_tiers << QVariantMap {
            { "title", QString("%1 · %2").arg(t.cover ? muse::qtrc("starscore", "Covers") : muse::qtrc("starscore", "Originals"),
                                              columnTitle(column)) },
            { "done", done }, { "total", int(items.size()) }, { "next", next },
        };
    }
    emit changed();
}

void DashboardModel::openSong(const QString& path, bool withAudit)
{
    if (path.isEmpty()) {
        return;
    }
    auto d = dispatcher();
    auto docks = dockWindowProvider();
    QTimer::singleShot(0, qApp, [d, path]() {
        d->dispatch("starscore-audit-open", muse::actions::ActionData::make_arg1<QUrl>(QUrl::fromLocalFile(path)));
    });
    if (withAudit) {
        QTimer::singleShot(2500, qApp, [d, docks]() {
            if (docks && docks->window() && !docks->window()->isDockOpen("starscoreAuditPanel")) {
                d->dispatch("toggle-starscore-audit");
            }
        });
    }
}

void DashboardModel::openTask(int index)
{
    if (index < 0 || index >= int(m_taskPaths.size())) {
        return;
    }
    openSong(m_taskPaths[index], m_taskAudit[index]);
}

void DashboardModel::auditInOrder()
{
    QStringList paths;
    for (size_t i = 0; i < m_taskPaths.size(); ++i) {
        if (m_taskAudit[i] && !paths.contains(m_taskPaths[i])) {
            paths << m_taskPaths[i];
        }
    }
    if (paths.isEmpty()) {
        return;
    }
    auto ss = starScore();
    QTimer::singleShot(0, qApp, [ss, paths]() {
        if (ss) {
            ss->startAuditWalk(paths);
        }
    });
}
