/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: keeping recordings.json up to date (setlist.fm, YouTube, play counts)
 */
#include "orgrecordings.h"

#include <memory>
#include <set>

#include <QJsonArray>
#include <QJsonDocument>

#include "orghtml.h"

namespace mu::project::starscore::org {
static QJsonObject setlistState(const QJsonObject& recordings)
{
    return recordings.value("setlistfm").toObject();
}

static QJsonObject showToJson(const SetlistShow& s, const QString& status)
{
    return QJsonObject { { "status", status }, { "date", s.date.toString(Qt::ISODate) }, { "venue", s.venue }, { "city", s.city },
                         { "title", s.title } };
}

static std::set<QString> recordedDates(const QJsonObject& recordings)
{
    std::set<QString> dates;
    for (const QJsonValue& v : recordings.value("shows").toArray()) {
        const QJsonObject s = v.toObject();
        if (s.value("band").toString() == "Starsign") {
            dates.insert(s.value("date").toString());
        }
    }
    return dates;
}

void baselineShows(QJsonObject& recordings, const std::vector<SetlistShow>& shows, const QDate& today)
{
    QJsonObject st = setlistState(recordings);
    QJsonObject seen = st.value("seen").toObject();
    for (const SetlistShow& s : shows) {
        if (s.date < today && !seen.contains(s.url)) {
            seen[s.url] = showToJson(s, "baseline");
        }
    }
    st["seen"] = seen;
    st["baselined"] = today.toString(Qt::ISODate);
    recordings["setlistfm"] = st;
}

namespace {
struct CheckState {
    QJsonObject recordings;
    QDate today;
    Fetcher fetch;
    std::function<void(OnlineResult)> done;
    OnlineResult result;
    std::vector<SetlistShow> candidates;
    size_t next = 0;
};

void fetchList(const Fetcher& fetch, const QString& url, std::function<void(std::vector<SetlistShow>, int pages)> done)
{
    fetch(url, false, [fetch, url, done](const QString& html, int) {
        std::vector<SetlistShow> shows = parseSetlistList(html);
        if (!shows.empty()) {
            done(shows, parseSetlistPageCount(html));
            return;
        }
        // blocked or built by script: try a real browser view
        fetch(url, true, [done](const QString& html2, int) {
            done(parseSetlistList(html2), parseSetlistPageCount(html2));
        });
    });
}

void fetchStats(std::shared_ptr<CheckState> st)
{
    const int y = st->today.year();
    st->result.year = y;
    auto stats = [st](int year, std::function<void(QMap<QString, int>)> got) {
        const QString url = QString("%1?year=%2").arg(SETLISTFM_STATS).arg(year);
        st->fetch(url, false, [st, url, got](const QString& html, int) {
            QMap<QString, int> m = parseSetlistStats(html);
            if (!m.isEmpty() || html.contains("songName")) {
                got(m);
                return;
            }
            st->fetch(url, true, [got](const QString& html2, int) { got(parseSetlistStats(html2)); });
        });
    };
    stats(y, [st, stats, y](QMap<QString, int> thisYear) {
        st->result.thisYear = thisYear;
        stats(y - 1, [st](QMap<QString, int> lastYear) {
            st->result.lastYear = lastYear;
            st->result.log << QString("Play counts read from setlist.fm: %1 songs this year, %2 last year.")
                .arg(st->result.thisYear.size()).arg(st->result.lastYear.size());
            st->done(st->result);
        });
    });
}

void fetchNextSetlist(std::shared_ptr<CheckState> st)
{
    if (st->next >= st->candidates.size()) {
        fetchStats(st);
        return;
    }
    const SetlistShow show = st->candidates[st->next++];
    st->fetch(show.url, false, [st, show](const QString& html, int) {
        NewShow ns;
        ns.show = show;
        const SetlistDetail d = parseSetlistPage(html);
        ns.setlist = d.songs.isEmpty() ? show.songs : d.songs;
        const QJsonObject seen = setlistState(st->recordings).value("seen").toObject();
        ns.askedBefore = seen.value(show.url).toObject().value("status").toString() == "later";
        st->result.newShows.push_back(ns);
        fetchNextSetlist(st);
    });
}
}

void checkSetlistFm(const QJsonObject& recordings, const QDate& today, const Fetcher& fetch, std::function<void(OnlineResult)> done)
{
    auto st = std::make_shared<CheckState>();
    st->recordings = recordings;
    st->today = today;
    st->fetch = fetch;
    st->done = done;
    const QJsonObject seen = setlistState(recordings).value("seen").toObject();

    fetchList(fetch, SETLISTFM_ARTIST, [st, seen](std::vector<SetlistShow> shows, int pages) {
        if (shows.empty()) {
            st->result.log << "setlist.fm couldn't be reached, so no new shows were looked for and play counts weren't refreshed.";
            st->done(st->result);
            return;
        }
        st->result.reached = true;
        if (seen.isEmpty()) {
            // first run: note every show on every page, ask about none
            st->result.firstRun = true;
            st->result.allShows = shows;
            auto remaining = std::make_shared<int>(std::max(0, std::min(pages, 20) - 1));
            if (*remaining == 0) {
                fetchStats(st);
                return;
            }
            for (int p = 2; p <= std::min(pages, 20); ++p) {
                fetchList(st->fetch, QString("%1?page=%2").arg(SETLISTFM_ARTIST).arg(p), [st, remaining](std::vector<SetlistShow> more, int) {
                    st->result.allShows.insert(st->result.allShows.end(), more.begin(), more.end());
                    if (--*remaining == 0) {
                        st->result.log << QString("First look at setlist.fm: %1 shows noted; only shows added from now on will be asked about.")
                            .arg(st->result.allShows.size());
                        fetchStats(st);
                    }
                });
            }
            return;
        }
        const std::set<QString> recorded = recordedDates(st->recordings);
        std::set<QString> listed;
        for (const SetlistShow& s : shows) {
            listed.insert(s.url);
            const QString status = seen.value(s.url).toObject().value("status").toString();
            if (s.date < st->today && (status.isEmpty() || status == "later")) {
                if (status.isEmpty() && recorded.count(s.date.toString(Qt::ISODate))) {
                    st->result.allShows.push_back(s);    // already in recordings.json: noted, not asked about
                    continue;
                }
                st->candidates.push_back(s);
            }
        }
        // shows skipped earlier that have dropped off the first page
        for (auto it = seen.begin(); it != seen.end(); ++it) {
            const QJsonObject o = it.value().toObject();
            if (o.value("status").toString() == "later" && !listed.count(it.key())) {
                SetlistShow s;
                s.url = it.key();
                s.date = QDate::fromString(o.value("date").toString(), Qt::ISODate);
                s.venue = o.value("venue").toString();
                s.city = o.value("city").toString();
                s.title = o.value("title").toString();
                st->candidates.push_back(s);
            }
        }
        std::sort(st->candidates.begin(), st->candidates.end(), [](const SetlistShow& a, const SetlistShow& b) { return a.date < b.date; });
        fetchNextSetlist(st);
    });
}

void readVideos(const std::vector<std::pair<NewShow, QString> >& shows, const QJsonObject& recordings, const Fetcher& fetch,
                std::function<void(std::vector<ShowVideo>)> done)
{
    struct State {
        std::vector<std::pair<NewShow, QString> > shows;
        std::vector<ShowVideo> out;
        size_t next = 0;
        QMap<QString, QString> aliases;
        QStringList skip;
    };
    auto st = std::make_shared<State>();
    st->shows = shows;
    const QJsonObject al = recordings.value("aliases").toObject();
    for (auto it = al.begin(); it != al.end(); ++it) {
        st->aliases[it.key()] = it.value().toString();
    }
    for (const QJsonValue& v : recordings.value("skipNames").toArray()) {
        st->skip << v.toString();
    }
    auto step = std::make_shared<std::function<void()> >();
    *step = [st, step, fetch, done]() {
        if (st->next >= st->shows.size()) {
            done(st->out);
            *step = nullptr;    // break the cycle
            return;
        }
        const auto [show, link] = st->shows[st->next++];
        const QString id = youTubeId(link);
        if (id.isEmpty()) {
            ShowVideo v;
            v.show = show;
            st->out.push_back(v);
            (*step)();
            return;
        }
        const QString url = "https://www.youtube.com/watch?v=" + id;
        auto finish = [st, step, show](const QString& html) {
            ShowVideo v;
            v.show = show;
            v.ok = parseYouTubeWatchPage(html, v.video);
            if (v.ok) {
                for (const Timestamp& t : parseTimestamps(v.video.description)) {
                    v.songs.push_back(matchTimestamp(t, st->aliases, st->skip));
                }
            }
            st->out.push_back(v);
            (*step)();
        };
        fetch(url, false, [fetch, url, finish](const QString& html, int) {
            YouTubeVideo probe;
            if (parseYouTubeWatchPage(html, probe)) {
                finish(html);
            } else {
                fetch(url, true, [finish](const QString& html2, int) { finish(html2); });
            }
        });
    };
    (*step)();
}

QString newRecordingId(const QJsonObject& recordings, const QString& prefix)
{
    std::set<QString> used;
    for (const char* key : { "performances", "releases" }) {
        for (const QJsonValue& v : recordings.value(key).toArray()) {
            used.insert(v.toObject().value("id").toString());
        }
    }
    for (const QJsonValue& v : recordings.value("deleted").toArray()) {
        used.insert(v.toString());
    }
    int n = 1;
    for (const QString& u : used) {
        if (u.startsWith(prefix)) {
            n = std::max(n, u.mid(prefix.size()).toInt() + 1);
        }
    }
    return QString("%1%2").arg(prefix).arg(n, 4, 10, QChar('0'));
}

QStringList applyShows(QJsonObject& recordings, const std::vector<NewShow>& offered, const std::vector<ShowAnswer>& answers,
                       const std::vector<ShowVideo>& videos, const QDate& today, QStringList& changedCodes)
{
    QStringList log;
    QJsonObject st = setlistState(recordings);
    QJsonObject seen = st.value("seen").toObject();
    QJsonArray shows = recordings.value("shows").toArray();
    QJsonArray perfs = recordings.value("performances").toArray();
    QJsonObject aliases = recordings.value("aliases").toObject();
    QJsonArray skipNames = recordings.value("skipNames").toArray();
    QSet<QString> aliasNorm, skipNorm;
    for (auto it = aliases.begin(); it != aliases.end(); ++it) {
        aliasNorm.insert(normalizeName(it.key()));
    }
    for (const QJsonValue& v : skipNames) {
        skipNorm.insert(normalizeName(v.toString()));
    }
    const QString todayIso = today.toString(Qt::ISODate);

    for (const NewShow& ns : offered) {
        const SetlistShow& s = ns.show;
        auto a = std::find_if(answers.begin(), answers.end(), [&](const ShowAnswer& x) { return x.url == s.url; });
        const ShowAnswer::Kind kind = a == answers.end() ? ShowAnswer::Later : a->kind;
        const QString when = prettyDate(s.date.toString(Qt::ISODate));
        if (kind == ShowAnswer::NotFilmed) {
            seen[s.url] = showToJson(s, "not filmed");
            log << QString("%1, %2: not filmed.").arg(when, esc(s.venue));
            continue;
        }
        auto v = std::find_if(videos.begin(), videos.end(), [&](const ShowVideo& x) { return x.show.show.url == s.url; });
        if (kind == ShowAnswer::Later || v == videos.end() || !v->ok) {
            seen[s.url] = showToJson(s, "later");
            log << (kind == ShowAnswer::Link ? QString("%1, %2: the video couldn&rsquo;t be read (%3); StarScore will ask again next time.")
                    .arg(when, esc(s.venue), esc(a->link))
                    : QString("%1, %2: skipped for now; StarScore will ask again next time.").arg(when, esc(s.venue)));
            continue;
        }
        const QString showId = "ss-" + s.date.toString(Qt::ISODate);
        QJsonArray unmatched;
        int added = 0;
        for (const MatchedTimestamp& m : v->songs) {
            const QString norm = normalizeName(m.name);
            if (m.code == "-") {
                if (!skipNorm.contains(norm) && !aliasNorm.contains(norm)) {
                    skipNames.append(m.name);
                    skipNorm.insert(norm);
                }
                continue;
            }
            if (m.code.isEmpty()) {
                unmatched.append(QJsonObject { { "name", m.name }, { "seconds", m.seconds } });
                continue;
            }
            if (!aliasNorm.contains(norm)) {
                aliases[m.name] = m.code;
                aliasNorm.insert(norm);
            }
            QJsonObject p { { "id", newRecordingId(QJsonObject { { "performances", perfs } }, "p") }, { "song", m.code }, { "show", showId },
                            { "seconds", m.seconds }, { "variant", m.variant.isEmpty() ? QJsonValue() : QJsonValue(m.variant) },
                            { "name", m.name }, { "rating", QJsonValue() }, { "modified", todayIso } };
            perfs.append(p);
            if (!changedCodes.contains(m.code)) {
                changedCodes << m.code;
            }
            ++added;
        }
        QJsonObject show { { "id", showId }, { "band", "Starsign" }, { "date", s.date.toString(Qt::ISODate) }, { "approx", false },
                           { "venue", s.venue }, { "city", s.city }, { "video", v->video.id }, { "setlistfm", s.url },
                           { "timestamps", added > 0 }, { "status", "filmed" }, { "title", v->video.title },
                           { "setlist", QJsonArray::fromStringList(ns.setlist) }, { "added", todayIso } };
        if (!unmatched.isEmpty()) {
            show["unmatched"] = unmatched;
        }
        shows.append(show);
        seen[s.url] = QJsonObject { { "status", "added" }, { "show", showId }, { "date", s.date.toString(Qt::ISODate) }, { "venue", s.venue } };
        log << QString("%1, %2: added with %3 from the video&rsquo;s timestamps%4.")
            .arg(when, esc(s.venue), plural(added, "song"),
                 unmatched.isEmpty() ? QString() : QString(" (%1 not matched to a song)").arg(unmatched.size()));
    }
    // shows already in recordings.json that setlist.fm lists: noted, never asked about
    st["seen"] = seen;
    st["checked"] = todayIso;
    recordings["setlistfm"] = st;
    recordings["shows"] = shows;
    recordings["performances"] = perfs;
    recordings["aliases"] = aliases;
    recordings["skipNames"] = skipNames;
    return log;
}

void mapPlayCounts(const QJsonObject& recordings, const QMap<QString, int>& byName, std::map<QString, int>& byCode, QStringList& notInLibrary)
{
    QMap<QString, QString> aliases;
    const QJsonObject al = recordings.value("aliases").toObject();
    for (auto it = al.begin(); it != al.end(); ++it) {
        aliases[it.key()] = it.value().toString();
    }
    QStringList skip;
    for (const QJsonValue& v : recordings.value("skipNames").toArray()) {
        skip << v.toString();
    }
    const QJsonObject notIn = recordings.value("notInLibrary").toObject();
    for (auto it = byName.begin(); it != byName.end(); ++it) {
        // a medley ("Live Strong / Strasbourg") counts for its song
        const MatchedTimestamp m = matchTimestamp({ 0, it.key() }, aliases, skip);
        if (!m.code.isEmpty() && m.code != "-") {
            byCode[m.code] += it.value();
        } else if (m.code.isEmpty() && !notInLibrary.contains(it.key())) {
            notInLibrary << it.key();
        }
    }
    Q_UNUSED(notIn);
}

void storePlayCounts(QJsonObject& recordings, const OnlineResult& r, const QDate& today)
{
    if (!r.reached) {
        return;
    }
    auto toObj = [](const QMap<QString, int>& m) {
        QJsonObject o;
        for (auto it = m.begin(); it != m.end(); ++it) {
            o[it.key()] = it.value();
        }
        return o;
    };
    QJsonObject pc = recordings.value("playCounts").toObject();
    pc[QString::number(r.year)] = toObj(r.thisYear);
    pc[QString::number(r.year - 1)] = toObj(r.lastYear);
    pc["fetched"] = today.toString(Qt::ISODate);
    recordings["playCounts"] = pc;
}

// ------------------------------------------------------------------ one song's copy
QJsonObject songRecordings(const QJsonObject& recordings, const QString& code)
{
    QJsonArray perfs, rels;
    QJsonObject shows;
    std::map<QString, QJsonObject> allShows;
    for (const QJsonValue& v : recordings.value("shows").toArray()) {
        allShows[v.toObject().value("id").toString()] = v.toObject();
    }
    for (const QJsonValue& v : recordings.value("performances").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("song").toString() == code) {
            perfs.append(p);
            const QString sid = p.value("show").toString();
            if (allShows.count(sid)) {
                shows[sid] = allShows[sid];
            }
        }
    }
    for (const QJsonValue& v : recordings.value("releases").toArray()) {
        if (v.toObject().value("song").toString() == code) {
            rels.append(v);
        }
    }
    return QJsonObject { { "code", code }, { "performances", perfs }, { "releases", rels }, { "shows", shows } };
}

bool mergeSongRecordings(QJsonObject& recordings, const QString& code, const QJsonObject& copy)
{
    bool changed = false;
    QJsonArray deleted = recordings.value("deleted").toArray();
    QSet<QString> gone;
    for (const QJsonValue& v : deleted) {
        gone.insert(v.toString());
    }
    for (const QJsonValue& v : copy.value("deleted").toArray()) {
        if (!gone.contains(v.toString())) {
            gone.insert(v.toString());
            deleted.append(v);
            changed = true;
        }
    }
    auto mergeList = [&](const char* key) {
        QJsonArray master = recordings.value(key).toArray();
        std::map<QString, int> index;
        for (int i = 0; i < master.size(); ++i) {
            index[master[i].toObject().value("id").toString()] = i;
        }
        for (const QJsonValue& v : copy.value(key).toArray()) {
            QJsonObject e = v.toObject();
            const QString id = e.value("id").toString();
            if (id.isEmpty() || gone.contains(id)) {
                continue;
            }
            e["song"] = code;
            auto it = index.find(id);
            if (it == index.end()) {
                master.append(e);
                changed = true;
            } else if (e.value("modified").toString() > master[it->second].toObject().value("modified").toString()) {
                master[it->second] = e;
                changed = true;
            }
        }
        QJsonArray kept;
        for (const QJsonValue& v : master) {
            if (gone.contains(v.toObject().value("id").toString())) {
                changed = true;
            } else {
                kept.append(v);
            }
        }
        recordings[key] = kept;
    };
    mergeList("performances");
    mergeList("releases");
    // shows the copy refers to that the master doesn't have (added in the Recordings window)
    QJsonArray shows = recordings.value("shows").toArray();
    std::set<QString> have;
    for (const QJsonValue& v : shows) {
        have.insert(v.toObject().value("id").toString());
    }
    const QJsonObject cs = copy.value("shows").toObject();
    for (auto it = cs.begin(); it != cs.end(); ++it) {
        if (!have.count(it.key())) {
            shows.append(it.value());
            changed = true;
        }
    }
    recordings["shows"] = shows;
    recordings["deleted"] = deleted;
    return changed;
}
}
