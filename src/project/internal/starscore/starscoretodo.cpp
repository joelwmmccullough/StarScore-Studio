/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the open song's to-do list (the To-do panel).
 *
 * The work on a song in priority order: 3-Horn Section, bass + guitar, lead sheet, drums, 2-Horn Section,
 * 2-Horn Flexible, 3-Horn Flexible, keys, 4-Horn Section, the 1-Horn sheets, 5-, 6- and 7-Horn Sections,
 * percussion, Big Band, Marching Band (each with its full score marked Finished). Each step is as far along as its least finished part (the part's own
 * status; untagged, the status of a section set by hand that holds it). Drums, keys and percussion that read
 * the lead sheet are as far along as the lead sheet.
 */
#include "starscoreservice.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"

#include <QDate>
#include <QLocale>

#include "organizer/orghtml.h"
#include "translation.h"

using namespace mu::project;
using namespace muse;

namespace {
//! A part's status: its own tag; untagged, the least finished of the sections set by hand that hold it; else none
//! (Empty). The same rule for the steps and for the "Every part" page.
StarScoreStatus todoPartStatus(const StarScoreService::Data& data, const QString& pid)
{
    auto it = data.partStatus.find(pid);
    if (it != data.partStatus.end()) {
        return StarScoreService::statusFromKey(it->second);
    }
    bool found = false;
    StarScoreStatus s = StarScoreStatus::Finished;
    for (const StarScoreSection& sec : data.sections) {
        if (!sec.autoStatus && sec.partIds.contains(pid)) {
            s = found ? std::min(s, sec.status) : sec.status;
            found = true;
        }
    }
    return found ? s : StarScoreStatus::Empty;
}

QString statusText(StarScoreStatus s)
{
    switch (s) {
    case StarScoreStatus::Empty: return muse::qtrc("starscore", "no status");
    case StarScoreStatus::Sketch: return muse::qtrc("starscore", "Sketch");
    case StarScoreStatus::InProgress: return muse::qtrc("starscore", "In progress");
    case StarScoreStatus::NeedsReview: return muse::qtrc("starscore", "Needs review");
    case StarScoreStatus::Finished: return muse::qtrc("starscore", "Finished");
    }
    return QString();
}
}

std::vector<StarScoreTodoItem> StarScoreService::todoList() const
{
    return todoList(load());
}

std::vector<StarScoreTodoItem> StarScoreService::todoList(const Data& data) const
{
    std::vector<StarScoreTodoItem> out;
    const engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return out;
    }

    auto partName = [&](const QString& pid) {
        const engraving::Part* p = ms->partById(ID(pid));
        return p ? p->partName().toQString() : pid;
    };
    auto instrumentOf = [&](const QString& pid) {
        const engraving::Part* p = ms->partById(ID(pid));
        return p ? p->instrumentId().toQString() : QString();
    };
    auto partStatus = [&](const QString& pid) { return todoPartStatus(data, pid); };

    // Adds parts to an item: its status becomes the least finished, and each unfinished part is listed
    auto addParts = [&](StarScoreTodoItem& item, const QStringList& pids) {
        for (const QString& pid : pids) {
            const StarScoreStatus s = partStatus(pid);
            item.status = item.status < 0 ? int(s) : std::min(item.status, int(s));
            if (s != StarScoreStatus::Finished) {
                item.details << QString("%1: %2").arg(partName(pid), statusText(s));
            }
        }
    };
    auto sectionParts = [&](const QString& templateKey, bool prefix) {
        QStringList pids;
        for (const StarScoreSection& sec : data.sections) {
            if (prefix ? sec.templateKey.startsWith(templateKey) : sec.templateKey == templateKey) {
                for (const QString& pid : sec.partIds) {
                    if (!pids.contains(pid)) {
                        pids << pid;
                    }
                }
            }
        }
        return pids;
    };

    // --- the rhythm section by role, and which roles read the lead sheet
    std::map<QString, QStringList> rolePids;
    std::set<QString> readsLead;
    for (const StarScoreSection& sec : data.sections) {
        if (sec.templateKey != "rhythm") {
            continue;
        }
        const QStringList& reads = sec.autoStatus ? sec.autoSkipSheets : sec.skipSheets;
        for (const QString& pid : sec.partIds) {
            const QString role = rhythmRole(instrumentOf(pid));   // the classifier the section status uses
            if ((role == "drums" || role == "keys" || role == "percussion") && reads.contains(role)
                && data.partStatus.find(pid) == data.partStatus.end()) {
                readsLead.insert(role);
                continue;
            }
            rolePids[role] << pid;
        }
    }
    const QStringList leadPids = sectionParts("lead-sheet", false);

    auto item = [](const QString& key, const QString& title) {
        StarScoreTodoItem i;
        i.key = key;
        i.title = title;
        return i;
    };
    auto finishMissing = [](StarScoreTodoItem& i, const QString& what) {
        // something the step needs isn't in the score: the step can't be finished yet
        i.details << what;
        if (i.status > int(StarScoreStatus::Empty)) {
            i.status = int(StarScoreStatus::Empty);
        }
    };
    auto sectionItem = [&](const QString& key, const QString& title, const QString& templateKey) {
        StarScoreTodoItem i = item(key, title);
        addParts(i, sectionParts(templateKey, false));
        return i;
    };
    // A rhythm role (drums, keys, percussion): its own parts, or the lead sheet when that player reads it
    auto roleItem = [&](const QString& key, const QString& title, const QString& role) {
        StarScoreTodoItem i = item(key, title);
        addParts(i, rolePids[role]);
        if (readsLead.count(role)) {
            if (leadPids.isEmpty()) {
                finishMissing(i, muse::qtrc("starscore", "reads the lead sheet, but there is none"));
            } else {
                StarScoreTodoItem lead = item(QString(), QString());
                addParts(lead, leadPids);
                i.status = i.status < 0 ? lead.status : std::min(i.status, lead.status);
                i.note = muse::qtrc("starscore", "Reads the lead sheet, so it's as far along as the lead sheet.");
            }
        }
        return i;
    };

    // 1. 3-Horn Section
    out.push_back(sectionItem("3-horn", muse::qtrc("starscore", "3-Horn Section"), "3-horn"));

    // 2. Bass + guitar
    {
        StarScoreTodoItem i = item("bass-guitar", muse::qtrc("starscore", "Bass + guitar"));
        addParts(i, rolePids["bass"]);
        addParts(i, rolePids["guitar"]);
        if (i.status >= 0) {
            if (rolePids["bass"].isEmpty()) {
                finishMissing(i, muse::qtrc("starscore", "no bass part"));
            }
            if (rolePids["guitar"].isEmpty()) {
                finishMissing(i, muse::qtrc("starscore", "no guitar part"));
            }
        }
        out.push_back(i);
    }

    // 3. Lead sheet
    {
        StarScoreTodoItem i = item("lead", muse::qtrc("starscore", "Lead sheet"));
        addParts(i, leadPids);
        out.push_back(i);
    }

    // 4. Drums
    out.push_back(roleItem("drums", muse::qtrc("starscore", "Drums"), "drums"));

    // 5–7. 2-Horn Section, 2-Horn Flexible, 3-Horn Flexible
    out.push_back(sectionItem("2-horn", muse::qtrc("starscore", "2-Horn Section"), "2-horn"));
    out.push_back(sectionItem("2-horn-any", muse::qtrc("starscore", "2-Horn Flexible"), "2-horn-any"));
    out.push_back(sectionItem("3-horn-any", muse::qtrc("starscore", "3-Horn Flexible"), "3-horn-any"));

    // 8. Keys
    out.push_back(roleItem("keys", muse::qtrc("starscore", "Keys"), "keys"));

    // 9. 4-Horn Section
    out.push_back(sectionItem("4-horn", muse::qtrc("starscore", "4-Horn Section"), "4-horn"));

    // 10. The 1-Horn sheets: trumpet, alto, tenor and trombone
    {
        StarScoreTodoItem i = sectionItem("1-horn", muse::qtrc("starscore", "1-Horn sheets"), "1-horn");
        if (i.status >= 0) {
            static const std::vector<std::pair<QString, QString> > HORNS {
                { "trumpet", "Trumpet" }, { "alto-saxophone", "Alto Sax" }, { "tenor-saxophone", "Tenor Sax" }, { "trombone", "Trombone" },
            };
            const QStringList pids = sectionParts("1-horn", false);
            for (const auto& [id, name] : HORNS) {
                const bool has = std::any_of(pids.begin(), pids.end(), [&](const QString& pid) {
                    const QString inst = instrumentOf(pid);
                    return inst.contains(id) && !(id == "trombone" && inst.contains("bass-trombone"));
                });
                if (!has) {
                    finishMissing(i, muse::qtrc("starscore", "no %1 sheet").arg(name));
                }
            }
        }
        out.push_back(i);
    }

    // 11–13. 5-, 6- and 7-Horn Sections (the 7-Horn main low horn — the bass trombone, or whatever the 7th chair is —
    // also as Bass Trombone / Bari Sax / Bass Sax / Bassoon versions)
    out.push_back(sectionItem("5-horn", muse::qtrc("starscore", "5-Horn Section"), "5-horn"));
    out.push_back(sectionItem("6-horn", muse::qtrc("starscore", "6-Horn Section"), "6-horn"));
    {
        StarScoreTodoItem i = sectionItem("7-horn", muse::qtrc("starscore", "7-Horn Section"), "7-horn");
        if (i.status >= 0) {
            QString mainName;   // the band's name for the main low horn
            for (const StarScoreSection& sec : data.sections) {
                if (sec.templateKey != "7-horn" || !mainName.isEmpty()) {
                    continue;
                }
                for (const QString& pid : sec.partIds) {
                    if (!sec.alternates.count(pid)) {
                        const QString n = lowHornName(instrumentOf(pid));
                        if (!n.isEmpty()) {
                            mainName = n;
                            break;
                        }
                    }
                }
            }
            QStringList have;   // low horns in the section, by the band's name
            for (const QString& pid : sectionParts("7-horn", false)) {
                have << lowHornName(instrumentOf(pid));
            }
            for (const QString& name : { QString("Bass Trombone"), QString("Bari Sax"), QString("Bass Sax"), QString("Bassoon") }) {
                if (name != mainName && !mainName.isEmpty() && !have.contains(name)) {
                    finishMissing(i, muse::qtrc("starscore", "no %1 version of the %2").arg(name, mainName));
                }
            }
        }
        out.push_back(i);
    }

    // 14. Percussion
    out.push_back(roleItem("percussion", muse::qtrc("starscore", "Percussion"), "percussion"));

    // 15–16. Big Band and Marching Band arrangements: every part, and the full score marked Finished
    auto familyItem = [&](const QString& key, const QString& title, const QString& sectionPrefix, const QString& templateKey) {
        StarScoreTodoItem i = item(key, title);
        addParts(i, sectionParts(sectionPrefix, true));
        if (i.status >= 0) {
            const StarScoreArrangement* arr = nullptr;
            for (const StarScoreArrangement& a : data.arrangements) {
                if (a.templateKey == templateKey) {
                    arr = &a;
                }
            }
            const StarScoreStatus scoreStatus = arr ? ownScoreStatus(data, *arr) : StarScoreStatus::Empty;
            i.status = std::min(i.status, int(scoreStatus));
            if (scoreStatus != StarScoreStatus::Finished) {
                i.details << (arr ? muse::qtrc("starscore", "Full score: %1").arg(statusText(scoreStatus))
                              : muse::qtrc("starscore", "no %1 arrangement for the full score").arg(title));
            }
        }
        return i;
    };
    out.push_back(familyItem("big-band", muse::qtrc("starscore", "Big Band arrangement"), "bigband-", "big-band"));
    out.push_back(familyItem("marching-band", muse::qtrc("starscore", "Marching Band arrangement"), "marching-", "marching-band"));
    return out;
}

// ---------------------------------------------------------------------------
//  The to-do list as a PDF page ("BALK - To-Do.pdf", next to the .starscore in Projects and Sheets)
// ---------------------------------------------------------------------------

namespace {
struct TodoLook {
    QString colour;   // fill
    QString ink;      // text on a light pill
    QString name;
};
TodoLook todoLook(int status)
{
    switch (status) {
    case 0: return { "#8A8A8A", "#4d4f57", "No status" };
    case 1: return { "#E0463A", "#a3271d", "Sketch" };
    case 2: return { "#F29B30", "#9a5a07", "In progress" };
    case 3: return { "#3C8CE7", "#1d5ba3", "Needs review" };
    case 4: return { "#3FB05A", "#1d6b33", "Finished" };
    }
    return { "transparent", "#8b90a0", "Not in the score yet" };
}
QString todoPill(int status)
{
    const TodoLook l = todoLook(status);
    if (status < 0) {
        return QString("<span class=\"pill none\">%1</span>").arg(l.name);
    }
    return QString("<span class=\"pill\" style=\"background:%1;\">%2</span>").arg(l.colour, l.name);
}
}

QString StarScoreService::todoPdfHtml(const QString& title, const QString& code, const QString& version) const
{
    using namespace mu::project::starscore::org;
    const engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return QString();
    }
    const Data data = load();
    const std::vector<StarScoreTodoItem> steps = todoList(data);

    // --- the steps
    int done = 0;
    QString nextUp;
    for (const StarScoreTodoItem& s : steps) {
        if (s.status == int(StarScoreStatus::Finished)) {
            ++done;
        } else if (nextUp.isEmpty()) {
            nextUp = s.title;
        }
    }

    // --- every part, by section
    auto partStatus = [&](const QString& pid) { return int(todoPartStatus(data, pid)); };
    auto bare = [](const QString& name) {
        const int at = name.lastIndexOf(": ");
        return at >= 0 ? name.mid(at + 2) : name;
    };
    int parts = 0, partsDone = 0;
    std::map<int, int> byStatus;
    QString sectionsHtml;
    for (const StarScoreSection& sec : data.sections) {
        QString rows;
        int secDone = 0, secParts = 0;
        for (const engraving::Part* p : ms->parts()) {
            const QString pid = idText(p);
            if (!sec.partIds.contains(pid)) {
                continue;
            }
            const int st = partStatus(pid);
            ++secParts;
            ++parts;
            ++byStatus[st];
            if (st == int(StarScoreStatus::Finished)) {
                ++secDone;
                ++partsDone;
            }
            // a stand-in version: "· Bass Trombone version" (named after the part it stands in for)
            const auto standIn = sec.alternates.find(pid);
            QString standInText;
            if (standIn != sec.alternates.end()) {
                const engraving::Part* mainPart = ms->partById(ID(standIn->second));
                const QString mainName = mainPart ? lowHornName(mainPart->instrumentId().toQString()) : QString();
                standInText = QString(" <span class=\"dim\">&middot; %1 version</span>")
                              .arg(esc(mainName.isEmpty() ? QString("Bass Trombone") : mainName));
            }
            rows += QString("<tr><td>%1%2</td><td class=\"r\">%3</td></tr>")
                    .arg(esc(bare(p->partName().toQString())), standInText, todoPill(st));
        }
        if (secParts == 0) {
            continue;
        }
        sectionsHtml += QString("<div class=\"sec\"><div class=\"sech\"><span>%1</span><span class=\"dim\">%2 of %3 finished</span></div>"
                                "<table>%4</table></div>")
                        .arg(esc(sec.name)).arg(secDone).arg(secParts).arg(rows);
    }
    // full scores with a status of their own
    QString scoresHtml;
    for (const StarScoreArrangement& a : data.arrangements) {
        if (hasOwnScoreStatus(a.templateKey)) {
            scoresHtml += QString("<tr><td>%1 full score</td><td class=\"r\">%2</td></tr>")
                          .arg(esc(a.name), todoPill(int(ownScoreStatus(data, a))));
        }
    }
    if (!scoresHtml.isEmpty()) {
        sectionsHtml += QString("<div class=\"sec\"><div class=\"sech\"><span>Full scores</span></div><table>%1</table></div>")
                        .arg(scoresHtml);
    }

    // --- the page
    QString b;
    b += QString("<div class=\"hdr\"><div><h1>%1</h1><div class=\"sub\">To-do list &middot; version %2 &middot; %3</div></div>"
                 "<div class=\"code\">%4</div></div>")
         .arg(esc(title), esc(version.isEmpty() ? QString("–") : version),
              esc(QLocale(QLocale::English).toString(QDate::currentDate(), "d MMMM yyyy")), esc(code));

    // tiles
    const int pct = steps.empty() ? 0 : int(std::round(100.0 * done / double(steps.size())));
    b += "<div class=\"grid2 tiles\">";
    b += QString("<div class=\"tile\"><div class=\"big\">%1<span class=\"of\">/%2</span></div><div class=\"lab\">steps finished</div>"
                 "<div class=\"bar\"><i class=\"s-done\" style=\"width:%3%;\"></i></div></div>")
         .arg(done).arg(steps.size()).arg(pct);
    b += QString("<div class=\"tile\"><div class=\"big\">%1<span class=\"of\">/%2</span></div><div class=\"lab\">parts finished</div>"
                 "<div class=\"bar\">").arg(partsDone).arg(parts);
    for (int st = 4; st >= 0; --st) {
        if (byStatus[st] > 0 && parts > 0) {
            b += QString("<i style=\"width:%1%;background:%2;\"></i>").arg(100.0 * byStatus[st] / parts, 0, 'f', 2).arg(todoLook(st).colour);
        }
    }
    b += "</div></div>";
    b += QString("<div class=\"tile next\"><div class=\"lab top\">%1</div><div class=\"nx\">%2</div></div>")
         .arg(nextUp.isEmpty() ? QString("All done") : QString("Next up"),
              nextUp.isEmpty() ? QString("Every step is finished") : esc(nextUp));
    b += "</div>";

    // the steps in order
    b += "<h2>In priority order</h2><table class=\"steps\">";
    int rank = 0;
    for (const StarScoreTodoItem& s : steps) {
        ++rank;
        const bool finished = s.status == int(StarScoreStatus::Finished);
        const bool next = s.title == nextUp;
        QString left;
        if (!finished) {
            QStringList items;
            for (const QString& d : s.details) {
                items << "<li>" + esc(d) + "</li>";
            }
            if (!items.isEmpty()) {
                left = "<ul>" + items.join("") + "</ul>";
            }
        }
        if (!s.note.isEmpty()) {
            left += "<div class=\"dim it\">" + esc(s.note) + "</div>";
        }
        b += QString("<tr class=\"%1\"><td class=\"n\">%2</td><td><span class=\"dot\" style=\"background:%3;%4\"></span></td>"
                     "<td class=\"t\">%5%6%7</td><td class=\"r\">%8</td></tr>")
             .arg(finished ? QString("fin") : next ? QString("nextrow") : QString())
             .arg(rank)
             .arg(s.status < 0 ? QString("transparent") : todoLook(s.status).colour,
                  s.status < 0 ? QString("border:1.5px solid #b9bdc9;") : QString())
             .arg(esc(s.title) + (finished ? " <span class=\"ck\">&#10003;</span>" : QString()),
                  next ? " <span class=\"tag\">next up</span>" : QString(), left)
             .arg(todoPill(s.status));
    }
    b += "</table>";

    // every part
    b += "<div class=\"pagebreak\"></div><h2>Every part</h2><div class=\"cols\">" + sectionsHtml + "</div>";

    // legend
    b += "<div class=\"legend\">";
    for (int st = 0; st <= 4; ++st) {
        b += QString("<span><span class=\"dot\" style=\"background:%1;\"></span> %2</span>").arg(todoLook(st).colour, todoLook(st).name);
    }
    b += "<span><span class=\"dot\" style=\"border:1.5px solid #b9bdc9;\"></span> Not in the score yet</span></div>";
    b += QString("<div class=\"foot\"><span>Made by StarScore Studio when %1 was exported</span><span>%2 - To-Do.pdf</span></div>")
         .arg(esc(title), esc(code));

    static const QString CSS = R"CSS(
.tiles{margin:14px 0 4px;gap:12px;}
.tile .big .of{font-size:15pt;color:#8b90a0;font-weight:600;margin-left:2px;}
.tile.next{background:#16213e;border-color:#16213e;color:#fff;display:flex;flex-direction:column;justify-content:center;}
.tile.next .lab{color:#b9c3dd;margin:0 0 4px;}
.tile.next .nx{font-size:15.5pt;font-weight:700;line-height:1.15;}
.pill{display:inline-block;color:#fff;border-radius:10px;padding:1.5px 9px;font-size:8.4pt;font-weight:700;white-space:nowrap;}
.pill.none{color:#8b90a0;border:1px solid #cfd3de;background:#fff;font-weight:600;}
td.r{text-align:right;white-space:nowrap;padding-right:0;}
table.steps td{padding:6px 8px 6px 0;font-size:10pt;}
table.steps td.n{width:22px;text-align:right;color:#8b90a0;font-weight:700;}
table.steps td.t{font-weight:700;color:#16213e;}
table.steps td.t ul{font-weight:400;color:#3d4356;font-size:9.1pt;margin-top:3px;}
table.steps td.t .it{font-weight:400;font-style:italic;font-size:9pt;margin-top:2px;}
table.steps tr.fin td{color:#8b90a0;}
table.steps tr.fin td.t{color:#5d7a68;font-weight:600;}
table.steps tr.nextrow td{background:#f3f6fc;}
table.steps tr.nextrow td.n{border-radius:5px 0 0 5px;}
.ck{color:#2f7d5c;font-weight:700;}
.tag{display:inline-block;background:#16213e;color:#fff;border-radius:3px;font-size:7.6pt;padding:1px 6px;margin-left:6px;
     text-transform:uppercase;letter-spacing:.6px;vertical-align:1px;}
.dot{width:11px;height:11px;border-radius:50%;}
.cols{column-count:2;column-gap:22px;}
.sec{break-inside:avoid;margin:0 0 12px;background:#f7f8fb;border:1px solid #e3e7f0;border-radius:6px;padding:8px 11px 4px;}
.sech{display:flex;justify-content:space-between;font-weight:700;color:#16213e;font-size:10.2pt;margin-bottom:2px;}
.sech .dim{font-weight:400;font-size:8.6pt;}
.sec td{font-size:9.4pt;padding:3.5px 6px 3.5px 0;border-bottom:1px solid #e9ecf3;}
.legend{margin-top:14px;}
.legend .dot{margin-right:3px;}
)CSS";
    return htmlPage(title + " - To-Do", b, CSS);
}
