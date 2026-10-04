/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the sheet record kept for the folder colours.
 *
 * Every Export to Sheets and Demos writes 6 Inbox/.organizer/sheets/CODE.json:
 *   "sheets"  — each sheet exported so far: its status when it was exported, its size and md5 (so a file replaced
 *               by hand afterwards no longer counts), the date and the score version;
 *   "tiers"   — for the song folder's colour, the sheets each colour needs (red … purple), and what's missing from
 *               the score for that colour (e.g. "no 2-Horn Standard arrangement");
 *   "folders" — for each sheet folder, the files that must be there.
 * The organizer reads these files and colours the folders (organizer/orgcolours.cpp). Sheets that weren't ticked in
 * an export keep the status they were exported with.
 */
#include "starscoreservice.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include <QCryptographicHash>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"

using namespace mu::project;
using namespace muse;

namespace mu::project::starscore {
//! For the export (starscorebandexport.cpp): a PDF it wrote, with its bytes, and the end of its use of them. Declared
//! there as well; the service header is shared, so it isn't declared in it.
void noteExportedPdf(const QString& absolutePath, const QByteArray& pdf);
void forgetExportedPdfs();
}

namespace {
//! The PDFs the running export wrote, by absolute path: their size and md5, from the bytes it wrote. The record used
//! to read every exported PDF back from Drive for its md5 (74 reads of files Drive may not have finished syncing).
struct ExportedPdf {
    qint64 size = 0;
    QString md5;
};
std::map<QString, ExportedPdf>& exportedPdfs()
{
    static std::map<QString, ExportedPdf> s_pdfs;
    return s_pdfs;
}

//! A file's size and md5: from the bytes the export wrote when it wrote this file, else from the file on disk
//! (a sheet left as it was because it came out the same). Empty md5 when the file can't be read.
ExportedPdf sizeAndMd5Of(const QString& path)
{
    auto it = exportedPdfs().find(path);
    if (it != exportedPdfs().end()) {
        return it->second;
    }
    ExportedPdf info;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return info;
    }
    info.size = f.size();
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(&f);
    info.md5 = QString::fromLatin1(h.result().toHex());
    return info;
}

//! A rhythm-section part's role: "drums", "percussion", "keys", "guitar" or "bass"
QString rhythmRole(const QString& id)
{
    if (id.contains("bass")) {          // electric-bass, bass-guitar, bass-synth, contrabass …
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

QJsonArray toArray(const QStringList& l)
{
    QJsonArray a;
    for (const QString& s : l) {
        a.append(s);
    }
    return a;
}

void addUnique(QStringList& to, const QStringList& from)
{
    for (const QString& s : from) {
        if (!to.contains(s)) {
            to << s;
        }
    }
}
}

void mu::project::starscore::noteExportedPdf(const QString& absolutePath, const QByteArray& pdf)
{
    exportedPdfs()[absolutePath] = ExportedPdf {
        qint64(pdf.size()), QString::fromLatin1(QCryptographicHash::hash(pdf, QCryptographicHash::Md5).toHex())
    };
}

void mu::project::starscore::forgetExportedPdfs()
{
    exportedPdfs().clear();
}

void StarScoreService::writeSheetRecord(const engraving::MasterScore* ms, const Data& data, const StarScoreBandExportPlan& full,
                                        const QStringList& onDisk) const
{
    if (!ms || full.code.isEmpty() || full.bandFolder.isEmpty()) {
        return;
    }
    const QString songDir = full.bandFolder + "/" + full.songFolder;
    const QString recordPath = full.bandFolder + "/6 Inbox/.organizer/sheets/" + full.code + ".json";

    QJsonObject record;
    {
        QFile f(recordPath);
        if (f.open(QIODevice::ReadOnly)) {
            record = QJsonDocument::fromJson(f.readAll()).object();
        }
    }

    // --- a part's status: its own tag; untagged, the status of a section set by hand that holds it; else none
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
    // a sheet's status: the least finished of the parts on it
    auto sheetStatus = [&](const StarScoreBandFile& f) {
        StarScoreStatus s = StarScoreStatus::Finished;
        for (const QString& pid : f.partIds) {
            s = std::min(s, partStatus(pid));
        }
        // the Big Band / Marching Band / Orchestra full score: also its own status
        if (f.isScore) {
            const QString folder = f.relativePath.section('/', 0, -2);
            const QString tpl = folder == "Big Band" ? "big-band" : folder == "Marching Band" ? "marching-band"
                                : folder == "Full Orchestra" ? "orchestra" : QString();
            if (!tpl.isEmpty()) {
                bool found = false;
                for (const StarScoreArrangement& a : data.arrangements) {
                    if (a.templateKey == tpl) {
                        s = std::min(s, ownScoreStatus(data, a));
                        found = true;
                    }
                }
                if (!found) {
                    s = StarScoreStatus::Empty;
                }
            }
        }
        return f.partIds.isEmpty() ? StarScoreStatus::Empty : s;
    };

    // --- the sheets on disk after this export (written now, or the same as before and left alone)
    QJsonObject sheets = record.value("sheets").toObject();
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    for (const StarScoreBandFile& f : full.files) {
        if (!f.sourceFile.isEmpty() || !onDisk.contains(f.relativePath)) {
            continue;
        }
        const ExportedPdf pdf = sizeAndMd5Of(songDir + "/" + f.relativePath);
        if (pdf.md5.isEmpty()) {
            continue;   // not there (or unreadable)
        }
        sheets[f.relativePath] = QJsonObject {
            { "status", statusKey(sheetStatus(f)) }, { "size", double(pdf.size) }, { "md5", pdf.md5 },
            { "exported", today }, { "version", data.version },
        };
    }
    starscore::forgetExportedPdfs();

    // --- which plan files belong to which part
    std::map<QString, QStringList> filesOfPart;      // part id -> its sheets (not scores)
    std::map<QString, QString> scoreOfFolder;        // folder -> its Score
    std::map<QString, QStringList> filesOfFolder;    // folder -> its sheets (not scores)
    for (const StarScoreBandFile& f : full.files) {
        if (!f.sourceFile.isEmpty()) {
            continue;
        }
        const QString folder = f.relativePath.section('/', 0, -2);
        if (f.isScore) {
            scoreOfFolder[folder] = f.relativePath;
            continue;
        }
        filesOfFolder[folder] << f.relativePath;
        for (const QString& pid : f.partIds) {
            filesOfPart[pid] << f.relativePath;
        }
    }
    auto filesOf = [&](const QStringList& pids, const StarScoreSection* skipAlternatesOf) {
        QStringList out;
        for (const QString& pid : pids) {
            if (skipAlternatesOf && skipAlternatesOf->alternates.count(pid)) {
                continue;
            }
            auto it = filesOfPart.find(pid);
            if (it != filesOfPart.end()) {
                addUnique(out, it->second);
            }
        }
        return out;
    };
    auto sectionById = [&](const QString& id) -> const StarScoreSection* {
        for (const StarScoreSection& s : data.sections) {
            if (s.id == id) {
                return &s;
            }
        }
        return nullptr;
    };
    // the section's folder: the shallowest one its sheets are in (7-Horn bass horns sit in a subfolder)
    auto folderOf = [&](const QStringList& files) {
        QString best;
        for (const QString& f : files) {
            const QString folder = f.section('/', 0, -2);
            if (best.isEmpty() || folder.count('/') < best.count('/')) {
                best = folder;
            }
        }
        return best;
    };

    // --- lead sheet, the 3-Horn Section, the rhythm roles
    QStringList lead, section3;
    bool has3 = false;
    std::map<QString, QStringList> roleFiles;        // "drums", "keys", "guitar", "bass"
    std::set<QString> roleParts, roleReadsLead;      // roles with a part; roles (drums, keys) read from the lead sheet
    for (const StarScoreSection& sec : data.sections) {
        if (sec.templateKey == "lead-sheet") {
            addUnique(lead, filesOf(sec.partIds, nullptr));
        } else if (sec.templateKey == "3-horn") {
            has3 = true;
            addUnique(section3, filesOf(sec.partIds, &sec));
        } else if (sec.templateKey == "rhythm") {
            const QStringList& reads = sec.autoStatus ? sec.autoSkipSheets : sec.skipSheets;
            for (const QString& pid : sec.partIds) {
                const engraving::Part* p = ms->partById(ID(pid));
                if (!p) {
                    continue;
                }
                const QString role = rhythmRole(p->instrumentId().toQString());
                if (role == "percussion") {
                    continue;   // left out of every colour rule
                }
                roleParts.insert(role);
                if ((role == "drums" || role == "keys") && reads.contains(role)) {
                    roleReadsLead.insert(role);
                    continue;
                }
                addUnique(roleFiles[role], filesOf({ pid }, nullptr));
            }
        }
    }

    struct Tier {
        QStringList paths;
        QStringList missing;
    };
    auto needRole = [&](Tier& t, const QString& role, const QString& label) {
        QStringList files = roleFiles[role];
        if (roleReadsLead.count(role)) {
            addUnique(files, lead);   // that player reads the lead sheet
        }
        if (files.isEmpty()) {
            t.missing << (roleParts.count(role) ? QString("no %1 sheet").arg(label) : QString("no %1 part").arg(label));
        }
        addUnique(t.paths, files);
    };
    auto needLead = [&](Tier& t) {
        if (lead.isEmpty()) {
            t.missing << "no lead sheet";
        }
        addUnique(t.paths, lead);
    };
    auto need3 = [&](Tier& t) {
        if (!has3 || section3.isEmpty()) {
            t.missing << "no 3-Horn Section";
        }
        addUnique(t.paths, section3);
    };

    // An arrangement's horn sheets (every section but the lead sheet and the rhythm section), its score, its folder
    auto arrangementOf = [&](const QString& tpl) -> const StarScoreArrangement* {
        for (const StarScoreArrangement& a : data.arrangements) {
            if (a.templateKey == tpl) {
                return &a;
            }
        }
        return nullptr;
    };
    auto hornSheets = [&](const StarScoreArrangement& a, bool withAlternates, QStringList* scores) {
        QStringList out;
        for (const QString& sid : a.sectionIds) {
            const StarScoreSection* s = sectionById(sid);
            if (!s || s->templateKey == "lead-sheet" || s->templateKey == "rhythm") {
                continue;
            }
            const QStringList files = filesOf(s->partIds, withAlternates ? nullptr : s);
            addUnique(out, files);
            if (scores) {
                // the section's own folder score, or the family folder's (Big Band, Marching Band)
                const QString folder = folderOf(files);
                auto sc = scoreOfFolder.find(folder);
                if (sc != scoreOfFolder.end() && !scores->contains(sc->second)) {
                    *scores << sc->second;
                }
            }
        }
        return out;
    };
    static const std::vector<std::pair<QString, QString> > NAMES {
        { "1-horn-standard", "1-Horn Standard" }, { "2-horn-any", "2-Horn Flexible" }, { "2-horn-standard", "2-Horn Standard" },
        { "3-horn-any", "3-Horn Flexible" }, { "3-horn-standard", "3-Horn Standard" }, { "4-horn-standard", "4-Horn Standard" },
        { "5-horn-standard", "5-Horn Standard" }, { "6-horn-standard", "6-Horn Standard" }, { "7-horn-standard", "7-Horn Standard" },
        { "big-band", "Big Band" }, { "marching-band", "Marching Band" },
    };
    auto nameOf = [&](const QString& tpl) {
        for (const auto& [k, n] : NAMES) {
            if (k == tpl) {
                return n;
            }
        }
        return tpl;
    };
    // 1-Horn: the four horns there are songbooks for
    static const QStringList ONE_HORN { "Trumpet", "Alto Sax", "Tenor Sax", "Trombone" };

    // --- the colour tiers. Each colour also needs everything the colours below it need (the organizer checks that).
    Tier red, orange, yellow, green, blue, purple;
    needLead(red);
    need3(red);
    needRole(red, "drums", "drums");
    needRole(red, "guitar", "guitar");
    needRole(red, "bass", "bass");
    needRole(red, "keys", "keys");

    needLead(orange);
    need3(orange);
    needRole(orange, "guitar", "guitar");
    needRole(orange, "bass", "bass");

    needRole(yellow, "drums", "drums");
    needRole(yellow, "keys", "keys");

    for (const QString& tpl : { QString("1-horn-standard"), QString("2-horn-any"), QString("2-horn-standard"), QString("3-horn-any"),
                                QString("3-horn-standard"), QString("4-horn-standard") }) {
        const StarScoreArrangement* a = arrangementOf(tpl);
        if (!a) {
            green.missing << QString("no %1 arrangement").arg(nameOf(tpl));
            continue;
        }
        const QStringList files = hornSheets(*a, true, nullptr);
        if (files.isEmpty()) {
            green.missing << QString("no horn sheets in %1").arg(nameOf(tpl));
        }
        if (tpl == "1-horn-standard") {
            for (const QString& horn : ONE_HORN) {
                const QString want = QString("1H/%1 - %2.pdf").arg(full.code, horn);
                if (!files.contains(want)) {
                    green.missing << QString("no %1 sheet in 1-Horn Standard").arg(horn);
                }
            }
        }
        addUnique(green.paths, files);
    }

    // Charts: the arrangement's score and every sheet in it (stand-in versions and percussion left out)
    auto needChart = [&](Tier& t, const QString& tpl, bool withLeadAndRhythm) {
        const StarScoreArrangement* a = arrangementOf(tpl);
        if (!a) {
            t.missing << QString("no %1 arrangement").arg(nameOf(tpl));
            return;
        }
        QStringList scores;
        const QStringList horns = hornSheets(*a, false, &scores);
        if (horns.isEmpty()) {
            t.missing << QString("no sheets in %1").arg(nameOf(tpl));
        }
        addUnique(t.paths, horns);
        addUnique(t.paths, scores);
        if (withLeadAndRhythm) {
            needLead(t);
            needRole(t, "drums", "drums");
            needRole(t, "guitar", "guitar");
            needRole(t, "bass", "bass");
            needRole(t, "keys", "keys");
        }
    };
    for (const QString& tpl : { QString("4-horn-standard"), QString("5-horn-standard"), QString("6-horn-standard"),
                                QString("7-horn-standard") }) {
        needChart(blue, tpl, true);
    }
    // Big Band and Marching Band: their folder holds the whole band, rhythm included
    needChart(purple, "big-band", false);
    needChart(purple, "marching-band", false);

    auto tierJson = [](const Tier& t) {
        QStringList missing = t.missing;
        missing.removeDuplicates();
        return QJsonObject { { "paths", toArray(t.paths) }, { "missing", toArray(missing) } };
    };
    const QJsonObject tiers {
        { "red", tierJson(red) }, { "orange", tierJson(orange) }, { "yellow", tierJson(yellow) },
        { "green", tierJson(green) }, { "blue", tierJson(blue) }, { "purple", tierJson(purple) },
    };

    // --- each sheet folder: the files that must be in it
    QJsonObject folders;
    auto setFolder = [&](const QString& folder, const QStringList& required, const QStringList& missing) {
        if (folder.isEmpty()) {
            return;
        }
        QJsonObject o = folders.value(folder).toObject();
        QJsonArray req = o.value("required").toArray();
        for (const QString& r : required) {
            if (!req.contains(r)) {
                req.append(r);
            }
        }
        QJsonArray mis = o.value("missing").toArray();
        for (const QString& m : missing) {
            mis.append(m);
        }
        o["required"] = req;
        o["missing"] = mis;
        folders[folder] = o;
    };

    if (!lead.isEmpty()) {
        setFolder("1 Lead Sheet", lead, {});
    }
    if (!roleParts.empty()) {
        // with a lead sheet, only guitar and bass must have their own sheets; without one, drums and keys too
        QStringList req, mis;
        QStringList roles { "guitar", "bass" };
        if (lead.isEmpty()) {
            roles << "drums" << "keys";
        }
        for (const QString& role : roles) {
            if (roleFiles[role].isEmpty()) {
                mis << QString("no %1 sheet").arg(role);
            }
            addUnique(req, roleFiles[role]);
        }
        setFolder("1 Rhythm", req, mis);
    }
    for (const StarScoreSection& sec : data.sections) {
        const QString key = sec.templateKey;
        if (key == "lead-sheet" || key == "rhythm") {
            continue;
        }
        const QStringList files = filesOf(sec.partIds, nullptr);
        const QString folder = folderOf(files);
        if (folder.isEmpty()) {
            continue;
        }
        QStringList req = filesOfFolder[folder];   // every sheet the export makes for that folder (not the score)
        if (key == "1-horn") {
            for (const QString& horn : ONE_HORN) {
                addUnique(req, { QString("1H/%1 - %2.pdf").arg(full.code, horn) });
            }
        } else if (key == "7-horn") {
            // the Bass Trombone line in every version StarScore makes, in the bass horns subfolder
            const QString bass = folder + "/Bass Horns (Horn #7)";
            QStringList bassReq = filesOfFolder[bass];
            for (const QString& horn : { QString("Bass Trombone"), QString("Bari Sax"), QString("Bass Sax"), QString("Bassoon"),
                                         QString("Bass Clarinet"), QString("Contrabass Clarinet"), QString("Contrabassoon"),
                                         QString("Tuba") }) {
                addUnique(bassReq, { QString("%1/%2 - %3.pdf").arg(bass, full.code, horn) });
            }
            addUnique(req, bassReq);
            setFolder(bass, bassReq, {});
            // The subfolder's own colours, each needing its sheets exported as Finished: Red with none of these,
            // Orange with the Bass Trombone, Yellow with Bass Trombone, Bari Sax and Bass Sax, Green with all eight
            auto sheet = [&](const QString& horn) { return QString("%1/%2 - %3.pdf").arg(bass, full.code, horn); };
            const QStringList orangeNeeds { sheet("Bass Trombone") };
            const QStringList yellowNeeds { sheet("Bass Trombone"), sheet("Bari Sax"), sheet("Bass Sax") };
            QJsonObject o = folders.value(bass).toObject();
            o["base"] = "Red";
            o["ladder"] = QJsonArray {
                QJsonObject { { "colour", "Orange" }, { "paths", toArray(orangeNeeds) } },
                QJsonObject { { "colour", "Yellow" }, { "paths", toArray(yellowNeeds) } },
                QJsonObject { { "colour", "Green" }, { "paths", toArray(bassReq) } },
            };
            folders[bass] = o;
        }
        setFolder(folder, req, {});
    }

    record["version"] = 1;
    record["about"] = "StarScore Studio: each exported sheet's status when exported (with its size and md5), the sheets each "
                      "folder colour needs, and the files each sheet folder must have";
    record["code"] = full.code;
    record["songRoot"] = full.songFolder;
    record["title"] = full.title;
    record["scoreVersion"] = data.version;
    record["updated"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    record["sheets"] = sheets;
    record["tiers"] = tiers;
    record["folders"] = folders;

    QDir().mkpath(QFileInfo(recordPath).absolutePath());
    QSaveFile out(recordPath);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(record).toJson(QJsonDocument::Indented));
        out.commit();
    }
}
