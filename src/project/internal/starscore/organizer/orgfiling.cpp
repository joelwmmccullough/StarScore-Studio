/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: filing (the old organize.py and maintain.py)
 */
#include "orgfiling.h"

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
static const QStringList HORNS { "Trumpet", "Flugelhorn", "Flute", "Clarinet", "Soprano Sax", "Alto Sax", "Tenor Sax", "Bari Sax",
                                 "Bass Sax", "Bass Clarinet", "Trombone", "Bass Trombone" };
static const QMap<QString, QString> HORN_ABBR { { "Trumpet", "Tpt" }, { "Flugelhorn", "Flg" }, { "Flute", "Flu" }, { "Clarinet", "Cla" },
    { "Soprano Sax", "Sop" }, { "Alto Sax", "Alt" }, { "Tenor Sax", "Ten" }, { "Bari Sax", "Bar" }, { "Bass Sax", "Bsx" },
    { "Bass Clarinet", "Bcl" }, { "Trombone", "Tbn" }, { "Bass Trombone", "Btb" } };
static const QStringList RHYTHM { "Bass", "Guitar", "Keys", "Elec Piano", "Organ", "Bass Synth", "Drums", "Percussion", "Congas", "Clavinet" };
static const QStringList STRINGS { "Violin", "Viola", "Cello", "Strings", "Bassoon", "Accordion" };

static const std::vector<std::pair<QString, QString> > CANON {
    { "^bass tromb", "Bass Trombone" }, { "^(bass sax|bass saxophone)", "Bass Sax" }, { "^tromb", "Trombone" },
    { "^bass clarinet", "Bass Clarinet" }, { "^clarinet", "Clarinet" }, { "^(soprano sax|soprano saxophone|sop sax)", "Soprano Sax" },
    { "^(alto sax|alto saxophone)", "Alto Sax" }, { "^(tenor sax|tenor saxophone)", "Tenor Sax" },
    { "^(bari sax|baritone sax|baritone saxophone)", "Bari Sax" }, { "^flugel", "Flugelhorn" }, { "^(trumpet|tpt)", "Trumpet" },
    { "^flute", "Flute" }, { "^sop.*recorder", "Recorder" }, { "^bass synth", "Bass Synth" },
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
    const QRegularExpressionMatch m = trailing.match(low);
    if (m.hasMatch()) {
        num = " " + m.captured(1);
        low = low.left(m.capturedStart()).trimmed();
    }
    low.remove(QRegularExpression("\\s*\\(.*?\\)\\s*$"));
    low.remove(QRegularExpression("\\bin b\\W*b?\\b|\\bin bb\\b|\\bin e\\W*b?\\b|\\bin eb\\b|\\bin c\\b"));
    low = low.trimmed();
    for (const auto& [pat, name] : CANON) {
        if (QRegularExpression(pat).match(low).hasMatch()) {
            return name + num;
        }
    }
    return QString();
}

static QString instrumentBase(const QString& i)
{
    return QString(i).remove(QRegularExpression("\\s+[12]$"));
}

static QString family(const QString& i)
{
    const QString b = instrumentBase(i);
    return HORN_ABBR.contains(b) ? "horn" : RHYTHM.contains(b) ? "rhythm" : STRINGS.contains(b) ? "strings" : "other";
}

static QString instrumentFromPdf(const QString& path)
{
    static const QStringList SEARCH { "Soprano Saxophone", "Alto Saxophone", "Tenor Saxophone", "Baritone Saxophone", "Bari Sax",
                                      "Bass Saxophone", "Bass Sax", "Bass Clarinet", "Clarinet", "Flute", "Flugelhorn", "Bass Trombone",
                                      "Trombone", "Trumpet", "Bass Guitar", "Electric Guitar", "Electric Piano", "Hammond Organ", "Keys",
                                      "Piano", "Drumset", "Drums", "Congas", "Percussion", "Violin", "Viola", "Cello", "Bassoon", "Strings",
                                      "Vocals", "Clavinet", "Synthesizer", "Guitar", "Organ" };
    const QString t = pdfFirstPageText(path);
    int bestPos = -1, bestLen = 0;
    QString best;
    for (const QString& i : SEARCH) {
        const QRegularExpressionMatch m = QRegularExpression("\\b" + QRegularExpression::escape(i) + "\\b",
                                                             QRegularExpression::CaseInsensitiveOption).match(t);
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

// ------------------------------------------------------------------ Sheets and Demos
namespace {
struct BandFiler {
    const Paths& paths;
    BandFilingReport& report;
    const Roster& roster;

    QString abs(const QString& rel) const { return paths.band + "/" + rel; }

    void supersede(const QString& dstRel, const QString& song)
    {
        if (!QFileInfo::exists(abs(dstRel))) {
            return;
        }
        const QString rel = relativeTo(abs(song), abs(dstRel));
        const QString tgt = freeName(abs(song + "/Version History/Superseded " + paths.todayIso() + "/" + rel));
        if (moveItem(abs(dstRel), tgt)) {
            report.superseded.push_back({ dstRel, relativeTo(paths.band, tgt) });
        }
    }

    void place(const QString& srcRel, const QString& song, const QString& sub, const QString& newName)
    {
        const QString dst = song + "/" + sub + "/" + newName;
        if (QDir::cleanPath(abs(srcRel)) == QDir::cleanPath(abs(dst))) {
            return;
        }
        supersede(dst, song);
        if (moveItem(abs(srcRel), abs(dst))) {
            report.filed.push_back({ srcRel, dst });
        } else {
            report.unrecognised << srcRel + " (couldn't move it)";
        }
    }

    QString existingHornFolder(const QString& song, int n, const QString& inst) const
    {
        QStringList cands;
        for (const QString& d : QDir(abs(song)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (d.startsWith(QString("%1H ").arg(n)) || (n == 1 && d == "1H")) {
                cands << d;
            }
        }
        if (cands.isEmpty()) {
            return QString();
        }
        if (!inst.isEmpty()) {
            const QString ab = HORN_ABBR.value(instrumentBase(inst));
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
        const QString songTitle = QFileInfo(song).fileName().mid(song.startsWith("4 Works In Progress/") ? 0 : 2);
        auto restOf = [&]() {
            QString r = base;
            r.remove(QRegularExpression("^" + QRegularExpression::escape(songTitle) + "\\s*[-_]?\\s*", QRegularExpression::CaseInsensitiveOption));
            r.remove(QRegularExpression("^" + code + "\\s*-\\s*"));
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
        if (QRegularExpression("lead[_ ]sheet", QRegularExpression::CaseInsensitiveOption).match(base).hasMatch()
            || pdfFirstPageText(abs(rel)).toLower().startsWith("lead sheet")) {
            return place(rel, song, "1 Lead Sheet", code + " - Lead Sheet.pdf");
        }
        if (base.toLower().startsWith("arrangement guide")) {
            return place(rel, song, "Version History/Superseded Guides", name);
        }
        static const QRegularExpression arr("(\\d)\\s*-\\s*Horn[_ ]?Arr(?:angement)?", QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch m = arr.match(base);
        if (m.hasMatch()) {
            const int n = m.captured(1).toInt();
            QString tail = base.mid(m.capturedEnd());
            tail.remove(QRegularExpression("^[\\s_\\-:]+"));
            if (tail.isEmpty() || QStringList { "horns", "score", "full score" }.contains(tail.trimmed().toLower())) {
                const QString f = existingHornFolder(song, n, QString());
                return place(rel, song, f.isEmpty() ? QString("%1H Any Horns").arg(n) : f, code + " - Score.pdf");
            }
            static const QRegularExpression chair("Horn[_ ](\\d)[_ ](?:in|for)[_ ](.+)$", QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch g = chair.match(tail);
            if (g.hasMatch()) {
                const QString key = g.captured(2).replace('_', ' ').trimmed();
                const QString lab = key.toLower().endsWith("clef") ? QString("Horn %1 (%2)").arg(g.captured(1), key)
                                    : QString("Horn %1 in %2").arg(g.captured(1), key);
                const QString f = existingHornFolder(song, n, QString());
                return place(rel, song, f.isEmpty() ? QString("%1H Any Horns").arg(n) : f, code + " - " + lab + ".pdf");
            }
            QString inst = canonInstrument(tail);
            if (inst.isEmpty()) {
                inst = instrumentFromPdf(abs(rel));
            }
            if (!inst.isEmpty() && family(inst) == "horn") {
                const QString f = existingHornFolder(song, n, inst);
                return place(rel, song, f.isEmpty() ? QString("%1H %2").arg(n).arg(HORN_ABBR.value(instrumentBase(inst))) : f,
                             code + " - " + inst + ".pdf");
            }
        }
        const QString rest = restOf();
        QString inst = canonInstrument(rest);
        if (inst.isEmpty()) {
            inst = instrumentFromPdf(abs(rel));
        }
        if (inst.isEmpty()) {
            const QRegularExpressionMatch pm = QRegularExpression("^([A-Za-z]+)(?:[_ ]?\\(.*\\))?$").match(rest);
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
            const QString ab = HORN_ABBR.value(instrumentBase(inst));
            const bool numbered = inst != instrumentBase(inst);
            QStringList fits;
            for (const QString& d : QDir(abs(song)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (!QRegularExpression("^\\dH ").match(d).hasMatch() || d.contains("Any")) {
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
            for (const QString& d : QDir(abs(song)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (QRegularExpression("^\\dS ").match(d).hasMatch()) {
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

static QString songTitleOf(const QString& root)
{
    return root.startsWith("4 Works In Progress/") ? root.mid(20) : root.mid(2);
}

BandFilingReport fileBand(const Paths& paths, Codes& codes, const Roster& roster, QJsonObject& aliases, const Progress& progress)
{
    BandFilingReport report;
    BandFiler filer { paths, report, roster };
    const QDir base(paths.band);

    // --- song folders with a number but no code: register them (this is how Mxter Shirts sat unfiled until 21 Aug)
    QStringList roots;
    for (const QString& d : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (QRegularExpression("^[123] ").match(d).hasMatch()) {
            roots << d;
        } else if (d == "4 Works In Progress") {
            for (const QString& w : QDir(base.filePath(d)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (!hidden(w)) {
                    roots << d + "/" + w;
                }
            }
        } else if (!hidden(d) && !QRegularExpression("^[4-9] ").match(d).hasMatch()) {
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
            const QRegularExpressionMatch m = QRegularExpression("^([A-Z]{4}) - ").match(it.fileName());
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

    // --- 6 Inbox
    std::map<QString, QString> byCode;
    std::vector<std::pair<QString, QString> > byName;   // (bare title, root), longest first
    for (const auto& [root, code] : codes.band) {
        byCode[code] = root;
        byName.emplace_back(songTitleOf(root), root);
    }
    std::sort(byName.begin(), byName.end(), [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    auto target = [&](const QString& fname, const QString& subdir) -> QString {
        const QRegularExpressionMatch m = QRegularExpression("^([A-Z]{3,4})\\s*-\\s*").match(fname);
        if (m.hasMatch() && byCode.count(m.captured(1))) {
            return byCode[m.captured(1)];
        }
        const QString low = normalizeName(fname);
        for (const auto& [title, root] : byName) {
            const QString t = normalizeName(title);
            if (low.startsWith(t + " ") || low == t || (" " + low + " ").contains(" " + t + " ")) {
                return root;
            }
        }
        if (!subdir.isEmpty()) {
            if (byCode.count(subdir.trimmed())) {
                return byCode[subdir.trimmed()];
            }
            for (const auto& [title, root] : byName) {
                if (normalizeName(subdir) == normalizeName(title)) {
                    return root;
                }
            }
        }
        return QString();
    };
    const QString inbox = paths.band + "/6 Inbox";
    QDir().mkpath(inbox);
    std::vector<QString> inboxFiles;
    QDirIterator iit(inbox, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iit.hasNext()) {
        iit.next();
        const QString rel = relativeTo(inbox, iit.filePath());
        bool skip = false;
        for (const QString& seg : rel.split('/')) {
            skip |= hidden(seg);
        }
        if (!skip) {
            inboxFiles.push_back(rel);
        }
    }
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
        for (const QString& f : dir.entryList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name)) {
            if (hidden(f) || QRegularExpression("^[A-Z]{3,4} - What").match(f).hasMatch() || f.endsWith(" - Recordings.pdf")) {
                continue;
            }
            filer.fileOne(song + "/" + f, song, code);
        }
        // The percussion sheet is called Percussion (it used to be named after the instrument: Congas, Bongos…)
        {
            const QDir rhythm(dir.filePath("1 Rhythm"));
            const QString perc = code + " - Percussion.pdf";
            if (rhythm.exists() && !rhythm.exists(perc)) {
                for (const QString& old : { code + " - Congas.pdf", code + " - Bongos.pdf", code + " - Timbales.pdf",
                                            code + " - Cajon.pdf" }) {
                    if (rhythm.exists(old) && moveItem(rhythm.filePath(old), rhythm.filePath(perc))) {
                        report.filed.push_back({ song + "/1 Rhythm/" + old, song + "/1 Rhythm/" + perc });
                        break;
                    }
                }
            }
        }
        for (const QString& want : QStringList { "1 Lead Sheet", "1 Rhythm", "Horn Part Guides" }) {
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
std::map<QString, QString> projectTunes(const Paths& paths)
{
    std::map<QString, QString> tunes;
    if (paths.projects.isEmpty()) {
        return tunes;
    }
    const QDir base(paths.projects);
    for (const QString& group : base.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!QRegularExpression("^[1234] ").match(group).hasMatch()) {
            continue;
        }
        for (const QString& t : QDir(base.filePath(group)).entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (!hidden(t)) {
                tunes[t] = group + "/" + t;
            }
        }
    }
    return tunes;
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
    if (QRegularExpression("deprecated|antiquated|\\(old\\)|old sw|even older").match(p).hasMatch()) {
        return "Deprecated";
    }
    if (QRegularExpression("\\bsolo\\b|comping|solo map").match(p).hasMatch()) {
        return "Solos & Practice";
    }
    if (p.contains("transcription")) {
        return "Transcriptions";
    }
    if (QRegularExpression("\\.(pdf|mp3|wav|mid|aif|aiff|jpg|png)$").match(p).hasMatch()) {
        return "Reference";
    }
    return QString();
}

ProjectsFilingReport fileProjects(const Paths& paths, const Progress& progress)
{
    ProjectsFilingReport report;
    if (paths.projects.isEmpty() || !QDir(paths.projects).exists()) {
        return report;
    }
    const QString base = paths.projects;
    const qint64 quiet = QDateTime::currentMSecsSinceEpoch() - 2 * 3600 * 1000;   // leave anything touched in the last 2 hours
    const std::map<QString, QString> tunes = projectTunes(paths);
    static const QStringList KEEP_TOP { "Projects Maintenance Report.pdf", "All Recordings.pdf", "Starsign Library.command" };

    // what to file: loose top-level files and everything in 9 Inbox (bundles move whole)
    QStringList targets;
    const QDir top(base);
    // Shortcuts (symlinks, e.g. "10 Sweater Weather" pointing into another band's shared Drive) are never
    // followed or moved: what they point to isn't part of this folder
    for (const QFileInfo& fi : top.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (fi.isSymLink() || hidden(fi.fileName()) || KEEP_TOP.contains(fi.fileName()) || (fi.isDir() && !isBundle(fi.filePath()))) {
            continue;
        }
        targets << fi.fileName();
    }
    const QString inbox = base + "/9 Inbox";
    QDir().mkpath(inbox);
    std::function<void(const QString&)> walk = [&](const QString& dirRel) {
        for (const QFileInfo& fi : QDir(base + "/" + dirRel).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (fi.isSymLink() || hidden(fi.fileName())) {
                continue;
            }
            const QString rel = dirRel + "/" + fi.fileName();
            if (fi.isDir() && !isBundle(fi.filePath())) {
                walk(rel);
            } else {
                targets << rel;
            }
        }
    };
    walk("9 Inbox");

    auto findTune = [&](const QString& rel) -> QString {
        const QString folder = QFileInfo(rel).path().replace('_', ' ');
        const QString fname = QFileInfo(rel).fileName().replace('_', ' ');
        QString best;
        int bw = 0, bl = 0;
        for (const auto& [t, d] : tunes) {
            const QRegularExpression re("(?<![A-Za-z])" + QRegularExpression::escape(t) + "(?![A-Za-z])", QRegularExpression::CaseInsensitiveOption);
            for (const auto& [hay, w] : std::vector<std::pair<QString, int> > { { folder, 2 }, { fname, 1 } }) {
                if (re.match(hay).hasMatch() && (w > bw || (w == bw && t.size() > bl))) {
                    best = d;
                    bw = w;
                    bl = int(t.size());
                }
            }
        }
        if (best.isEmpty()) {
            // "AMPL - …" files: the tune whose .starscore carries that code
            const QRegularExpressionMatch m = QRegularExpression("^([A-Z]{4}) - ").match(QFileInfo(rel).fileName());
            if (m.hasMatch()) {
                for (const auto& [t, d] : tunes) {
                    if (!QDir(base + "/" + d).entryList({ m.captured(1) + " - *.starscore" }, QDir::Files).isEmpty()) {
                        return d;
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
            archive = tune + "/Version History/Superseded " + paths.todayIso() + "/" + fi.fileName();
        } else {
            const QString sub = purposeOf(rel);
            dst = tune + "/MuseScore Files/" + (sub.isEmpty() ? QString() : sub + "/") + fi.fileName();
            archive = tune + "/MuseScore Files/Deprecated/Superseded " + paths.todayIso() + "/" + fi.fileName();
        }
        if (QFileInfo::exists(base + "/" + dst)) {
            const QString old = freeName(base + "/" + archive);
            if (!moveItem(base + "/" + dst, old)) {
                report.unmatched << rel + " (couldn't move the file it replaces)";
                continue;
            }
            report.superseded.push_back({ dst, relativeTo(base, old) });
        }
        if (moveItem(base + "/" + rel, base + "/" + dst)) {
            report.filed.push_back({ rel, dst });
        } else {
            report.unmatched << rel + " (couldn't move it)";
        }
    }

    // empty folders -> "Z Empty Folders (safe to delete)/<where they were>"
    const QString EMPTY = "Z Empty Folders (safe to delete)";
    static const QStringList KEEP { EMPTY, ".organizer", "9 Inbox", "6 Templates", "7 Sketches" };
    auto isEmpty = [](const QString& p) {
        for (const QString& x : QDir(p).entryList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot)) {
            if (x != ".DS_Store") {
                return false;
            }
        }
        return true;
    };
    for (int pass = 0; pass < 4 && !progress.stopped(); ++pass) {
        QStringList cands;
        std::function<void(const QString&)> scan = [&](const QString& dirRel) {
            if (progress.stopped()) {
                return;
            }
            for (const QFileInfo& fi : QDir(base + "/" + dirRel).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Name)) {
                const QString rel = dirRel.isEmpty() ? fi.fileName() : dirRel + "/" + fi.fileName();
                if (fi.isSymLink() || fi.fileName().startsWith('.') || isBundle(fi.filePath()) || KEEP.contains(rel.section('/', 0, 0))) {
                    continue;
                }
                scan(rel);
                // group folders (1–4, 6–8) and tune folders stay even when empty
                if (rel.count('/') >= 2 && isEmpty(fi.filePath()) && fi.lastModified().toMSecsSinceEpoch() < quiet) {
                    cands << rel;
                }
            }
        };
        scan(QString());
        if (cands.isEmpty() || progress.stopped()) {
            break;
        }
        for (const QString& c : cands) {
            const QString d = freeName(base + "/" + EMPTY + "/" + c);
            if (moveItem(base + "/" + c, d)) {
                report.swept.push_back({ c, relativeTo(base, d) });
            }
        }
    }
    return report;
}

QJsonObject projectsSnapshot(const Paths& paths, const Codes& codes)
{
    QJsonObject o;
    if (paths.projects.isEmpty()) {
        return o;
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
            templates += rel.startsWith("6 Templates/");
        }
        quarantined += rel.startsWith("8 Quarantine");
    }
    std::map<QString, QString> codeByTitle;
    for (const auto& [t, c] : codes.projects) {
        codeByTitle[t] = c;
    }
    std::vector<std::pair<QString, QString> > ordered;   // by group, then name
    for (const auto& [name, rel] : projectTunes(paths)) {
        ordered.emplace_back(name, rel);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        return a.second.section('/', 0, 0) < b.second.section('/', 0, 0);
    });
    QJsonArray tunes;
    for (const auto& [name, rel] : ordered) {
        const QDir d(paths.projects + "/" + rel);
        auto count = [&](const QString& sub) {
            int n = 0;
            QDirIterator i(d.filePath(sub), QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
            while (i.hasNext()) {
                i.next();
                n += i.fileName() != ".DS_Store";
            }
            return n;
        };
        const QStringList ss = d.entryList({ "*.starscore" }, QDir::Files);
        QString code = codeByTitle.count(name) ? codeByTitle[name] : QString();
        if (code.isEmpty() && !ss.isEmpty()) {
            code = QRegularExpression("^([A-Z]{4}) - ").match(ss.first()).captured(1);
        }
        int deprecated = count("Deprecated") + count("MuseScore Files/Deprecated");
        tunes.append(QJsonObject { { "name", name }, { "group", rel.section('/', 0, 0) }, { "code", code }, { "starscore", !ss.isEmpty() },
                                   { "museScoreFiles", count("MuseScore Files") }, { "versionHistory", count("Version History") },
                                   { "deprecated", deprecated } });
    }
    o["files"] = files;
    o["starscore"] = starscore;
    o["mscz"] = mscz;
    o["templates"] = templates;
    o["quarantined"] = quarantined;
    o["tunes"] = tunes;
    return o;
}

// ------------------------------------------------------------------ codes
QStringList syncCodes(const Paths& paths, Codes& codes)
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
    for (const auto& [tune, rel] : projectTunes(paths)) {
        // the code: the .starscore's "CODE - Title.starscore", then the band folder by title
        QString fileCode;
        for (const QString& f : QDir(paths.projects + "/" + rel).entryList({ "*.starscore" }, QDir::Files, QDir::Name)) {
            const QRegularExpressionMatch m = QRegularExpression("^([A-Z]{4}) - ").match(f);
            if (m.hasMatch()) {
                fileCode = m.captured(1);
                break;
            }
        }
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
