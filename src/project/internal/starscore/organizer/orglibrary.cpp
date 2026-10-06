/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: what is in each song folder, and how finished it is (the old build_data.py)
 *
 * Changes from build_data.py: the archive is "Version History" (it used to look for "Old Versions" and found
 * nothing); Horn Part Guides, Update Notes, Version History, Reference PDFs and the generated PDFs are known
 * folders, not "Additional material"; "1H" (StarScore's 1-horn sheets) is an arrangement; the codes come from
 * codes.json; Works In Progress songs are measured one by one rather than as one big song.
 */
#include "orglibrary.h"

#include <algorithm>
#include <set>

#include <QDateTime>
#include <QRegularExpression>

namespace mu::project::starscore::org {
const std::vector<std::pair<QString, QString> >& hornOrder()
{
    static const std::vector<std::pair<QString, QString> > HORN_ORDER {
        { "Trumpet", "Tpt" }, { "Flugelhorn", "Flg" }, { "Piccolo", "Pic" }, { "Flute", "Flu" }, { "Clarinet", "Cla" }, { "Soprano Sax", "Sop" },
        { "Alto Sax", "Alt" }, { "Tenor Sax", "Ten" }, { "Bari Sax", "Bar" }, { "Bass Sax", "Bsx" }, { "Bass Clarinet", "Bcl" },
        { "Trombone", "Tbn" }, { "Bass Trombone", "Btb" },
    };
    return HORN_ORDER;
}

QString hornAbbr(const QString& instrument)
{
    for (const auto& [name, a] : hornOrder()) {
        if (name == instrument) {
            return a;
        }
    }
    return QString();
}

QString hornFromAbbr(const QString& abbr)
{
    for (const auto& [name, a] : hornOrder()) {
        if (a == abbr) {
            return name;
        }
    }
    return abbr;
}

bool expandHornFolder(const QString& folder, int& n, QStringList& instruments, bool& generic)
{
    instruments.clear();
    generic = false;
    if (folder == "1H" || folder.startsWith("1H ")) {
        n = 1;
        generic = folder.contains("Any");
        return true;
    }
    static const QRegularExpression re("^(\\d)H (.+)$");
    const QRegularExpressionMatch m = re.match(folder);
    if (!m.hasMatch()) {
        return false;
    }
    n = m.captured(1).toInt();
    const QString rest = m.captured(2);
    if (rest.startsWith("Any") || rest.startsWith("Flexible")) {
        generic = true;
        return true;
    }
    static const QRegularExpression tok("^(\\d)?([A-Za-z]{3})$");
    for (const QString& t : QString(rest).replace(" (orig)", "").split(' ', Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch tm = tok.match(t);
        if (!tm.hasMatch()) {
            continue;
        }
        const int count = tm.captured(1).isEmpty() ? 1 : tm.captured(1).toInt();
        const QString base = hornFromAbbr(tm.captured(2));
        if (count == 1) {
            instruments << base;
        } else {
            for (int i = 1; i <= count; ++i) {
                instruments << QString("%1 %2").arg(base).arg(i);
            }
        }
    }
    return true;
}

QString hornFolderName(const QStringList& instruments)
{
    std::map<QString, int> count;
    static const QRegularExpression num("\\s+[12]$");
    for (const QString& i : instruments) {
        count[QString(i).remove(num)]++;
    }
    QStringList parts;
    int total = 0;
    for (const auto& [name, abbr] : hornOrder()) {
        auto it = count.find(name);
        if (it != count.end()) {
            parts << (it->second > 1 ? QString::number(it->second) : QString()) + abbr;
            total += it->second;
        }
    }
    return QString("%1H %2").arg(total).arg(parts.join(' '));
}

static bool expandStrings(const QString& folder, int& n, QStringList& out)
{
    static const QRegularExpression re("^(\\d)S (.+)$");
    const QRegularExpressionMatch m = re.match(folder);
    if (!m.hasMatch()) {
        return false;
    }
    static const QMap<QString, QString> ABBR { { "Vln", "Violin" }, { "Vla", "Viola" }, { "Vc", "Cello" }, { "Cb", "Double Bass" } };
    n = m.captured(1).toInt();
    static const QRegularExpression tok("^(\\d)?([A-Za-z]{2,3})$");
    for (const QString& t : m.captured(2).split(' ', Qt::SkipEmptyParts)) {
        const QRegularExpressionMatch tm = tok.match(t);
        if (!tm.hasMatch()) {
            continue;
        }
        const int count = tm.captured(1).isEmpty() ? 1 : tm.captured(1).toInt();
        const QString base = ABBR.value(tm.captured(2), tm.captured(2));
        for (int i = 1; i <= count; ++i) {
            out << (count == 1 ? base : QString("%1 %2").arg(base).arg(i));
        }
    }
    return true;
}

static const QMap<QString, QString> SPECIAL_ENSEMBLE {
    { "Big Band", "Full big-band chart (5 saxes, 4 trumpets, 4 trombones, rhythm)." },
    { "Full Orchestra", "Orchestral score and parts." },
    { "Marching Band", "Marching band score and parts." },
};

static const QStringList RHYTHM_ORDER { "Drums", "Percussion", "Congas", "Bass", "Bass Synth", "Guitar", "Keys", "Elec Piano",
                                        "Organ", "Clavinet" };

static QString rhythmBase(const QString& r)
{
    static const QRegularExpression paren(" \\(.*\\)$");
    return QString(r).remove(paren);
}

static QString partNameOf(const QString& file)
{
    static const QRegularExpression re("^\\w+ - (.+)\\.pdf$");
    const QRegularExpressionMatch m = re.match(file);
    return m.hasMatch() ? m.captured(1) : QString();
}

int SongInfo::currentSheets() const
{
    int n = int(lead.size() + rhythm.size() + extras.size() + demos.size());
    for (const ArrangementInfo& a : arrangements) {
        n += int(a.files.size());
    }
    for (const EnsembleInfo& e : ensembles) {
        n += int(e.files.size());
    }
    return n;
}

const SongInfo* Library::song(const QString& code) const
{
    auto it = byCode.find(code);
    return it == byCode.end() ? nullptr : &songs[it->second];
}

static std::map<QString, Quality> measureQuality(const ScanResult& scan, const QString& root)
{
    // Blank: fewer than 120 music symbols. Short: a lead sheet or rhythm part with fewer pages than the song's
    // reference page count and under 75% of the symbols of its fullest rhythm sheet.
    std::map<QString, Quality> q;
    std::vector<std::pair<QString, const SheetEntry*> > all;
    const QString prefix = root + "/";
    for (auto it = scan.sheets.lower_bound(prefix); it != scan.sheets.end() && it->first.startsWith(prefix); ++it) {
        if (songRootOf(it->first) == root && !it->first.contains("/Reference PDFs/") && it->second.measured) {
            all.emplace_back(it->first, &it->second);
        }
    }
    std::map<int, int> pageCounts;
    int leadPages = 0;
    for (const auto& [rel, e] : all) {
        if (!rel.contains("Score")) {
            pageCounts[e->pages]++;
        }
        if (rel.contains("Lead Sheet")) {
            leadPages = std::max(leadPages, e->pages);
        }
    }
    if (pageCounts.empty()) {
        return q;
    }
    int mode = 0, best = -1;
    for (const auto& [pages, c] : pageCounts) {   // most common; ties go to the first seen (smallest), like Counter
        if (c > best) {
            best = c;
            mode = pages;
        }
    }
    const int ref = std::max(mode, leadPages);
    int maxGlyphs = 0;
    auto isRhythmish = [](const QString& rel) { return rel.contains("/1 Rhythm/") || rel.contains("Lead Sheet"); };
    for (const auto& [rel, e] : all) {
        if (isRhythmish(rel)) {
            maxGlyphs = std::max(maxGlyphs, e->glyphs);
        }
    }
    for (const auto& [rel, e] : all) {
        if (e->glyphs < 120) {
            q[rel] = Quality::Empty;
        } else if (isRhythmish(rel) && e->pages < ref && e->glyphs < 0.75 * maxGlyphs) {
            q[rel] = Quality::Thin;
        } else {
            q[rel] = Quality::Ok;
        }
    }
    return q;
}

static QString statusOfHorns(const std::vector<ArrangementInfo>& arrs, int n)
{
    bool any = false;
    for (const ArrangementInfo& a : arrs) {
        if (a.n != n) {
            continue;
        }
        any = true;
        if (a.usable && !a.scoreOnly && a.emptyParts.isEmpty()) {
            return "done";
        }
    }
    return any ? "partial" : "none";
}

Library buildLibrary(const ScanResult& scan, const std::map<QString, QString>& codes)
{
    Library lib;
    struct Raw {
        QString name, cat, root;
        std::map<QString, QStringList> folders;   // first folder -> files below it (relative)
        QStringList topFiles;
        int archive = 0;
        QString oldestLead, oldestFile;
    };
    std::map<QString, Raw> raw;
    static const QMap<QChar, QString> CAT { { '1', "orig" }, { '2', "cover" }, { '3', "vocal" } };
    static const QRegularExpression leadName("lead[_ ]?sheet", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression songGroup("^[123] ");

    for (const FileEntry& f : scan.tree) {
        lib.totalFiles++;
        const QStringList seg = f.rel.split('/');
        if (seg[0].startsWith("5 Archive")) {
            lib.archivedFiles++;
            continue;
        }
        if (seg[0] == "6 Inbox") {
            lib.inboxFiles++;
            continue;
        }
        QString name, cat, root;
        QStringList rest;
        if (seg[0] == "4 Works In Progress" && seg.size() >= 3) {
            cat = "wip";
            root = seg[0] + "/" + seg[1];
            rest = seg.mid(2);
        } else if (seg.size() >= 2 && songGroup.match(seg[0]).hasMatch()) {
            cat = CAT.value(seg[0].at(0));
            root = seg[0];
            rest = seg.mid(1);
        } else {
            continue;
        }
        name = songTitleOf(root);
        Raw& r = raw[root];
        r.name = name;
        r.cat = cat;
        r.root = root;
        const QString fileDate = QDateTime::fromMSecsSinceEpoch(f.mtime).date().toString(Qt::ISODate);
        if (r.oldestFile.isEmpty() || fileDate < r.oldestFile) {
            r.oldestFile = fileDate;
        }
        if (f.rel.endsWith(".pdf", Qt::CaseInsensitive) && leadName.match(seg.last()).hasMatch()) {
            const QString& d = fileDate;
            if (r.oldestLead.isEmpty() || d < r.oldestLead) {
                r.oldestLead = d;
            }
        }
        if (rest.size() == 1) {
            r.topFiles << rest[0];
            continue;
        }
        if (rest[0] == "Version History" || rest[0] == "Old Versions") {
            r.archive++;
            lib.archivedFiles++;
            continue;
        }
        r.folders[rest[0]] << rest.mid(1).join('/');
    }
    // empty song folders still exist as songs when they have a code
    for (const auto& [root, code] : codes) {
        if (!raw.count(root)) {
            Raw& r = raw[root];
            r.root = root;
            r.name = songTitleOf(root);
            r.cat = root.startsWith("4 Works In Progress/") ? QString("wip") : CAT.value(root.at(0), "orig");
        }
    }

    for (auto& [root, r] : raw) {
        SongInfo s;
        s.name = r.name;
        s.cat = r.cat;
        s.root = root;
        s.archive = r.archive;
        s.oldestLeadDate = r.oldestLead;
        s.oldestFileDate = r.oldestFile;
        auto code = codes.find(root);
        if (code != codes.end()) {
            s.code = code->second;
        } else {
            // a folder that isn't registered yet: take the code its files use
            static const QRegularExpression pre("^([A-Z]{3,4}) - ");
            for (const auto& [folder, files] : r.folders) {
                for (const QString& fn : files) {
                    const QRegularExpressionMatch m = pre.match(fn.section('/', -1));
                    if (m.hasMatch()) {
                        s.code = m.captured(1);
                        break;
                    }
                }
                if (!s.code.isEmpty()) {
                    break;
                }
            }
            if (s.code.isEmpty()) {
                s.code = "????";
            }
        }
        for (const QString& t : r.topFiles) {
            if (t.endsWith(" - Recordings.pdf")) {
                s.hasRecordingsPdf = true;
            }
        }

        const std::map<QString, Quality> quality = measureQuality(scan, root);
        auto qualityOf = [&](const QString& folder, const QString& file) {
            auto it = quality.find(root + "/" + folder + "/" + file);
            return it == quality.end() ? Quality::Ok : it->second;
        };

        for (auto& [folder, files] : r.folders) {
            files.sort();
            int n = 0;
            QStringList instruments;
            bool generic = false;
            int sn = 0;
            QStringList strings;
            if (folder == "1 Lead Sheet") {
                s.lead = files;
                for (const QString& f : files) {
                    s.leadq[f] = qualityOf(folder, f);
                }
            } else if (folder == "1 Rhythm") {
                for (const QString& f : files) {
                    const QString part = partNameOf(f);
                    if (!part.isEmpty()) {
                        s.rhythm << part;
                        s.rhythmq[part] = qualityOf(folder, f);
                    }
                }
            } else if (folder == "Extras") {
                s.extras = files;
            } else if (folder == "Demos") {
                s.demos = files;
            } else if (folder == "Reference PDFs") {
                s.references = files;
            } else if (folder == "Horn Part Guides") {
                s.hornGuides = files;
            } else if (folder.startsWith("Update Notes")) {
                // known folder, nothing to list: SongInfo::updateNotes is set by the organizer after the folder
                // is re-dated (the scan this library comes from predates that, and a fresh folder has no files yet)
            } else if (expandHornFolder(folder, n, instruments, generic)) {
                ArrangementInfo a;
                a.folder = folder;
                a.n = n;
                a.instruments = instruments;
                a.generic = generic;
                a.solo = n == 1;
                a.files = files;
                a.scoreOnly = std::all_of(files.begin(), files.end(), [](const QString& f) { return f.endsWith("- Score.pdf") || f.contains(" Score (") || f.endsWith(" Score.pdf"); });
                for (const QString& f : files) {
                    a.quality[f] = qualityOf(folder, f);
                    if (f.contains("Score")) {
                        continue;
                    }
                    if (a.quality[f] == Quality::Empty) {
                        a.emptyParts << f;
                    } else {
                        a.liveParts << f;
                        if (a.quality[f] == Quality::Thin) {
                            a.thinParts << f;
                        }
                    }
                }
                a.usable = !a.liveParts.isEmpty();
                s.arrangements.push_back(a);
            } else if (SPECIAL_ENSEMBLE.contains(folder) || expandStrings(folder, sn, strings)) {
                EnsembleInfo e;
                e.folder = folder;
                e.files = files;
                e.note = SPECIAL_ENSEMBLE.contains(folder) ? SPECIAL_ENSEMBLE.value(folder)
                         : QString("%1 strings: %2").arg(sn).arg(strings.join(", "));
                s.ensembles.push_back(e);
            } else {
                s.other[folder] = files;
            }
        }
        std::sort(s.arrangements.begin(), s.arrangements.end(), [](const ArrangementInfo& a, const ArrangementInfo& b) {
            return a.n != b.n ? a.n < b.n : a.folder < b.folder;
        });
        std::stable_sort(s.rhythm.begin(), s.rhythm.end(), [](const QString& a, const QString& b) {
            const int ia = RHYTHM_ORDER.indexOf(rhythmBase(a)), ib = RHYTHM_ORDER.indexOf(rhythmBase(b));
            const int ka = ia < 0 ? 99 : ia, kb = ib < 0 ? 99 : ib;
            return ka != kb ? ka < kb : a < b;
        });

        // --- status
        const bool leadOk = std::any_of(s.leadq.begin(), s.leadq.end(), [](const auto& p) { return p.second != Quality::Empty; });
        s.status.lead = leadOk ? "done" : (s.lead.isEmpty() ? "none" : "partial");
        static const std::vector<std::pair<QString, QStringList> > ROLES {
            { "drums", { "Drums", "Percussion", "Congas" } }, { "bass", { "Bass", "Bass Synth" } },
            { "keys", { "Keys", "Elec Piano", "Organ", "Piano", "Clavinet" } }, { "guitar", { "Guitar" } },
        };
        std::set<QString> live, stub;
        for (const QString& part : s.rhythm) {
            (s.rhythmq[part] == Quality::Empty ? stub : live).insert(rhythmBase(part));
        }
        int done = 0, started = 0;
        for (const auto& [role, names] : ROLES) {
            QString v = "none";
            for (const QString& nm : names) {
                if (live.count(nm)) {
                    v = "done";
                    break;
                }
                if (stub.count(nm)) {
                    v = "empty";
                }
            }
            s.status.roles[role] = v;
            done += v == "done";
            started += v != "none";
        }
        s.status.rhythm = done == int(ROLES.size()) ? "done" : (started ? "partial" : "none");
        for (int n = 1; n <= 7; ++n) {
            s.status.horns[n] = statusOfHorns(s.arrangements, n);
        }
        s.status.three = s.status.horns[3];

        // --- sheets that exist but aren't written
        for (const QString& f : s.lead) {
            if (s.leadq[f] != Quality::Ok) {
                s.issues.push_back({ "1 Lead Sheet", f, s.leadq[f] });
            }
        }
        for (const auto& [part, q] : s.rhythmq) {
            if (q != Quality::Ok) {
                s.issues.push_back({ "1 Rhythm", s.code + " - " + part + ".pdf", q });
            }
        }
        for (const ArrangementInfo& a : s.arrangements) {
            for (const QString& f : a.emptyParts) {
                s.issues.push_back({ a.folder, f, Quality::Empty });
            }
            for (const QString& f : a.thinParts) {
                s.issues.push_back({ a.folder, f, Quality::Thin });
            }
        }
        lib.songs.push_back(s);
    }
    std::sort(lib.songs.begin(), lib.songs.end(), [](const SongInfo& a, const SongInfo& b) { return a.name < b.name; });
    for (int i = 0; i < int(lib.songs.size()); ++i) {
        lib.byCode[lib.songs[i].code] = i;
        lib.byRoot[lib.songs[i].root] = i;
    }
    return lib;
}
}
