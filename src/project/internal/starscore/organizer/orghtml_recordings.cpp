/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: Recordings pages (the old recbuild.py, gen_recordings.py, gen_allrecordings.py).
 *
 * Data: recordings.json. Ratings are Joel's own (0–5 stars per live take, set in StarScore's Recordings window);
 * the Google Form voting was never used, so the pages no longer link to it.
 */
#include "orghtml.h"

#include <algorithm>

#include <QRegularExpression>

namespace mu::project::starscore::org {
static const char* REC_CSS = R"CSS(
a { color:#1a4f8a; text-decoration:none; }
.hero{background:#16213e;color:#fff;margin:0 0 11px;padding:14px 20px 12px;border-radius:8px;
      display:flex;justify-content:space-between;align-items:flex-start;}
.hero h1{color:#fff;font-size:21pt;margin:0;letter-spacing:-.3px;}
.hero .sub{color:#a9b4d0;font-size:9.6pt;margin-top:3px;}
.hero.sw{background:#2d1b3d;} .hero.sw .sub{color:#c2acd4;}
.hero .code{font-family:'SF Mono',Menlo,Consolas,monospace;font-size:15pt;font-weight:700;
      color:#16213e;background:#c8a24a;padding:3px 10px;border-radius:5px;letter-spacing:1px;}
.lead{font-size:9.6pt;color:#3d4356;margin:0 0 7px;}
.lead b{color:#16213e;}
table.perf{width:100%;border-collapse:collapse;table-layout:fixed;}
table.perf th{font-size:7.6pt;text-transform:uppercase;letter-spacing:.7px;color:#6d7387;
   text-align:left;padding:0 6px 3px 0;border-bottom:1.5px solid #16213e;font-weight:700;white-space:nowrap;}
table.perf td{padding:2.1px 6px 2.1px 0;font-size:9pt;border-bottom:1px solid #f0f2f7;vertical-align:middle;}
table.perf tr:last-child td{border-bottom:none;}
td.dt{white-space:nowrap;color:#16213e;font-weight:600;width:84px;}
td.vn{color:#3d4356;overflow:hidden;text-overflow:ellipsis;white-space:nowrap;}
td.vn.wide{white-space:normal;overflow:visible;text-overflow:clip;}
td.ts{white-space:nowrap;text-align:right;width:52px;
      font-family:'SF Mono',Menlo,Consolas,monospace;font-size:8.4pt;color:#16213e;}
td.ts a{color:#12457f;font-weight:700;text-decoration:underline;}
td.rate{white-space:nowrap;width:86px;text-align:right;}
td.go{white-space:nowrap;width:118px;padding-right:0;text-align:right;overflow:hidden;}
a.go{display:inline-block;background:#eaf1fa;border:1px solid #a9c4e4;color:#12457f;
     text-decoration:underline;font-weight:700;font-size:7.6pt;padding:1.4px 7px 1.8px;
     border-radius:4px;white-space:nowrap;max-width:100%;}
a.go.long{white-space:normal;line-height:1.2;text-align:center;padding:1.4px 6px 2.4px;}
.stars{display:inline-block;white-space:nowrap;vertical-align:middle;font-size:10.5pt;letter-spacing:.4px;line-height:1;}
.stars .on{color:#c8a24a;} .stars .off{color:#d7dbe4;}
.ln{padding:1px 0;} .ln + .ln{padding-top:3px;}
.play{display:inline-block;width:0;height:0;margin-right:4px;vertical-align:middle;
      border-left:4.5px solid #12457f;border-top:3.2px solid transparent;border-bottom:3.2px solid transparent;}
tr.sec td{padding:8px 0 3px;border-bottom:1.5px solid #16213e;font-size:8pt;font-weight:700;
   text-transform:uppercase;letter-spacing:.8px;color:#16213e;}
tr.sec.first td{padding-top:0;}
tr.sec.swsec td{border-bottom-color:#5d3f7a;color:#5d3f7a;}
tr.sec td span.hint{text-transform:none;letter-spacing:0;font-weight:400;color:#8b90a0;font-size:8pt;}
td.best{background:#fdf8ec;}
.var{color:#8a6d1f;font-size:8pt;font-style:italic;}
.none{background:#f7f8fb;border-left:3px solid #c8a24a;padding:12px 14px;border-radius:0 5px 5px 0;
      font-size:10pt;color:#3d4356;margin-top:6px;}
.fnote{margin-top:10px;background:#f7f8fb;border-left:3px solid #c8a24a;padding:8px 12px;
       border-radius:0 5px 5px 0;font-size:8.8pt;color:#3d4356;}
.fnote b{color:#16213e;}
.foot2{margin-top:11px;padding-top:7px;border-top:1px solid #dfe3ec;color:#8b90a0;font-size:7.6pt;
       display:flex;justify-content:space-between;align-items:center;gap:12px;}
.foot2 a{color:#8b90a0;}
.plst{font-size:8.2pt;color:#3d4356;white-space:nowrap;}
)CSS";

void RecordingsData::load(const QJsonObject& o)
{
    root = o;
    venueShort = o.value("venueShort").toObject();    // kept here, not in a global: a run and the Recordings window can both load
    shows.clear();
    for (const QJsonValue& v : o.value("shows").toArray()) {
        const QJsonObject s = v.toObject();
        shows[s.value("id").toString()] = s;
    }
}

QJsonArray RecordingsData::performancesOf(const QString& code) const
{
    QJsonArray out;
    for (const QJsonValue& v : root.value("performances").toArray()) {
        if (v.toObject().value("song").toString() == code) {
            out.append(v);
        }
    }
    return out;
}

QJsonArray RecordingsData::releasesOf(const QString& code) const
{
    QJsonArray out;
    for (const QJsonValue& v : root.value("releases").toArray()) {
        if (v.toObject().value("song").toString() == code) {
            out.append(v);
        }
    }
    return out;
}

int RecordingsData::filmedStarsignShows() const
{
    int n = 0;
    for (const auto& [id, s] : shows) {
        n += s.value("band").toString() == "Starsign" && s.value("timestamps").toBool() && s.value("status").toString() == "filmed";
    }
    return n;
}

static QString watchUrl(const QString& vid, int seconds = -1)
{
    return seconds < 0 ? "https://www.youtube.com/watch?v=" + vid
           : QString("https://www.youtube.com/watch?v=%1&t=%2s").arg(vid).arg(seconds);
}

static QString button(const QString& url, const QString& label = "Click here to watch")
{
    return QString("<a class=\"%1\" href=\"%2\"><span class=\"play\"></span>%3</a>")
           .arg(label.size() > 20 ? "go long" : "go", esc(url), label);
}

static QString stars(int rating)
{
    if (rating <= 0) {
        return "<span class=\"stars\"><span class=\"off\">&#9733;&#9733;&#9733;&#9733;&#9733;</span></span>";
    }
    return QString("<span class=\"stars\"><span class=\"on\">%1</span><span class=\"off\">%2</span></span>")
           .arg(QString("&#9733;").repeated(std::min(rating, 5)), QString("&#9733;").repeated(5 - std::min(rating, 5)));
}

static QString secRow(const QString& title, const QString& hint, bool& first, bool sw = false)
{
    const QString h = hint.isEmpty() ? QString() : " <span class=\"hint\">" + hint + "</span>";
    const QString cls = QString("sec") + (first ? " first" : "") + (sw ? " swsec" : "");
    first = false;
    return QString("<tr class=\"%1\"><td colspan=\"5\">%2%3</td></tr>").arg(cls, title, h);
}

struct Take {
    QString date, venue, video, variant;
    bool approx = false;
    int seconds = 0;
    int rating = 0;
};

//! A song's performances, one group per show; a show's later cues (an encore, the song after a separate intro)
//! become extra buttons
static std::vector<std::vector<Take> > groupedTakes(const RecordingsData& rec, const QJsonArray& perfs)
{
    std::vector<std::vector<Take> > groups;
    std::map<QString, int> index;
    for (const QJsonValue& v : perfs) {
        const QJsonObject p = v.toObject();
        auto s = rec.shows.find(p.value("show").toString());
        if (s == rec.shows.end() || s->second.value("video").toString().isEmpty()) {
            continue;
        }
        Take t;
        t.date = s->second.value("date").toString();
        t.approx = s->second.value("approx").toBool();
        t.venue = s->second.value("venue").toString();
        t.video = s->second.value("video").toString();
        t.seconds = p.value("seconds").toInt();
        t.variant = p.value("variant").toString();
        t.rating = p.value("rating").toInt();
        const QString key = t.date + "|" + t.video;
        if (!index.count(key)) {
            index[key] = int(groups.size());
            groups.emplace_back();
        }
        groups[index[key]].push_back(t);
    }
    for (auto& g : groups) {
        std::sort(g.begin(), g.end(), [](const Take& a, const Take& b) { return a.seconds < b.seconds; });
    }
    std::stable_sort(groups.begin(), groups.end(), [](const std::vector<Take>& a, const std::vector<Take>& b) {
        int ra = 0, rb = 0;
        for (const Take& t : a) {
            ra = std::max(ra, t.rating);
        }
        for (const Take& t : b) {
            rb = std::max(rb, t.rating);
        }
        return ra != rb ? ra > rb : a[0].date > b[0].date;
    });
    return groups;
}

static void takeRows(const RecordingsData& rec, const std::vector<std::vector<Take> >& groups, QString& b)
{
    static const QRegularExpression onlySuffix("\\s+only$");
    for (const auto& g : groups) {
        const Take& t0 = g[0];
        const QString note = g.size() == 1 ? t0.variant : QString();
        const QString v = note.isEmpty() ? QString() : " <span class=\"var\">" + esc(note) + "</span>";
        std::vector<std::pair<const Take*, QString> > cues { { &t0, QString() } };
        for (size_t i = 1; i < g.size(); ++i) {
            if (g[i].variant.isEmpty()) {
                QString skip = t0.variant.isEmpty() ? QString("intro") : t0.variant;
                skip.remove(onlySuffix);
                cues.push_back({ &g[i], "skip " + skip });
            } else if (g[i].variant == "encore") {
                cues.push_back({ &g[i], "encore" });
            }
        }
        QString times, links;
        int rating = 0;
        for (const Take& t : g) {
            rating = std::max(rating, t.rating);
        }
        for (const auto& [t, lab] : cues) {
            const QString deep = watchUrl(t->video, t->seconds);
            times += QString("<div class=\"ln\"><a href=\"%1\">%2</a></div>").arg(esc(deep), hhmmss(t->seconds));
            links += "<div class=\"ln\">" + button(deep, "Click here to watch" + (lab.isEmpty() ? QString() : " (" + esc(lab) + ")"))
                     + "</div>";
        }
        b += QString("<tr><td class=\"dt\">%1%2</td><td class=\"vn\">%3%4</td><td class=\"ts\">%5</td><td class=\"rate\">%6</td>"
                     "<td class=\"go\">%7</td></tr>")
             .arg(t0.approx ? "c. " : "", prettyDate(t0.date), esc(rec.venue(t0.venue)), v, times, stars(rating), links);
    }
}

QString recordingsBlock(const RecordingsData& rec, const QString& name, const QString& code, const QString& anchor,
                        bool swOnly, const QString& swName)
{
    QString b;
    const QString aid = anchor.isEmpty() ? QString() : " id=\"" + anchor + "\"";
    b += QString("<div class=\"hero%1\"%2><div><h1>%3</h1><div class=\"sub\">%4</div></div>%5</div>")
         .arg(swOnly ? " sw" : "", aid, esc(name),
              swOnly ? "Sweater Weather &middot; recordings" : "Recordings &middot; albums, sessions and live shows",
              code.isEmpty() ? QString() : "<div class=\"code\">" + esc(code) + "</div>");

    const QString key = swOnly ? "sw:" + swName : code;
    const auto groups = groupedTakes(rec, rec.performancesOf(key));
    std::vector<std::vector<Take> > swGroups;
    if (!swName.isEmpty() && !swOnly) {
        swGroups = groupedTakes(rec, rec.performancesOf("sw:" + swName));
    } else if (swOnly) {
        swGroups = groups;
    }
    std::vector<QJsonObject> album, wip, sessions, sources, swRel;
    for (const QJsonValue& v : rec.releasesOf(key)) {
        const QJsonObject r = v.toObject();
        const QString kind = r.value("kind").toString();
        (kind == "album" ? album : kind == "wip" ? wip : kind == "session" ? sessions : kind == "original" ? sources : swRel).push_back(r);
    }
    auto newestFirst = [](std::vector<QJsonObject>& v) {
        std::stable_sort(v.begin(), v.end(), [](const QJsonObject& a, const QJsonObject& b) {
            return a.value("date").toString() > b.value("date").toString();
        });
    };
    newestFirst(album);
    newestFirst(sessions);
    newestFirst(swRel);
    if (!album.empty()) {
        wip.clear();      // unreleased mixes only for songs with nothing released
    }
    const auto& liveGroups = swOnly ? std::vector<std::vector<Take> >() : groups;
    const int nSet = rec.filmedStarsignShows();

    if (album.empty() && wip.empty() && sessions.empty() && liveGroups.empty() && swGroups.empty() && swRel.empty() && sources.empty()) {
        b += QString("<div class=\"none\"><b>Nothing recorded yet.</b><br>No album take, no live session, and none of the "
                     "%1 filmed shows list it in their setlist.</div>").arg(nSet);
    } else {
        QStringList bits;
        if (!album.empty()) {
            bits << QString("<b>%1</b> album recording%2").arg(album.size()).arg(album.size() > 1 ? "s" : "");
        }
        if (!wip.empty()) {
            bits << "an unreleased studio mix";
        }
        if (!swRel.empty()) {
            bits << QString("<b>%1</b> Sweater Weather release%2").arg(swRel.size()).arg(swRel.size() > 1 ? "s" : "");
        }
        if (!sessions.empty()) {
            bits << QString("<b>%1</b> live session%2").arg(sessions.size()).arg(sessions.size() > 1 ? "s" : "");
        }
        if (!liveGroups.empty()) {
            bits << QString("<b>%1</b> filmed Starsign show%2").arg(liveGroups.size()).arg(liveGroups.size() > 1 ? "s" : "");
        }
        if (!swGroups.empty()) {
            bits << QString("<b>%1</b> filmed Sweater Weather show%2").arg(swGroups.size()).arg(swGroups.size() > 1 ? "s" : "");
        }
        QString lead = bits.join(", ");
        if (!lead.isEmpty() && lead.at(0).isLower()) {
            lead[0] = lead.at(0).toUpper();
        }
        b += "<p class=\"lead\">" + lead + ". Every blue button is a link. Live takes are listed best first (Joel's stars), "
             "newest first among equals.</p>";
        b += "<table class=\"perf\"><tr><th>Date</th><th>Source</th><th style=\"text-align:right;\">At</th>"
             "<th style=\"text-align:right;\">Stars</th><th style=\"text-align:right;\">Watch it</th></tr>";
        bool first = true;
        if (!sources.empty()) {
            b += secRow(sources.size() > 1 ? "The originals" : "The original", "not us &mdash; where the cover comes from", first);
            for (const QJsonObject& s : sources) {
                const QString note = s.value("note").toString();
                b += QString("<tr><td class=\"dt\">&mdash;</td><td class=\"vn wide\" colspan=\"3\">%1%2</td><td class=\"go\">%3</td></tr>")
                     .arg(esc(s.value("label").toString()), note.isEmpty() ? QString() : " <span class=\"var\">" + esc(note) + "</span>",
                          button(watchUrl(s.value("video").toString())));
            }
        }
        struct Kind {
            const std::vector<QJsonObject>* rows;
            QString title, hint;
            bool shade;
        };
        for (const Kind& k : { Kind { &album, "Album recording", "newest first", true },
                               Kind { &wip, "Studio recording", "album 2 &mdash; not released yet", true },
                               Kind { &swRel, "Sweater Weather release", "", true },
                               Kind { &sessions, "Live session", "", false } }) {
            if (k.rows->empty()) {
                continue;
            }
            b += secRow(k.title + (k.rows->size() > 1 ? "s" : ""), k.hint, first);
            const QString cls = k.shade ? " best" : "";
            for (const QJsonObject& r : *k.rows) {
                const QString note = r.value("note").toString();
                const QString label = esc(r.value("label").toString());
                const QString what = k.title.startsWith("Album") ? "<b>" + label + "</b> &mdash; album"
                                     : k.title.startsWith("Studio") ? QString("Work in progress")
                                     : k.title.startsWith("Sweater") ? "<b>" + label + "</b>" : QString("Live session");
                b += QString("<tr><td class=\"dt%1\">%2</td><td class=\"vn wide%1\" colspan=\"3\">%3%4</td><td class=\"go%1\">%5</td></tr>")
                     .arg(cls, prettyDate(r.value("date").toString()), what,
                          note.isEmpty() ? QString() : " <span class=\"var\">" + esc(note) + "</span>",
                          button(watchUrl(r.value("video").toString())));
            }
        }
        if (!liveGroups.empty()) {
            b += secRow("Live performances", QString("%1 of the %2 filmed shows").arg(liveGroups.size()).arg(nSet), first);
            takeRows(rec, liveGroups, b);
        }
        if (!swGroups.empty()) {
            b += secRow("Sweater Weather, live", swOnly ? QString() : QString("the tune before it was a Starsign tune"), first, true);
            takeRows(rec, swGroups, b);
        }
        b += "</table>";
    }
    for (const QJsonValue& v : rec.root.value("footnotes").toArray()) {
        const QJsonObject f = v.toObject();
        if (f.value("song").toString() != code || code.isEmpty()) {
            continue;
        }
        b += QString("<div class=\"fnote\"><b>%1</b> &mdash; %2. Released %3 on <b>%4</b>. &nbsp;%5</div>")
             .arg(esc(f.value("name").toString()), esc(f.value("why").toString()), prettyDate(f.value("date").toString()),
                  esc(f.value("album").toString()), button(watchUrl(f.value("video").toString())));
    }
    return b;
}

QString recordingsFooter(const RecordingsData& rec, const QDate& today, bool sw)
{
    const QJsonObject pl = rec.root.value("playlists").toObject();
    QStringList links;
    auto link = [&](const QString& url, const QString& label) {
        if (!url.isEmpty()) {
            links << QString("<a href=\"%1\">%2</a>").arg(esc(url), esc(label));
        }
    };
    link(pl.value("live").toString(), "live shows");
    link(pl.value("sessions").toString(), "sessions");
    const QJsonObject albums = pl.value("albums").toObject();
    for (auto it = albums.begin(); it != albums.end(); ++it) {
        link(it.value().toString(), it.key());
    }
    if (sw) {
        link(pl.value("swLive").toString(), "SW live");
        link(pl.value("swReleases").toString(), "SW releases");
    }
    return QString("<div class=\"foot2\"><span class=\"plst\">Playlists: %1</span><span>Made %2 &mdash; this PDF is "
                   "auto-generated, do not edit it by hand</span></div>")
           .arg(links.join(" &middot; "), prettyDate(today.toString(Qt::ISODate)));
}

static QString swNameFor(const RecordingsData& rec, const QString& code)
{
    const QJsonObject a = rec.root.value("swAdapted").toObject();
    for (auto it = a.begin(); it != a.end(); ++it) {
        if (it.value().toString() == code) {
            return it.key();
        }
    }
    return QString();
}

QString recordingsHtml(const RecordingsData& rec, const SongInfo& song, const QDate& today)
{
    const QString sw = swNameFor(rec, song.code);
    const QString body = recordingsBlock(rec, song.name, song.code, QString(), false, sw) + recordingsFooter(rec, today, !sw.isEmpty());
    return htmlPage(song.name + " — recordings", body, REC_CSS, "12mm 13mm 12mm 13mm");
}

static QString slug(const QString& name)
{
    static const QRegularExpression notAlnum("[^a-z0-9]+"), edgeDash("^-|-$");
    return "x" + name.toLower().replace(notAlnum, "-").remove(edgeDash);
}

QString allRecordingsHtml(const RecordingsData& rec, const Library& lib, const QDate& today)
{
    static const char* EXTRA = R"CSS(
.pg{page-break-after:always;break-after:page;}
.cover{padding-top:62mm;}
.cover h1{font-size:34pt;margin:0 0 6px;color:#16213e;letter-spacing:-1px;}
.cover .sub{font-size:12pt;color:#6d7387;margin-bottom:26px;}
.tiles{display:flex;gap:9px;margin-bottom:26px;}
.tiles .tile{background:#f6f7fb;border:1px solid #e3e7f0;border-radius:7px;padding:10px 13px;flex:1;}
.tiles .big{font-size:21pt;font-weight:700;color:#16213e;line-height:1;}
.tiles .lab{font-size:7.6pt;text-transform:uppercase;letter-spacing:.6px;color:#6d7387;margin-top:4px;}
.howto{background:#fdf8ec;border-left:3px solid #c8a24a;padding:11px 14px;border-radius:0 5px 5px 0;
       font-size:9.6pt;color:#3d4356;line-height:1.5;}
.howto b{color:#16213e;}
h2.idx{font-size:11pt;color:#16213e;border-bottom:1.5px solid #16213e;padding-bottom:3px;
       margin:0 0 7px;text-transform:uppercase;letter-spacing:.8px;}
.cols{display:flex;gap:16px;font-size:9pt;}
.cols>div{flex:1;min-width:0;}
.cols a{display:block;color:#1b2030;padding:1.6px 0;text-decoration:none;border-bottom:1px solid #f0f2f7;}
.grp{font-size:7.6pt;text-transform:uppercase;letter-spacing:.7px;color:#6d7387;margin:9px 0 3px;font-weight:700;}
.grp.top{margin-top:0;}
)CSS";
    static const QMap<QString, QString> CAT { { "orig", "Starsign originals" }, { "cover", "Covers" },
        { "vocal", "Covers (need a vocalist)" }, { "wip", "Works in progress" } };
    struct Entry {
        QString group, name, anchor, html;
    };
    std::vector<Entry> entries;
    std::vector<const SongInfo*> songs;
    for (const SongInfo& s : lib.songs) {
        if (s.code != "????") {
            songs.push_back(&s);
        }
    }
    std::stable_sort(songs.begin(), songs.end(), [](const SongInfo* a, const SongInfo* b) { return a->root < b->root; });
    for (const SongInfo* s : songs) {
        entries.push_back({ CAT.value(s->cat, "Other"), s->name, slug(s->name),
                            recordingsBlock(rec, s->name, s->code, slug(s->name), false, swNameFor(rec, s->code)) });
    }
    // Sweater Weather tunes that never became Starsign tunes
    QStringList swOnly;
    const QJsonObject adapted = rec.root.value("swAdapted").toObject();
    for (const QJsonValue& v : rec.root.value("performances").toArray()) {
        const QString song = v.toObject().value("song").toString();
        if (song.startsWith("sw:") && !adapted.contains(song.mid(3)) && !swOnly.contains(song.mid(3))) {
            swOnly << song.mid(3);
        }
    }
    swOnly.sort();
    const QJsonObject display = rec.root.value("swDisplay").toObject();
    for (const QString& sw : swOnly) {
        const QString disp = display.value(sw).toString(sw);
        entries.push_back({ "Sweater Weather only", disp, slug(sw), recordingsBlock(rec, disp, QString(), slug(sw), true, sw) });
    }

    int nPerf = rec.root.value("performances").toArray().size();
    int nStudio = 0, rated = 0;
    for (const QJsonValue& v : rec.root.value("releases").toArray()) {
        nStudio += v.toObject().value("kind").toString() != "original";
    }
    for (const QJsonValue& v : rec.root.value("performances").toArray()) {
        rated += v.toObject().value("rating").toInt() > 0;
    }

    QString b;
    b += QString("<div class=\"pg\"><div class=\"cover\"><h1>Every recording</h1><div class=\"sub\">Starsign and Sweater Weather "
                 "&middot; albums, sessions, live shows &middot; %1</div><div class=\"tiles\">"
                 "<div class=\"tile\"><div class=\"big\">%2</div><div class=\"lab\">Tunes</div></div>"
                 "<div class=\"tile\"><div class=\"big\">%3</div><div class=\"lab\">Studio &amp; session takes</div></div>"
                 "<div class=\"tile\"><div class=\"big\">%4</div><div class=\"lab\">Filmed performances</div></div>"
                 "<div class=\"tile\"><div class=\"big\">%5</div><div class=\"lab\">Rated so far</div></div></div>"
                 "<div class=\"howto\"><b>This is the combined copy.</b> Every Starsign song folder in <i>Sheets and Demos</i> "
                 "holds its own one-page version; this one adds the Sweater Weather tunes that never became Starsign tunes.<br>"
                 "<b>Stars.</b> Live takes carry your own rating, set in StarScore (Recordings window, one per song). "
                 "Rated takes sort best first; among equals the newest comes first.</div></div></div>")
         .arg(prettyDate(today.toString(Qt::ISODate))).arg(entries.size()).arg(nStudio).arg(nPerf).arg(rated);

    // contents in three columns, split evenly (WebKit doesn't break CSS columns across a printed page)
    std::vector<QString> items;
    QString last;
    for (const Entry& e : entries) {
        if (e.group != last) {
            items.push_back(QString("<div class=\"grp%1\">%2</div>").arg(items.empty() ? " top" : "", esc(e.group)));
            last = e.group;
        }
        items.push_back(QString("<a href=\"#%1\">%2</a>").arg(e.anchor, esc(e.name)));
    }
    b += "<div class=\"pg\"><h2 class=\"idx\">Contents</h2><div class=\"cols\">";
    const int per = int((items.size() + 2) / 3);
    for (int c = 0; c < 3; ++c) {
        b += "<div>";
        for (int i = c * per; i < std::min<int>((c + 1) * per, int(items.size())); ++i) {
            b += items[i];
        }
        b += "</div>";
    }
    b += "</div></div>";
    for (size_t i = 0; i < entries.size(); ++i) {
        const bool lastOne = i + 1 == entries.size();
        b += QString("<div class=\"%1\">%2%3</div>").arg(lastOne ? "" : "pg", entries[i].html,
                                                         lastOne ? recordingsFooter(rec, today, true) : QString());
    }
    return htmlPage("Every recording — Starsign & Sweater Weather", b, REC_CSS + QString(EXTRA), "12mm 13mm 12mm 13mm");
}
}
