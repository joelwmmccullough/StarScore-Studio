/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: filing (the old organize.py and maintain.py)
 */
#include "orgfiling.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "orghtml.h"
#include "orglibrary.h"
#include "orgonline.h"
#include "orgplatform.h"

namespace mu::project::starscore::org {
// ------------------------------------------------------------------ instruments
// the horns and their abbreviations are hornOrder() (orglibrary.h), shared with the folder names and the Band Guide
static const QStringList RHYTHM { "Bass", "Guitar", "Keys", "Elec Piano", "Organ", "Bass Synth", "Drums", "Percussion", "Congas", "Clavinet" };
static const QStringList STRINGS { "Violin", "Viola", "Cello", "Strings", "Bassoon", "Accordion" };

//! "CODE - " at the start of a file name, as every filed sheet has it
static const QRegularExpression CODE_PREFIX("^([A-Z]{4}) - ");

static const std::vector<std::pair<QString, QString> > CANON {
    { "^bass tromb", "Bass Trombone" }, { "^(bass sax|bass saxophone)", "Bass Sax" }, { "^tromb", "Trombone" },
    { "^bass clarinet", "Bass Clarinet" }, { "^clarinet", "Clarinet" }, { "^(soprano sax|soprano saxophone|sop sax)", "Soprano Sax" },
    { "^(alto sax|alto saxophone)", "Alto Sax" }, { "^(tenor sax|tenor saxophone)", "Tenor Sax" },
    { "^(bari sax|baritone sax|baritone saxophone)", "Bari Sax" }, { "^flugel", "Flugelhorn" }, { "^(trumpet|tpt)", "Trumpet" },
    { "^piccolo", "Piccolo" }, { "^flute", "Flute" }, { "^sop.*recorder", "Recorder" }, { "^bass synth", "Bass Synth" },
    { "^(bass guitar|4-string bass|6-string bass|electric bass)", "Bass" }, { "^(electric guitar|guitar)", "Guitar" },
    { "^(hammond organ|organ|electric piano|elec piano|e\\.? ?piano|epiano|rhodes|wurlitzer|keys|piano|keyboards?|clavinet|clav)\\b", "Keys" }, { "^(drums|drumset|drum set|drummer)", "Drums" }, { "^(percussion|percussionist)", "Percussion" },
    { "^(congas|bongos|timbales|cajon)", "Percussion" }, { "^violin", "Violin" }, { "^viola", "Viola" }, { "^cello", "Cello" }, { "^bassoon", "Bassoon" },
    { "^accordion", "Accordion" }, { "^strings", "Strings" }, { "^(vocals?|voice|lead vox)", "Vocals" }, { "^ewi", "EWI" },
    { "^didgeridoo", "Didgeridoo" }, { "^synth", "Keys" },
};

QString canonInstrument(const QString& raw)
{
    QString low = QString(raw).replace('_', ' ').simplified().toLower();
    QString num;
    static const QRegularExpression trailing("\\b([12])\\b\\s*$");
    static const QRegularExpression paren("\\s*\\(.*?\\)\\s*$");
    static const QRegularExpression key("\\bin b\\W*b?\\b|\\bin bb\\b|\\bin e\\W*b?\\b|\\bin eb\\b|\\bin c\\b");
    static const std::vector<std::pair<QRegularExpression, QString> > canon = []() {
        std::vector<std::pair<QRegularExpression, QString> > out;
        for (const auto& [pat, name] : CANON) {
            out.emplace_back(QRegularExpression(pat), name);
        }
        return out;
    }();
    const QRegularExpressionMatch m = trailing.match(low);
    if (m.hasMatch()) {
        num = " " + m.captured(1);
        low = low.left(m.capturedStart()).trimmed();
    }
    low.remove(paren);
    low.remove(key);
    low = low.trimmed();
    for (const auto& [re, name] : canon) {
        if (re.match(low).hasMatch()) {
            return name + num;
        }
    }
    return QString();
}

static QString instrumentBase(const QString& i)
{
    static const QRegularExpression num("\\s+[12]$");
    return QString(i).remove(num);
}

static QString family(const QString& i)
{
    const QString b = instrumentBase(i);
    return !hornAbbr(b).isEmpty() ? "horn" : RHYTHM.contains(b) ? "rhythm" : STRINGS.contains(b) ? "strings" : "other";
}

static QString instrumentFromPdf(const QString& path)
{
    static const QStringList SEARCH { "Soprano Saxophone", "Alto Saxophone", "Tenor Saxophone", "Baritone Saxophone", "Bari Sax",
                                      "Bass Saxophone", "Bass Sax", "Bass Clarinet", "Clarinet", "Piccolo", "Flute", "Flugelhorn", "Bass Trombone",
                                      "Trombone", "Trumpet", "Bass Guitar", "Electric Guitar", "Electric Piano", "Hammond Organ", "Keys",
                                      "Piano", "Drumset", "Drums", "Congas", "Percussion", "Violin", "Viola", "Cello", "Bassoon", "Strings",
                                      "Vocals", "Clavinet", "Synthesizer", "Guitar", "Organ" };
    static const std::vector<QRegularExpression> patterns = []() {
        std::vector<QRegularExpression> out;
        for (const QString& i : SEARCH) {
            out.emplace_back("\\b" + QRegularExpression::escape(i) + "\\b", QRegularExpression::CaseInsensitiveOption);
        }
        return out;
    }();
    const QString t = pdfFirstPageText(path);
    int bestPos = -1, bestLen = 0;
    QString best;
    for (int k = 0; k < SEARCH.size(); ++k) {
        const QString& i = SEARCH[k];
        const QRegularExpressionMatch m = patterns[k].match(t);
        if (m.hasMatch() && (bestPos < 0 || m.capturedStart() < bestPos || (m.capturedStart() == bestPos && i.size() > bestLen))) {
            bestPos = int(m.capturedStart());
            bestLen = int(i.size());
            best = i;
        }
    }
    return best.isEmpty() ? QString() : canonInstrument(best);
}

static bool hidden(const QString& name)
{
    return name.startsWith('.') || name.startsWith("~$") || name.startsWith("Icon\r");
}

//! Everything to file below a folder: files, and bundles (.logicx, .band, .pages…) as one item each, never their
//! insides. Hidden names and shortcuts (symlinks) are left alone. Paths come back relative to `base`, under `dirRel`.
//! Until Oct 2026 the 6 Inbox walk went inside bundles and filed a Logic project's inner files one by one, which
//! destroyed the project.
static void walkLeaves(const QString& base, const QString& dirRel, QStringList& out)
{
    for (const QFileInfo& fi : QDir(joinPath(base, dirRel)).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (fi.isSymLink() || hidden(fi.fileName())) {
            continue;
        }
        const QString rel = joinPath(dirRel, fi.fileName());
        if (fi.isDir() && !isBundle(fi.filePath())) {
            walkLeaves(base, rel, out);
        } else {
            out << rel;
        }
    }
}

// ------------------------------------------------------------------ Sheet Music (14 Oct 2026)
QJsonObject sheetRecordInSheetMusic(const QJsonObject& record)
{
    auto paths = [](const QJsonValue& v) {
        QJsonArray out;
        for (const QJsonValue& p : v.toArray()) {
            out.append(inSheetMusic(p.toString()));
        }
        return out;
    };
    QJsonObject r = record;
    if (record.contains("sheets")) {
        QJsonObject sheets;
        const QJsonObject old = record.value("sheets").toObject();
        for (auto it = old.begin(); it != old.end(); ++it) {
            const QString key = inSheetMusic(it.key());
            // (a sheet recorded at both places: the entry at its current place wins)
            if (!sheets.contains(key) || key == it.key()) {
                sheets[key] = it.value();
            }
        }
        r["sheets"] = sheets;
    }
    if (record.contains("tiers")) {
        QJsonObject tiers = record.value("tiers").toObject();
        for (auto it = tiers.begin(); it != tiers.end(); ++it) {
            QJsonObject t = it.value().toObject();
            if (t.contains("paths")) {
                t["paths"] = paths(t.value("paths"));
            }
            it.value() = t;
        }
        r["tiers"] = tiers;
    }
    if (record.contains("folders")) {
        QJsonObject folders;
        const QJsonObject old = record.value("folders").toObject();
        for (auto it = old.begin(); it != old.end(); ++it) {
            QJsonObject o = it.value().toObject();
            if (o.contains("required")) {
                o["required"] = paths(o.value("required"));
            }
            if (o.contains("ladder")) {
                QJsonArray ladder;
                for (const QJsonValue& step : o.value("ladder").toArray()) {
                    QJsonObject st = step.toObject();
                    st["paths"] = paths(st.value("paths"));
                    ladder.append(st);
                }
                o["ladder"] = ladder;
            }
            folders[songFolderPath(it.key())] = o;
        }
        r["folders"] = folders;
    }
    return r;
}

SheetMusicMove moveSongIntoSheetMusic(const QString& band, const QString& songRoot, const QString& code, const QString& todayIso)
{
    SheetMusicMove out;
    const QString song = band + "/" + songRoot;
    const QDir dir(song);
    if (songRoot.isEmpty() || !dir.exists()) {
        return out;
    }
    // A folder's files into another folder that may already hold some: the folder moves whole when the other isn't
    // there; else file by file, the newer of two files with one name kept and the other archived. The emptied folders
    // go (a folder with anything left in it, a .DS_Store included, stays). True when anything moved.
    auto mergeInto = [&](const QString& from, const QString& to) {
        if (!QFileInfo::exists(to)) {
            QDir().mkpath(QFileInfo(to).absolutePath());
            if (moveItem(from, to)) {
                return true;
            }
            out.failed << relativeTo(band, from);
            return false;
        }
        QStringList leaves;
        walkLeaves(from, QString(), leaves);
        bool any = false;
        for (const QString& rel : leaves) {
            const QString src = from + "/" + rel;
            const QString dst = to + "/" + rel;
            auto archive = [&](const QString& path) {
                const QString tgt = freeName(song + "/Version History/" + todayIso + " Superseded/" + relativeTo(song, dst));
                if (moveItem(path, tgt)) {
                    out.superseded.push_back({ relativeTo(band, path), relativeTo(band, tgt) });
                    return true;
                }
                return false;
            };
            if (QFileInfo::exists(dst)) {
                const bool srcNewer = QFileInfo(src).lastModified() > QFileInfo(dst).lastModified();
                if (!srcNewer) {
                    any |= archive(src);
                    continue;
                }
                if (!archive(dst)) {
                    out.failed << relativeTo(band, src);
                    continue;
                }
            }
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (moveItem(src, dst)) {
                any = true;
            } else {
                out.failed << relativeTo(band, src);
            }
        }
        QStringList dirs;
        QDirIterator it(from, QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            dirs << it.next();
        }
        std::sort(dirs.begin(), dirs.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
        for (const QString& d : dirs) {
            QDir().rmdir(d);
        }
        QDir().rmdir(from);
        return any;
    };
    for (const QFileInfo& fi : dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString name = fi.fileName();
        if (fi.isSymLink() || hidden(name) || staysAtSongTop(name)) {
            continue;
        }
        if (mergeInto(fi.filePath(), song + "/" + SHEET_MUSIC + "/" + name)) {
            out.moved << name;
        }
    }
    // The Half-Time and Double-Time sheets in their own folders beside Sheet Music (Joel, 15 Oct 2026): until 1.18.47
    // they were in a subfolder of each sheet folder ("Sheet Music/4H Tpt Alt Ten/Double-Time/…"), or of Sheet Music
    // itself for a sheet at the top of the song folder
    const QString sheets = song + "/" + SHEET_MUSIC;
    for (const QString& variant : { QString("Half-Time"), QString("Double-Time") }) {
        const QString target = song + "/" + sheetMusicFor(variant);
        bool any = false;
        if (QFileInfo(sheets + "/" + variant).isDir()) {
            any |= mergeInto(sheets + "/" + variant, target);
        }
        for (const QFileInfo& fi : QDir(sheets).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString inner = fi.filePath() + "/" + variant;
            if (!fi.isSymLink() && QFileInfo(inner).isDir()) {
                any |= mergeInto(inner, target + "/" + fi.fileName());
            }
        }
        if (any) {
            out.moved << sheetMusicFor(variant);
        }
    }

    // the sheet record: every path in it where the sheet is now
    if (!code.isEmpty()) {
        const QString recordPath = band + "/6 Inbox/.organizer/sheets/" + code + ".json";
        const JsonRead read = readJsonChecked(recordPath);
        if (read.ok && read.doc.isObject()) {
            const QJsonObject now = sheetRecordInSheetMusic(read.doc.object());
            if (now != read.doc.object()) {
                writeJson(recordPath, QJsonDocument(now));
            }
        }
    }
    return out;
}

// ------------------------------------------------------------------ Sheets and Demos
namespace {
struct BandFiler {
    const Paths& paths;
    BandFilingReport& report;
    const Roster& roster;

    QString abs(const QString& rel) const { return paths.band + "/" + rel; }

    //! Moves what is at dstRel to Version History; returns where it went ("" when nothing was there or it couldn't move)
    QString supersede(const QString& dstRel, const QString& song)
    {
        if (!QFileInfo::exists(abs(dstRel))) {
            return QString();
        }
        const QString rel = relativeTo(abs(song), abs(dstRel));
        const QString tgt = freeName(abs(song + "/Version History/" + paths.todayIso() + " Superseded/" + rel));
        if (moveItem(abs(dstRel), tgt)) {
            report.superseded.push_back({ dstRel, relativeTo(paths.band, tgt) });
            return tgt;
        }
        return QString();
    }

    //! sub: the folder as named in the song ("1 Rhythm", "3H Tpt Alt Ten/Section Scores", "Demos"); a sheet folder is in
    //! Sheet Music
    void place(const QString& srcRel, const QString& song, const QString& sub, const QString& newName)
    {
        const QString dst = song + "/" + songFolderPath(sub) + "/" + newName;
        if (QDir::cleanPath(abs(srcRel)) == QDir::cleanPath(abs(dst))) {
            return;
        }
        if (!QFileInfo::exists(abs(srcRel))) {
            report.unrecognised << srcRel + " (couldn't move it)";
            return;
        }
        const QString archived = supersede(dst, song);
        if (moveItem(abs(srcRel), abs(dst))) {
            report.filed.push_back({ srcRel, dst });
            return;
        }
        // the move failed after the sheet it replaces had gone to Version History: put that one back, so the
        // song isn't left without its sheet
        if (!archived.isEmpty() && moveItem(archived, abs(dst))) {
            report.superseded.pop_back();
        }
        report.unrecognised << srcRel + " (couldn't move it)";
    }

    QString existingHornFolder(const QString& song, int n, const QString& inst) const
    {
        QStringList cands;
        for (const QString& d : QDir(abs(song + "/" + SHEET_MUSIC)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (d.startsWith(QString("%1H ").arg(n)) || (n == 1 && d == "1H")) {
                cands << d;
            }
        }
        if (cands.isEmpty()) {
            return QString();
        }
        if (!inst.isEmpty()) {
            const QString ab = hornAbbr(instrumentBase(inst));
            for (const QString& c : cands) {
                if (!ab.isEmpty() && c.contains(ab)) {
                    return c;
                }
            }
        }
        return cands.first();
    }

    void fileOne(const QString& rel, const QString& song, const QString& code)
    {
        const QFileInfo fi(abs(rel));
        const QString name = fi.fileName();
        const QString base = fi.completeBaseName();
        const QString ext = fi.suffix().toLower();
        const QString songTitle = songTitleOf(song);
        auto restOf = [&]() {
            // the title and the code the file may start with ("Amplitudes - Bass", "AMPL - Bass"), then stray edges
            QString r = base;
            if (r.startsWith(songTitle, Qt::CaseInsensitive)) {
                r = r.mid(songTitle.size());
                static const QRegularExpression afterTitle("^\\s*[-_]?\\s*");
                r.remove(afterTitle);
            }
            if (r.startsWith(code)) {
                static const QRegularExpression dash("^\\s*-\\s*");
                const QRegularExpressionMatch m = dash.match(r.mid(code.size()));
                if (m.hasMatch()) {
                    r = r.mid(code.size() + m.capturedLength());
                }
            }
            static const QRegularExpression edge("^[\\s\\-_]+|[\\s\\-_]+$");
            return r.remove(edge);
        };

        if (isAudio(name)) {
            const QString rest = restOf();
            return place(rel, song, "Demos", QString("%1 - %2.%3").arg(code, rest.isEmpty() ? QString("Demo") : rest, fi.suffix()));
        }
        if (ext != "pdf") {
            return place(rel, song, "Extras", QString("%1 - %2.%3").arg(code, restOf().isEmpty() ? base : restOf(), fi.suffix()));
        }
        if (base.contains("What's Here") || base.endsWith(" - Recordings")) {
            return;
        }
        static const QRegularExpression leadSheet("lead[_ ]sheet", QRegularExpression::CaseInsensitiveOption);
        if (leadSheet.match(base).hasMatch() || pdfFirstPageText(abs(rel)).toLower().startsWith("lead sheet")) {
            return place(rel, song, leadSheetFolderOf(paths.band + "/" + song), code + " - Lead Sheet.pdf");
        }
        if (base.toLower().startsWith("arrangement guide")) {
            return place(rel, song, "Version History/" + paths.todayIso() + " Superseded Guides", name);
        }
        static const QRegularExpression arr("(\\d)\\s*-\\s*Horn[_ ]?Arr(?:angement)?", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = arr.match(base);
        if (m.hasMatch()) {
            const int n = m.captured(1).toInt();
            QString tail = base.mid(m.capturedEnd());
            static const QRegularExpression tailEdge("^[\\s_\\-:]+");
            tail.remove(tailEdge);
            if (tail.isEmpty() || QStringList { "horns", "score", "full score" }.contains(tail.trimmed().toLower())) {
                const QString f = existingHornFolder(song, n, QString());
                return place(rel, song, (f.isEmpty() ? QString("%1H Flexible").arg(n) : f) + "/Section Scores",
                             code + " - Section Score (Concert).pdf");
            }
            static const QRegularExpression chair("Horn[_ ](\\d)[_ ](?:in|for)[_ ](.+)$", QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch g = chair.match(tail);
            if (g.hasMatch()) {
                const QString key = g.captured(2).replace('_', ' ').trimmed();
                const QString lab = key.toLower().endsWith("clef") ? QString("Horn %1 (%2)").arg(g.captured(1), key)
                                    : QString("Horn %1 in %2").arg(g.captured(1), key);
                const QString f = existingHornFolder(song, n, QString());
                return place(rel, song, f.isEmpty() ? QString("%1H Flexible").arg(n) : f, code + " - " + lab + ".pdf");
            }
            QString inst = canonInstrument(tail);
            if (inst.isEmpty()) {
                inst = instrumentFromPdf(abs(rel));
            }
            if (!inst.isEmpty() && family(inst) == "horn") {
                const QString f = existingHornFolder(song, n, inst);
                return place(rel, song, f.isEmpty() ? QString("%1H %2").arg(n).arg(hornAbbr(instrumentBase(inst))) : f,
                             code + " - " + inst + ".pdf");
            }
        }
        const QString rest = restOf();
        QString inst = canonInstrument(rest);
        if (inst.isEmpty()) {
            inst = instrumentFromPdf(abs(rel));
        }
        if (inst.isEmpty()) {
            static const QRegularExpression playerName("^([A-Za-z]+)(?:[_ ]?\\(.*\\))?$");
            const QRegularExpressionMatch pm = playerName.match(rest);
            if (pm.hasMatch()) {
                auto hint = roster.fileNameHints.find(pm.captured(1));
                if (hint != roster.fileNameHints.end()) {
                    inst = hint->second;
                } else {
                    for (const Player& p : roster.players) {
                        if (p.name == pm.captured(1) && !p.instruments.isEmpty()) {
                            inst = p.instruments.first();
                        }
                    }
                }
            }
        }
        if (inst.isEmpty()) {
            report.unrecognised << rel;
            return;
        }
        const QString fam = family(inst);
        if (fam == "rhythm") {
            return place(rel, song, "1 Rhythm", code + " - " + inst + ".pdf");
        }
        if (fam == "horn") {
            // the horn folders that have this instrument; with more than one it's a guess, so leave it for Joel
            const QString ab = hornAbbr(instrumentBase(inst));
            const bool numbered = inst != instrumentBase(inst);
            QStringList fits;
            static const QRegularExpression hornFolder("^\\dH ");
            for (const QString& d : QDir(abs(song + "/" + SHEET_MUSIC)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (!hornFolder.match(d).hasMatch() || d.contains("Any") || d.contains("Flexible")) {
                    continue;
                }
                const QStringList toks = d.split(' ').mid(1);
                for (const QString& t : toks) {
                    if (t.endsWith(ab) && (!numbered || t.size() > ab.size())) {
                        fits << d;
                    }
                }
            }
            if (fits.size() == 1) {
                return place(rel, song, fits.first(), code + " - " + inst + ".pdf");
            }
            if (fits.isEmpty()) {
                return place(rel, song, "Extras", code + " - " + inst + ".pdf");
            }
            report.unrecognised << rel + " (more than one horn folder could take it: " + fits.join(", ") + ")";
            return;
        }
        if (fam == "strings") {
            QStringList sf;
            static const QRegularExpression stringsFolder("^\\dS ");
            for (const QString& d : QDir(abs(song + "/" + SHEET_MUSIC)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (stringsFolder.match(d).hasMatch()) {
                    sf << d;
                }
            }
            if (!sf.isEmpty()) {
                return place(rel, song, sf.first(), code + " - " + inst + ".pdf");
            }
        }
        place(rel, song, "Extras", code + " - " + inst + ".pdf");
    }
};
}

BandFilingReport fileBand(const Paths& paths, Codes& codes, const Roster& roster, QJsonObject& aliases, const Progress& progress)
{
    BandFilingReport report;
    BandFiler filer { paths, report, roster };
    const QDir base(paths.band);

    // --- song folders with a number but no code: register them (this is how Mxter Shirts sat unfiled until 21 Aug)
    QStringList roots;
    static const QRegularExpression songGroup("^[123] "), otherGroup("^[4-9] ");
    for (const QString& d : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (songGroup.match(d).hasMatch()) {
            roots << d;
        } else if (d == "4 Works In Progress") {
            for (const QString& w : QDir(base.filePath(d)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (!hidden(w)) {
                    roots << d + "/" + w;
                }
            }
        } else if (!hidden(d) && !otherGroup.match(d).hasMatch()) {
            report.newSongs << d;
        }
    }
    for (const QString& root : roots) {
        if (codes.band.count(root)) {
            continue;
        }
        // the code its files already use, if it's free; otherwise a new one
        QString code;
        QDirIterator it(base.filePath(root), QDir::Files, QDirIterator::Subdirectories);
        std::map<QString, int> seen;
        while (it.hasNext()) {
            it.next();
            if (it.filePath().contains("/Version History/")) {
                continue;
            }
            const QRegularExpressionMatch m = CODE_PREFIX.match(it.fileName());
            if (m.hasMatch()) {
                seen[m.captured(1)]++;
            }
        }
        const QStringList taken = codes.allCodes();
        int best = 0;
        for (const auto& [c, n] : seen) {
            if (n > best && !taken.contains(c)) {
                best = n;
                code = c;
            }
        }
        if (code.isEmpty()) {
            code = suggestCode(songTitleOf(root), taken);
        }
        codes.band[root] = code;
        codes.bandChanged = true;
        if (!aliases.contains(songTitleOf(root))) {
            aliases[songTitleOf(root)] = code;
        }
        report.registered << root + " → " + code;
    }

    // --- song folders laid out before 14 Oct 2026: their sheet folders into Sheet Music
    for (const auto& [song, code] : codes.band) {
        const SheetMusicMove m = moveSongIntoSheetMusic(paths.band, song, code, paths.todayIso());
        if (!m.moved.isEmpty()) {
            report.intoSheetMusic << song + ": " + m.moved.join(", ");
        }
        for (const Move& s : m.superseded) {
            report.superseded.push_back(s);
        }
        for (const QString& f : m.failed) {
            report.unrecognised << f + " (couldn't move it into Sheet Music)";
        }
    }

    // --- 6 Inbox
    std::map<QString, QString> byCode;
    struct Title {
        QString title, norm, root;    // bare title, normalizeName(title), song folder
    };
    std::vector<Title> byName;        // longest title first, so "Live Strong + Strasbourg" beats "Live Strong"
    for (const auto& [root, code] : codes.band) {
        byCode[code] = root;
        const QString title = songTitleOf(root);
        byName.push_back({ title, normalizeName(title), root });
    }
    std::sort(byName.begin(), byName.end(), [](const Title& a, const Title& b) { return a.title.size() > b.title.size(); });
    static const QRegularExpression codeStart("^([A-Z]{3,4})\\s*-\\s*");
    auto target = [&](const QString& fname, const QString& subdir) -> QString {
        const QRegularExpressionMatch m = codeStart.match(fname);
        if (m.hasMatch() && byCode.count(m.captured(1))) {
            return byCode[m.captured(1)];
        }
        const QString low = normalizeName(fname);
        const QString padded = " " + low + " ";
        for (const Title& t : byName) {
            if (low.startsWith(t.norm + " ") || low == t.norm || padded.contains(" " + t.norm + " ")) {
                return t.root;
            }
        }
        if (!subdir.isEmpty()) {
            if (byCode.count(subdir.trimmed())) {
                return byCode[subdir.trimmed()];
            }
            const QString sub = normalizeName(subdir);
            for (const Title& t : byName) {
                if (sub == t.norm) {
                    return t.root;
                }
            }
        }
        return QString();
    };
    const QString inbox = paths.band + "/6 Inbox";
    QDir().mkpath(inbox);
    QStringList inboxFiles;    // files and bundles, relative to 6 Inbox
    walkLeaves(inbox, QString(), inboxFiles);
    std::sort(inboxFiles.begin(), inboxFiles.end());
    for (const QString& rel : inboxFiles) {
        const QString fname = QFileInfo(rel).fileName();
        const QString sub = rel.contains('/') ? rel.section('/', 0, 0) : QString();
        const QString tgt = target(fname, sub);
        if (tgt.isEmpty()) {
            report.inboxUnmatched << "6 Inbox/" + rel;
            continue;
        }
        const QString dst = freeName(paths.band + "/" + tgt + "/" + fname);
        if (moveItem(inbox + "/" + rel, dst)) {
            report.inboxFiled.push_back({ "6 Inbox/" + rel, relativeTo(paths.band, dst) });
        }
    }

    // --- loose files in every song folder
    for (const auto& [song, code] : codes.band) {
        if (progress.stopped()) {
            break;
        }
        const QDir dir(paths.band + "/" + song);
        if (!dir.exists()) {
            continue;
        }
        // loose files, and bundles (a .logicx dropped in the song root goes to Demos as one item)
        static const QRegularExpression whatsHere("^[A-Z]{3,4} - What");
        for (const QFileInfo& fi : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString f = fi.fileName();
            if (fi.isSymLink() || hidden(f) || (fi.isDir() && !isBundle(fi.filePath()))) {
                continue;
            }
            if (whatsHere.match(f).hasMatch() || f.endsWith(" - Recordings.pdf")) {
                continue;
            }
            filer.fileOne(song + "/" + f, song, code);
        }
        // and loose in Sheet Music
        for (const QFileInfo& fi : QDir(dir.filePath(SHEET_MUSIC)).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QString f = fi.fileName();
            if (fi.isSymLink() || hidden(f) || (fi.isDir() && !isBundle(fi.filePath()))) {
                continue;
            }
            filer.fileOne(song + "/" + SHEET_MUSIC + "/" + f, song, code);
        }
        // The percussion sheet is called Percussion (it used to be named after the instrument: Congas, Bongos…)
        {
            const QDir rhythm(dir.filePath(songFolderPath("1 Rhythm")));
            const QString perc = code + " - Percussion.pdf";
            if (rhythm.exists() && !rhythm.exists(perc)) {
                for (const QString& old : { code + " - Congas.pdf", code + " - Bongos.pdf", code + " - Timbales.pdf",
                                            code + " - Cajon.pdf" }) {
                    if (rhythm.exists(old) && moveItem(rhythm.filePath(old), rhythm.filePath(perc))) {
                        report.filed.push_back({ song + "/" + songFolderPath("1 Rhythm") + "/" + old,
                                                 song + "/" + songFolderPath("1 Rhythm") + "/" + perc });
                        break;
                    }
                }
            }
        }
        // (a vocal cover's "1 Lead Sheet + Voice" stands for "1 Lead Sheet": an empty "1 Lead Sheet" left beside it goes)
        const QString lead = leadSheetFolderOf(dir.absolutePath());
        if (lead == LEAD_SHEET_VOICE_FOLDER) {
            const QString old = songFolderPath(LEAD_SHEET_FOLDER);
            QDir(dir.filePath(old)).remove(".DS_Store");
            dir.rmdir(old);
        }
        for (const QString& name : QStringList { lead, "1 Rhythm", "Horn Part Guides" }) {
            const QString want = songFolderPath(name);
            if (!dir.exists(want)) {
                dir.mkpath(want);
                report.foldersMade << song + "/" + want;
            }
        }
    }
    return report;
}

void redateUpdateNotes(const Paths& paths, const Codes& codes, const QStringList& changedRoots, BandFilingReport& report)
{
    const QString want = "Update Notes " + paths.today.toString("yy-MM-dd");
    for (const auto& [song, code] : codes.band) {
        QDir dir(paths.band + "/" + song);
        if (!dir.exists()) {
            continue;
        }
        const QStringList existing = dir.entryList({ "Update Notes*" }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (existing.isEmpty()) {
            dir.mkpath(want);
            report.foldersMade << song + "/" + want;
        } else if (changedRoots.contains(song) && existing.last() != want && !dir.exists(want)) {
            if (dir.rename(existing.last(), want)) {
                report.updateNotesRedated << QString("%1: %2 → %3").arg(song, existing.last(), want);
            }
        }
    }
}

// ------------------------------------------------------------------ Projects and Sheets
const QStringList& starsignSongGroups()
{
    static const QStringList GROUPS {
        "1 Starsign/1 Originals/1 Repertoire", "1 Starsign/1 Originals/2 In Progress", "1 Starsign/1 Originals/3 Sketches",
        "1 Starsign/2 Covers/1 Repertoire", "1 Starsign/2 Covers/2 In Progress",
        "1 Starsign/3 Vocal Covers/1 Repertoire", "1 Starsign/3 Vocal Covers/2 In Progress",
    };
    return GROUPS;
}

QString starsignGroupFor(const QString& kind, const QString& stage)
{
    const QString k = kind == "cover" ? "1 Starsign/2 Covers" : kind == "vocal" ? "1 Starsign/3 Vocal Covers" : "1 Starsign/1 Originals";
    if (stage == "repertoire") {
        return k + "/1 Repertoire";
    }
    if (stage == "sketch" && kind != "cover" && kind != "vocal") {
        return k + "/3 Sketches";
    }
    return k + "/2 In Progress";
}

LayoutReport renumberProjectsTopFolders(const Paths& paths)
{
    LayoutReport report;
    if (paths.projects.isEmpty()) {
        return report;
    }
    const QDir base(paths.projects);
    // (in this order: "4 Other Projects" is out of the way before "7 Sketches" becomes "4 Sketches")
    static const std::vector<std::pair<QString, QString> > RENAMES {
        { "STARSIGN", "1 Starsign" }, { "4 Other Projects", "2 Other Projects" }, { "6 Templates", "3 Templates" },
        { "7 Sketches", "4 Sketches" }, { "8 Quarantine", "5 Quarantine" }, { "9 Inbox", "6 Inbox" },
        { "10 Sweater Weather", "7 Sweater Weather" },
    };
    const QFileInfoList top = base.entryInfoList(QDir::Dirs | QDir::Files | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& [oldStart, newStart] : RENAMES) {
        for (const QFileInfo& f : top) {
            // the name, or the name and more ("8 Quarantine (empty template copies)")
            const QString name = f.fileName();
            if (name != oldStart && !name.startsWith(oldStart + " ")) {
                continue;
            }
            const QString to = newStart + name.mid(oldStart.size());
            const QFileInfo dst(base.filePath(to));
            if (dst.exists() || dst.isSymLink()) {
                report.problems << QString("\"%1\" wasn't renamed \"%2\": a folder of that name is already there").arg(name, to);
                continue;
            }
            // (a shortcut is renamed itself, not what it points to)
            if (::rename(QFile::encodeName(f.absoluteFilePath()).constData(), QFile::encodeName(dst.absoluteFilePath()).constData()) == 0) {
                report.moved.push_back({ name, to });
            } else {
                report.problems << QString("couldn't rename \"%1\" to \"%2\"").arg(name, to);
            }
        }
    }
    return report;
}

QString starsignKindOf(const QString& relOrPath)
{
    // (before the top folders were numbered: "STARSIGN/…")
    const QString p = QString("/" + QString(relOrPath).replace('\\', '/')).replace("/STARSIGN/", "/1 Starsign/");
    if (p.contains("/1 Starsign/1 Originals/") || p.contains("/1 Starsign Originals/") || p.contains("/3 Starsign WIP/")) {
        return "original";
    }
    if (p.contains("/1 Starsign/2 Covers/") || p.contains("/2 Starsign Covers/")) {
        return "cover";
    }
    if (p.contains("/1 Starsign/3 Vocal Covers/")) {
        return "vocal";
    }
    return QString();
}

QString starsignStageOf(const QString& relOrPath)
{
    const QString p = "/" + QString(relOrPath).replace('\\', '/');
    if (starsignKindOf(relOrPath).isEmpty()) {
        return QString();
    }
    if (p.contains("/1 Repertoire/") || p.contains("/1 Starsign Originals/") || p.contains("/2 Starsign Covers/")) {
        return "repertoire";
    }
    if (p.contains("/3 Sketches/") || p.contains("/3 Starsign WIP/")) {
        return "sketch";
    }
    return "progress";
}

namespace {
//! A folder's contents moved into another (made when missing); a name already there gets " 2"… (freeName)
void mergeFolder(const QString& base, const QString& srcRel, const QString& dstRel, LayoutReport& report)
{
    for (const QFileInfo& fi : QDir(base + "/" + srcRel).entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
        if (fi.fileName() == ".DS_Store") {
            continue;
        }
        const QString dst = freeName(base + "/" + dstRel + "/" + fi.fileName());
        if (fi.isDir() && !fi.isSymLink() && !isBundle(fi.filePath()) && QFileInfo(base + "/" + dstRel + "/" + fi.fileName()).isDir()) {
            mergeFolder(base, srcRel + "/" + fi.fileName(), dstRel + "/" + fi.fileName(), report);
            continue;
        }
        if (moveItem(fi.filePath(), dst)) {
            report.moved.push_back({ srcRel + "/" + fi.fileName(), relativeTo(base, dst) });
        } else {
            report.problems << srcRel + "/" + fi.fileName() + " (couldn't move it)";
        }
    }
    QDir(base + "/" + srcRel).remove(".DS_Store");
    QDir(base).rmdir(srcRel);
}

//! A tune folder moved whole to a group (or merged into the tune folder of the same name already there)
void moveTune(const QString& base, const QString& srcRel, const QString& group, LayoutReport& report)
{
    const QString name = QFileInfo(srcRel).fileName();
    const QString dstRel = group + "/" + name;
    if (QFileInfo::exists(base + "/" + dstRel)) {
        mergeFolder(base, srcRel, dstRel, report);
        return;
    }
    if (moveItem(base + "/" + srcRel, base + "/" + dstRel)) {
        report.moved.push_back({ srcRel, dstRel });
    } else {
        report.problems << srcRel + " (couldn't move it)";
    }
}
}

LayoutReport migrateProjectsLayout(const Paths& paths)
{
    LayoutReport report;
    if (paths.projects.isEmpty()) {
        return report;
    }
    const QString base = paths.projects;
    static const QString ORIGINALS = "1 Starsign Originals", COVERS = "2 Starsign Covers", WIP = "3 Starsign WIP";
    if (!QDir(base + "/" + ORIGINALS).exists() && !QDir(base + "/" + COVERS).exists() && !QDir(base + "/" + WIP).exists()) {
        return report;
    }
    for (const QString& g : starsignSongGroups()) {
        QDir().mkpath(base + "/" + g);
    }
    auto key = [](QString name) { return name.toLower().simplified(); };
    // Joel, 19 Oct 2026: the originals in progress, and the vocal covers
    static const QStringList IN_PROGRESS { "mxter shirts", "intern", "big milk", "jeju", "meevin" };
    static const QStringList VOCAL { "ghostbusters", "another star", "move on up" };
    // the songs with a folder in Sheets and Demos ("2 Bumper Cars"): the covers ready to play
    std::set<QString> inBand;
    static const QRegularExpression bandSong("^[123] (.+)$");
    for (const QString& d : QDir(paths.band).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QRegularExpressionMatch m = bandSong.match(d);
        if (m.hasMatch()) {
            inBand.insert(key(m.captured(1)));
        }
    }
    auto tunesOf = [&](const QString& group) {
        return QDir(base + "/" + group).entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    };
    for (const QString& group : { ORIGINALS, WIP, COVERS }) {
        if (!QDir(base + "/" + group).exists()) {
            continue;
        }
        for (const QFileInfo& t : tunesOf(group)) {
            const QString name = t.fileName();
            if (name.startsWith('.') || t.isSymLink()) {
                continue;
            }
            const QString rel = group + "/" + name;
            if (t.isFile()) {
                if (t.suffix().toLower() == "starscore") {
                    // a song file with no folder of its own (WIP's "Yenom Terces.starscore"): a folder named after it
                    const QString stage = group == ORIGINALS ? "repertoire" : "sketch";
                    const QString dst = starsignGroupFor(group == COVERS ? "cover" : "original", stage) + "/" + t.completeBaseName() + "/" + name;
                    if (moveItem(t.filePath(), base + "/" + dst)) {
                        report.moved.push_back({ rel, dst });
                    } else {
                        report.problems << rel + " (couldn't move it)";
                    }
                } else if (moveItem(t.filePath(), freeName(base + "/6 Inbox/" + name))) {
                    report.moved.push_back({ rel, "6 Inbox/" + name });   // (filed by the usual filing)
                }
                continue;
            }
            QString kind = "original", stage;
            if (group == COVERS) {
                kind = VOCAL.contains(key(name)) ? "vocal" : "cover";
                stage = inBand.count(key(name)) ? "repertoire" : "progress";
            } else if (IN_PROGRESS.contains(key(name))) {
                stage = "progress";
            } else {
                stage = group == ORIGINALS ? "repertoire" : "sketch";
            }
            moveTune(base, rel, starsignGroupFor(kind, stage), report);
        }
        // what's left is hidden (MuseScore's .mscbackup beside a loose file): into the group the songs went to
        const QString leftoversTo = starsignGroupFor(group == COVERS ? "cover" : "original", group == WIP ? "sketch" : "repertoire");
        for (const QFileInfo& h : QDir(base + "/" + group).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
            if (h.fileName() == ".DS_Store") {
                continue;
            }
            if (h.isDir() && QFileInfo(base + "/" + leftoversTo + "/" + h.fileName()).isDir()) {
                mergeFolder(base, group + "/" + h.fileName(), leftoversTo + "/" + h.fileName(), report);
            } else if (moveItem(h.filePath(), freeName(base + "/" + leftoversTo + "/" + h.fileName()))) {
                report.moved.push_back({ group + "/" + h.fileName(), leftoversTo + "/" + h.fileName() });
            }
        }
        QDir(base + "/" + group).remove(".DS_Store");
        if (!QDir(base).rmdir(group)) {
            report.problems << group + " (not empty, so it stays)";
        }
    }
    // "2 Other Projects/Mxter Shirts": its files into the Starsign Mxter Shirts' MuseScore Files
    const QString otherMxter = "2 Other Projects/Mxter Shirts";
    if (QDir(base + "/" + otherMxter).exists()) {
        QString starsignMxter;
        for (const QString& g : starsignSongGroups()) {
            if (QDir(base + "/" + g + "/Mxter Shirts").exists()) {
                starsignMxter = g + "/Mxter Shirts";
            }
        }
        if (!starsignMxter.isEmpty()) {
            mergeFolder(base, otherMxter, starsignMxter + "/MuseScore Files", report);
        }
    }
    return report;
}

std::vector<ProjectTune> listProjectTunes(const Paths& paths)
{
    // Listed once per run and shared (syncCodes, fileProjects, projectsSnapshot and the folder colours each used
    // to list the groups and every tune's *.starscore again). The 1 Starsign song folders and "2 Other Projects" (and
    // the old groups 1-3 while they're still there); the colours take only the Starsign ones, see ProjectTune::starsign.
    std::vector<ProjectTune> tunes;
    if (paths.projects.isEmpty()) {
        return tunes;
    }
    QStringList groups = starsignSongGroups();
    groups << "1 Starsign Originals" << "2 Starsign Covers" << "3 Starsign WIP" << "2 Other Projects";
    const QDir base(paths.projects);
    for (const QString& group : groups) {
        if (!base.exists(group)) {
            continue;
        }
        for (const QFileInfo& t : QDir(base.filePath(group)).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            // shortcuts (symlinks into another band's shared Drive) are not tunes of this folder
            if (t.isSymLink() || hidden(t.fileName())) {
                continue;
            }
            ProjectTune tune;
            tune.name = t.fileName();
            tune.group = group;
            tune.rel = group + "/" + tune.name;
            tune.starsign = group != "2 Other Projects";
            for (const QString& f : QDir(t.filePath()).entryList({ "*.starscore" }, QDir::Files, QDir::Name)) {
                tune.hasStarScore = true;
                const QRegularExpressionMatch m = CODE_PREFIX.match(f);
                if (m.hasMatch() && tune.fileCode.isEmpty()) {
                    tune.fileCode = m.captured(1);
                }
            }
            tunes.push_back(tune);
        }
    }
    return tunes;
}

QString ProjectTune::codeIn(const Codes& codes) const
{
    const auto known = codes.projects.find(name);
    return known != codes.projects.end() ? known->second : fileCode;
}

static QString purposeOf(const QString& path)
{
    const QString p = path.toLower();
    if (p.contains("for sale")) {
        return "For Sale";
    }
    if (p.contains("95jc")) {
        return "95JC";
    }
    if (p.contains("marching")) {
        return "Marching Band";
    }
    static const QRegularExpression deprecated("deprecated|antiquated|\\(old\\)|old sw|even older");
    static const QRegularExpression solo("\\bsolo\\b|comping|solo map");
    static const QRegularExpression reference("\\.(pdf|mp3|wav|mid|aif|aiff|jpg|png)$");
    if (deprecated.match(p).hasMatch()) {
        return "Deprecated";
    }
    if (solo.match(p).hasMatch()) {
        return "Solos & Practice";
    }
    if (p.contains("transcription")) {
        return "Transcriptions";
    }
    if (reference.match(p).hasMatch()) {
        return "Reference";
    }
    return QString();
}

ProjectsFilingReport fileProjects(const Paths& paths, const std::vector<ProjectTune>& tunes, const Progress& progress)
{
    ProjectsFilingReport report;
    if (paths.projects.isEmpty() || !QDir(paths.projects).exists()) {
        return report;
    }
    const QString base = paths.projects;
    const qint64 quiet = QDateTime::currentMSecsSinceEpoch() - 2 * 3600 * 1000;   // leave anything touched in the last 2 hours
    static const QStringList KEEP_TOP { "Projects Maintenance Report.pdf", "All Recordings.pdf", "Starsign Library.command" };

    // what to file: loose top-level files and everything in 6 Inbox (bundles move whole)
    QStringList targets;
    const QDir top(base);
    // Shortcuts (symlinks, e.g. "7 Sweater Weather" pointing into another band's shared Drive) are never
    // followed or moved: what they point to isn't part of this folder
    for (const QFileInfo& fi : top.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (fi.isSymLink() || hidden(fi.fileName()) || KEEP_TOP.contains(fi.fileName()) || (fi.isDir() && !isBundle(fi.filePath()))) {
            continue;
        }
        targets << fi.fileName();
    }
    const QString inbox = base + "/6 Inbox";
    QDir().mkpath(inbox);
    walkLeaves(base, "6 Inbox", targets);

    // one pattern per tune name, built once
    struct TuneMatch {
        const ProjectTune* tune;
        QRegularExpression re;
    };
    std::vector<TuneMatch> matchers;
    for (const ProjectTune& t : tunes) {
        matchers.push_back({ &t, QRegularExpression("(?<![A-Za-z])" + QRegularExpression::escape(t.name) + "(?![A-Za-z])",
                                                    QRegularExpression::CaseInsensitiveOption) });
    }
    auto findTune = [&](const QString& rel) -> QString {
        const QString folder = QFileInfo(rel).path().replace('_', ' ');
        const QString fname = QFileInfo(rel).fileName().replace('_', ' ');
        QString best;
        int bw = 0, bl = 0;
        for (const TuneMatch& tm : matchers) {
            const QString& t = tm.tune->name;
            for (const auto& [hay, w] : std::vector<std::pair<QString, int> > { { folder, 2 }, { fname, 1 } }) {
                if (tm.re.match(hay).hasMatch() && (w > bw || (w == bw && t.size() > bl))) {
                    best = tm.tune->rel;
                    bw = w;
                    bl = int(t.size());
                }
            }
        }
        if (best.isEmpty()) {
            // "AMPL - …" files: the tune whose .starscore carries that code
            const QRegularExpressionMatch m = CODE_PREFIX.match(QFileInfo(rel).fileName());
            if (m.hasMatch()) {
                for (const ProjectTune& t : tunes) {
                    if (t.fileCode == m.captured(1)) {
                        return t.rel;
                    }
                }
            }
        }
        return best;
    };

    for (const QString& rel : targets) {
        if (progress.stopped()) {
            break;
        }
        const QFileInfo fi(base + "/" + rel);
        if (fi.lastModified().toMSecsSinceEpoch() > quiet) {
            report.skippedRecent << rel;
            continue;
        }
        const QString tune = findTune(rel);
        if (tune.isEmpty()) {
            report.unmatched << rel;
            continue;
        }
        QString dst, archive;
        if (fi.suffix().toLower() == "starscore") {
            dst = tune + "/" + fi.fileName();
            archive = tune + "/Version History/" + paths.todayIso() + " Superseded/" + fi.fileName();
        } else {
            const QString sub = purposeOf(rel);
            dst = tune + "/MuseScore Files/" + (sub.isEmpty() ? QString() : sub + "/") + fi.fileName();
            archive = tune + "/MuseScore Files/Deprecated/" + paths.todayIso() + " Superseded/" + fi.fileName();
        }
        QString old;
        if (QFileInfo::exists(base + "/" + dst)) {
            old = freeName(base + "/" + archive);
            if (!moveItem(base + "/" + dst, old)) {
                report.unmatched << rel + " (couldn't move the file it replaces)";
                continue;
            }
            report.superseded.push_back({ dst, relativeTo(base, old) });
        }
        if (moveItem(base + "/" + rel, base + "/" + dst)) {
            report.filed.push_back({ rel, dst });
        } else {
            // the move failed after the file it replaces had gone to Deprecated: put that one back
            if (!old.isEmpty() && moveItem(old, base + "/" + dst)) {
                report.superseded.pop_back();
            }
            report.unmatched << rel + " (couldn't move it)";
        }
    }

    // empty folders -> "Z Empty Folders (safe to delete)/<where they were>"
    // One walk, children first: a folder that is empty once its empty children have moved out is seen on the way
    // back up. Group folders (1-4, 6-8) and tune folders stay even when empty; so does anything touched in the last
    // two hours (the walk looks at a folder's date after its children moved, so a parent that just emptied waits
    // for the next run, as it did when this took several passes).
    const QString EMPTY = "Z Empty Folders (safe to delete)";
    static const QStringList KEEP { EMPTY, ".organizer", "6 Inbox", "3 Templates", "4 Sketches" };
    auto isEmpty = [](const QString& p) {
        for (const QString& x : QDir(p).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
            if (x != ".DS_Store") {
                return false;
            }
        }
        return true;
    };
    std::function<void(const QString&)> sweep = [&](const QString& dirRel) {
        if (progress.stopped()) {
            return;
        }
        for (const QFileInfo& fi : QDir(base + "/" + dirRel).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
            const QString rel = dirRel.isEmpty() ? fi.fileName() : dirRel + "/" + fi.fileName();
            if (fi.isSymLink() || fi.fileName().startsWith('.') || isBundle(fi.filePath()) || KEEP.contains(rel.section('/', 0, 0))) {
                continue;
            }
            sweep(rel);
            // group folders (and 1 Starsign and its kinds) and tune folders stay, even empty
            const bool groupOrTune = std::any_of(starsignSongGroups().begin(), starsignSongGroups().end(), [&](const QString& g) {
                return g == rel || g.startsWith(rel + "/") || QFileInfo(rel).path() == g;
            });
            if (!groupOrTune && rel.count('/') >= 2 && isEmpty(fi.filePath())
                && QFileInfo(fi.filePath()).lastModified().toMSecsSinceEpoch() < quiet) {
                const QString d = freeName(base + "/" + EMPTY + "/" + rel);
                if (moveItem(base + "/" + rel, d)) {
                    report.swept.push_back({ rel, relativeTo(base, d) });
                }
            }
        }
    };
    sweep(QString());
    return report;
}

QJsonObject projectsSnapshot(const Paths& paths, const Codes& codes, const std::vector<ProjectTune>& tunes)
{
    QJsonObject o;
    if (paths.projects.isEmpty()) {
        return o;
    }
    // One walk of the folder gives every count; each tune's figures come from the path prefix (until Oct 2026
    // this walked the folder once and then every tune four more times).
    struct Counts {
        int museScoreFiles = 0, versionHistory = 0, deprecated = 0;
    };
    std::map<QString, Counts> perTune;   // by rel
    std::map<QString, const ProjectTune*> tuneByRel;
    for (const ProjectTune& t : tunes) {
        tuneByRel[t.rel] = &t;
    }
    int files = 0, starscore = 0, mscz = 0, templates = 0, quarantined = 0;
    QDirIterator it(paths.projects, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QString rel = relativeTo(paths.projects, it.filePath());
        if (rel.startsWith(".organizer/") || it.fileName() == ".DS_Store" || rel.contains(".mscbackup/") || rel.contains(".logicx/")) {
            continue;
        }
        ++files;
        const QString suf = it.fileInfo().suffix().toLower();
        if (suf == "starscore") {
            ++starscore;
        } else if (suf == "mscz" || suf == "mscx") {
            ++mscz;
            templates += rel.startsWith("3 Templates/");
        }
        quarantined += rel.startsWith("5 Quarantine");
        // "1 Starsign/1 Originals/1 Repertoire/Amplitudes/MuseScore Files/…": the tune is the first two segments
        // ("2 Other Projects/…") or the first four (1 Starsign)
        const QStringList seg = rel.split('/');
        const int depth = seg.value(0) == "1 Starsign" ? 4 : 2;
        const QString tuneRel = seg.mid(0, depth).join('/');
        if (seg.size() <= depth || !tuneByRel.count(tuneRel)) {
            continue;
        }
        Counts& c = perTune[tuneRel];
        const QString inTune = seg.mid(depth).join('/');
        c.museScoreFiles += inTune.startsWith("MuseScore Files/");
        c.versionHistory += inTune.startsWith("Version History/");
        c.deprecated += inTune.startsWith("Deprecated/") || inTune.startsWith("MuseScore Files/Deprecated/");
    }
    std::vector<const ProjectTune*> ordered;   // by group, then name (the list is already by group, then name)
    for (const ProjectTune& t : tunes) {
        ordered.push_back(&t);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const ProjectTune* a, const ProjectTune* b) {
        return a->group != b->group ? a->group < b->group : a->name < b->name;
    });
    QJsonArray tunesJson;
    for (const ProjectTune* t : ordered) {
        const Counts c = perTune.count(t->rel) ? perTune[t->rel] : Counts();
        tunesJson.append(QJsonObject { { "name", t->name }, { "group", t->group }, { "code", t->codeIn(codes) }, { "starscore", t->hasStarScore },
                                       { "museScoreFiles", c.museScoreFiles }, { "versionHistory", c.versionHistory },
                                       { "deprecated", c.deprecated } });
    }
    o["files"] = files;
    o["starscore"] = starscore;
    o["mscz"] = mscz;
    o["templates"] = templates;
    o["quarantined"] = quarantined;
    o["tunes"] = tunesJson;
    return o;
}

// ------------------------------------------------------------------ codes
QStringList syncCodes(const Paths& paths, Codes& codes, const std::vector<ProjectTune>& tunes)
{
    QStringList log, added;
    if (paths.projects.isEmpty()) {
        return log;
    }
    std::map<QString, QString> bandByTitle;     // normalized title -> code
    std::map<QString, QString> bandTitleOf;     // code -> band title
    for (const auto& [root, code] : codes.band) {
        bandByTitle[normalizeName(songTitleOf(root))] = code;
        bandTitleOf[code] = songTitleOf(root);
    }
    for (const ProjectTune& t : tunes) {
        // the code: the .starscore's "CODE - Title.starscore", then the band folder by title
        const QString& fileCode = t.fileCode;
        const QString& tune = t.name;
        const QString norm = normalizeName(tune);
        QString bandCode = bandByTitle.count(norm) ? bandByTitle[norm] : QString();
        if (bandCode.isEmpty() && !fileCode.isEmpty() && bandTitleOf.count(fileCode)) {
            bandCode = fileCode;    // same song, spelt differently ("Feed Your Kids Bugs" / "Feed Your Kid Bugs")
        }
        const QString want = !bandCode.isEmpty() ? bandCode : fileCode;
        if (!fileCode.isEmpty() && !bandCode.isEmpty() && fileCode != bandCode) {
            log << QString("<b>%1</b>: its StarScore file says <span class=\"mono\">%2</span> but Sheets and Demos uses "
                           "<span class=\"mono\">%3</span>; the band folder&rsquo;s code is used. Rename the file to match when convenient.")
                .arg(esc(tune), fileCode, bandCode);
        }
        if (want.isEmpty()) {
            continue;
        }
        auto it = codes.projects.find(tune);
        if (it == codes.projects.end()) {
            codes.projects[tune] = want;
            codes.projectsChanged = true;
            added << QString("%1 (<span class=\"mono\">%2</span>)").arg(esc(tune), want);
        } else if (it->second != want && !bandCode.isEmpty()) {
            log << QString("<b>%1</b>: <span class=\"mono\">codes_proj.json</span> said <span class=\"mono\">%2</span>, now "
                           "<span class=\"mono\">%3</span> to match Sheets and Demos.").arg(esc(tune), it->second, want);
            it->second = want;
            codes.projectsChanged = true;
        }
    }
    if (!added.isEmpty()) {
        log.prepend("Added to <span class=\"mono\">codes_proj.json</span>: " + added.join(", ") + ".");
    }
    // a Projects-only code that a band song also uses
    for (const auto& [title, code] : codes.projects) {
        if (bandTitleOf.count(code) && normalizeName(bandTitleOf[code]) != normalizeName(title)
            && !codes.projects.count(bandTitleOf[code])) {
            // "Feed Your Kids Bugs" next to the band's "Feed Your Kid Bugs" is the same song; only warn when unsure
            if (!normalizeName(bandTitleOf[code]).startsWith(normalizeName(title).left(6))) {
                log << QString("<span class=\"mono\">%1</span> is <b>%2</b> in Projects and <b>%3</b> in Sheets and Demos &mdash; "
                               "worth a look.").arg(code, esc(title), esc(bandTitleOf[code]));
            }
        }
    }
    return log;
}

// ------------------------------------------------------------------ retiring the Python toolkits
QStringList retireOldToolkits(const Paths& paths)
{
    // only the Python organizer and its leftovers; data StarScore uses and the records of the August 2026 migration
    // (manifest.tsv, regroup.tsv) stay, and so do the Starsign Library app (songlib) and the Any-Horn converter (anyhorn)
    QStringList moved;
    auto retire = [&](const QString& folder, const QStringList& patterns) {
        const QString dest = folder + "/Deprecated/Retired " + paths.todayIso();
        const QStringList names = QDir(folder).entryList(patterns, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
        for (const QString& n : names) {
            // the RULES.md StarScore writes stays (a first run that was cut short already wrote it)
            if (n == "RULES.md") {
                QFile f(folder + "/" + n);
                if (f.open(QIODevice::ReadOnly) && f.read(200).contains("StarScore Studio does all of this")) {
                    continue;
                }
            }
            if (moveItem(folder + "/" + n, freeName(dest + "/" + n))) {
                moved << n;
            }
        }
    };
    retire(paths.toolkit, { "organize.py", "deploy.py", "place_pdfs.py", "scripts", "scratch", "__pycache__", "Colour Song Folders.command",
                            "density.tsv", "fingerprints.json", "tree.tsv", "dates.tsv", "changes.json", "report.json", "maintcounts.json",
                            "ratings.json", "hornparts.json", "hornparts.zip", "pdfs.zip", "RULES.md" });
    if (!paths.projToolkit.isEmpty()) {
        retire(paths.projToolkit, { "*.py", "scripts", "scratch", "__pycache__", "listing.tsv", "report.json", "fingerprints.json",
                                    "maintlog.json", "batch*.json", "all_live.json", "all_rel.json", "empty_retired.json", "inv*.json",
                                    "inventory.json", "movelog.txt", "mscz_index.json", "named_parts.json", "plan.json",
                                    "projstate_fresh.json", "rebuild_state.json", "recheck.json", "render_batches.json", "rename_plan.json",
                                    "rollout*", "structcheck*.json", "sweep*.json", "*.zip", "MXTR-main-synced-*.mscz" });
    }
    return moved;
}
}
