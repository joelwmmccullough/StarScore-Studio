/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the open song's to-do list (the To-do panel).
 *
 * The work on a song in priority order: 3-Horn Section, bass + guitar, lead sheet, drums, 2-Horn Section,
 * 2-Horn Flexible, 3-Horn Flexible, keys, 4-Horn Section, the 1-Horn sheets, 5-, 6- and 7-Horn Sections,
 * percussion, Big Band, Marching Band. Each step is as far along as its least finished part (the part's own
 * status; untagged, the status of a section set by hand that holds it). Drums, keys and percussion that read
 * the lead sheet are as far along as the lead sheet.
 */
#include "starscoreservice.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"

#include "translation.h"

using namespace mu::project;
using namespace muse;

namespace {
//! A rhythm-section part's role: "drums", "percussion", "keys", "guitar" or "bass"
QString todoRhythmRole(const QString& id)
{
    if (id.contains("bass")) {
        return "bass";
    }
    if (id.contains("guitar")) {
        return "guitar";
    }
    if (id == "drumset" || id == "drum-kit" || id.startsWith("drum")) {
        return "drums";
    }
    if (id == "congas" || id == "bongos" || id == "percussion" || id == "timbales" || id == "cajon"
        || id.contains("shaker") || id.contains("tambourine") || id.contains("cowbell") || id.contains("conga")) {
        return "percussion";
    }
    return "keys";
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
    std::vector<StarScoreTodoItem> out;
    const engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return out;
    }
    const Data data = load();

    auto partName = [&](const QString& pid) {
        const engraving::Part* p = ms->partById(ID(pid));
        return p ? p->partName().toQString() : pid;
    };
    auto instrumentOf = [&](const QString& pid) {
        const engraving::Part* p = ms->partById(ID(pid));
        return p ? p->instrumentId().toQString() : QString();
    };
    // a part's status: its own tag; untagged, a section set by hand that holds it; else none (Empty)
    auto partStatus = [&](const QString& pid) {
        auto it = data.partStatus.find(pid);
        if (it != data.partStatus.end()) {
            return statusFromKey(it->second);
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
    };

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
            const QString role = todoRhythmRole(instrumentOf(pid));
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

    // 11–13. 5-, 6- and 7-Horn Sections (the 7-Horn Bass Trombone also as Bari Sax, Bass Sax and Bassoon)
    out.push_back(sectionItem("5-horn", muse::qtrc("starscore", "5-Horn Section"), "5-horn"));
    out.push_back(sectionItem("6-horn", muse::qtrc("starscore", "6-Horn Section"), "6-horn"));
    {
        StarScoreTodoItem i = sectionItem("7-horn", muse::qtrc("starscore", "7-Horn Section"), "7-horn");
        if (i.status >= 0) {
            QStringList have;
            for (const QString& pid : sectionParts("7-horn", false)) {
                have << instrumentOf(pid);
            }
            for (const auto& [id, name] : std::vector<std::pair<QString, QString> > {
                    { "baritone-saxophone", "Bari Sax" }, { "bass-saxophone", "Bass Sax" }, { "bassoon", "Bassoon" } }) {
                if (!have.contains(id)) {
                    finishMissing(i, muse::qtrc("starscore", "no %1 version of the Bass Trombone").arg(name));
                }
            }
        }
        out.push_back(i);
    }

    // 14. Percussion
    out.push_back(roleItem("percussion", muse::qtrc("starscore", "Percussion"), "percussion"));

    // 15–16. Big Band and Marching Band arrangements
    {
        StarScoreTodoItem i = item("big-band", muse::qtrc("starscore", "Big Band arrangement"));
        addParts(i, sectionParts("bigband-", true));
        out.push_back(i);
    }
    {
        StarScoreTodoItem i = item("marching-band", muse::qtrc("starscore", "Marching Band arrangement"));
        addParts(i, sectionParts("marching-", true));
        out.push_back(i);
    }
    return out;
}
