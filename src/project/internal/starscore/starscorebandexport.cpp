/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — export a .starscore into the band's "Sheets and Demos" folder,
 * named and filed exactly like the existing sheets there (see 6 Inbox/.organizer/RULES.md):
 *
 *   <N> <Song>/1 Lead Sheet/CODE - Lead Sheet.pdf
 *   <N> <Song>/1 Rhythm/CODE - Bass.pdf, Drums, Guitar, Keys, Congas, ...
 *   <N> <Song>/3H Tpt Alt Ten/CODE - Score.pdf, CODE - Trumpet.pdf, CODE - Alto Sax.pdf, ...
 *   <N> <Song>/3H Any Horns/CODE - Score.pdf, CODE - Horn 1 in C.pdf, ...
 *   <N> <Song>/Big Band, Full Orchestra, Marching Band, Extras
 *
 * A file that would be replaced is first moved to "<Song>/Version History/Superseded <date>/".
 */
#include "starscoreservice.h"

#include <map>
#include <set>

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/interval.h"
#include "engraving/dom/clef.h"
#include "engraving/dom/box.h"
#include "engraving/dom/text.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/select.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/measure.h"

#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationpainting.h"
#include "notation/inotationparts.h"
#include "notation/inotationstyle.h"
#include "notation/inotation.h"

#include "starscoreengraving.h"
#include "starscorehouse.h"
#include "starscorepdf.h"

#include "io/filestream.h"
#include "translation.h"
#include "log.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;

// Order and short codes used in horn folder names ("5H 2Tpt Alt Ten Tbn")
static const std::vector<std::pair<QString, QString> > STARSCORE_HORN_ORDER {
    { "Trumpet", "Tpt" }, { "Flugelhorn", "Flg" }, { "Flute", "Flu" }, { "Clarinet", "Cla" }, { "Soprano Sax", "Sop" },
    { "Alto Sax", "Alt" }, { "Tenor Sax", "Ten" }, { "Bari Sax", "Bar" }, { "Bass Sax", "Bsx" }, { "Bass Clarinet", "Bcl" },
    { "Trombone", "Tbn" }, { "Bass Trombone", "Btb" },
};

//! The band's name for a horn, or empty when the instrument is not a horn
static QString starscoreHornName(const QString& id)
{
    if (id.contains("bass-trombone")) {
        return "Bass Trombone";
    }
    if (id.contains("trombone")) {
        return "Trombone";
    }
    if (id.contains("flugelhorn")) {
        return "Flugelhorn";
    }
    if (id.contains("trumpet") || id.contains("cornet")) {
        return "Trumpet";
    }
    if (id.contains("bass-clarinet")) {
        return "Bass Clarinet";
    }
    if (id.contains("clarinet")) {
        return "Clarinet";
    }
    if (id == "flute" || id == "c-flute" || id == "piccolo") {
        return "Flute";
    }
    if (id.contains("soprano-saxophone")) {
        return "Soprano Sax";
    }
    if (id.contains("alto-saxophone")) {
        return "Alto Sax";
    }
    if (id.contains("tenor-saxophone")) {
        return "Tenor Sax";
    }
    if (id.contains("baritone-saxophone")) {
        return "Bari Sax";
    }
    if (id.contains("bass-saxophone")) {
        return "Bass Sax";
    }
    return QString();
}

//! The horn's name printed on its sheet ("Trumpet 1" -> "Trumpet 1 in B♭", "Alto Sax" -> "Alto Saxophone")
static QString starscoreSheetHornName(const QString& bandName)
{
    static const std::vector<std::pair<QString, QString> > FULL {
        { "Soprano Sax", "Soprano Saxophone" }, { "Alto Sax", "Alto Saxophone" }, { "Tenor Sax", "Tenor Saxophone" },
        { "Bari Sax", "Baritone Saxophone" }, { "Bass Sax", "Bass Saxophone" },
    };
    QString name = bandName;
    for (const auto& [shortName, full] : FULL) {
        if (name.startsWith(shortName)) {
            name = full + name.mid(shortName.size());
            break;
        }
    }
    if (name.startsWith("Trumpet") || name.startsWith("Flugelhorn") || name.startsWith("Clarinet")
        || name.startsWith("Bass Clarinet")) {
        name += QString::fromUtf8(" in B\u266D");
    }
    return name;
}

//! The band's name for a rhythm-section instrument
static QString starscoreRhythmName(const QString& id, const QString& partName)
{
    if (id.contains("bass-synth")) {
        return "Bass Synth";
    }
    if (id.contains("bass-guitar") || id.contains("electric-bass") || id.contains("fretless") || id == "contrabass"
        || id == "double-bass" || id == "acoustic-bass") {
        return "Bass";
    }
    if (id.contains("guitar")) {
        return "Guitar";
    }
    if (id == "electric-piano") {
        return "Elec Piano";
    }
    if (id.contains("organ")) {
        return "Organ";
    }
    if (id == "clavinet") {
        return "Clavinet";
    }
    if (id.contains("piano") || id.contains("keyboard") || id.contains("synth")) {
        return "Keys";
    }
    if (id.contains("drum")) {
        return "Drums";
    }
    if (id == "congas") {
        return "Congas";
    }
    if (id.contains("percussion") || id == "bongos" || id == "timbales" || id == "cajon" || id == "shaker") {
        return "Percussion";
    }
    return partName;
}

static QString starscoreSafeFileName(QString s)
{
    s.replace(QRegularExpression("[/:\\\\]"), "-");
    return s.trimmed();
}

static bool starscoreIsUntitled(const QString& title)
{
    const QString t = title.trimmed().toLower();
    return t.isEmpty() || t == "untitled score" || t == "untitled";
}

//! The text of the Title in the score's title frame
static QString starscoreTitleFrameText(const mu::engraving::MasterScore* ms)
{
    const mu::engraving::MeasureBase* first = ms ? ms->first() : nullptr;
    if (!first || !first->isVBox()) {
        return QString();
    }
    for (mu::engraving::EngravingItem* e : first->el()) {
        if (e && e->isText() && mu::engraving::toText(e)->textStyleType() == mu::engraving::TextStyleType::TITLE) {
            return mu::engraving::toText(e)->plainText().toQString().simplified();
        }
    }
    return QString();
}

//! The song's title: the score's title, or the title frame's when that is empty or "Untitled score",
//! or the file name without its "CODE - " prefix
static QString starscoreSongTitle(const INotationProjectPtr& project)
{
    if (!project) {
        return QString();
    }
    QString title = project->metaInfo().title.trimmed();
    if (starscoreIsUntitled(title) && project->masterNotation()) {
        title = starscoreTitleFrameText(project->masterNotation()->masterScore());
    }
    if (starscoreIsUntitled(title)) {
        const QString fileBase = QFileInfo(project->path().toQString()).completeBaseName();
        const QRegularExpressionMatch m = QRegularExpression("^([A-Z]{3,4})\\s*-\\s*(.*)$").match(fileBase);
        title = m.hasMatch() ? m.captured(2).trimmed() : fileBase;
    }
    return starscoreIsUntitled(title) ? QString() : title;
}

static std::map<QString, QString> starscoreReadCodes(const QString& bandFolder)
{
    std::map<QString, QString> folderToCode;
    QFile f(bandFolder + "/6 Inbox/.organizer/codes.json");
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
        for (auto it = o.begin(); it != o.end(); ++it) {
            folderToCode[it.key()] = it.value().toString();
        }
    }
    return folderToCode;
}

//! A four-letter code for a new song, in the style of the others (AMPL, FYKB, HTLS…), that no song uses yet
static QString starscoreSuggestCode(const QString& title, const std::set<QString>& taken)
{
    QStringList words;
    for (const QString& w : title.normalized(QString::NormalizationForm_D).toUpper().split(QRegularExpression("[^A-Z0-9]+"),
                                                                                             Qt::SkipEmptyParts)) {
        words << w;
    }
    if (words.isEmpty()) {
        words << "SONG";
    }
    const QString letters = words.join("");

    QStringList candidates;
    if (words.size() >= 4) {
        QString c;
        for (int i = 0; i < 4; ++i) {
            c += words[i].at(0);
        }
        candidates << c;
    }
    if (words.size() == 1) {
        candidates << words[0].left(4);
    }
    if (words.size() >= 2) {
        candidates << words[0].left(2) + words[1].left(2);
        candidates << words[0].left(1) + words[1].left(3);
        candidates << words[0].left(3) + words[1].left(1);
        candidates << words.last().left(4);
        candidates << words[0].left(4);
    }
    if (words.size() == 3) {
        candidates << words[0].left(2) + words[1].left(1) + words[2].left(1);
        candidates << words[0].left(1) + words[1].left(1) + words[2].left(2);
    }
    // first letter plus any three later letters, in order
    for (int a = 1; a < letters.size(); ++a) {
        for (int b = a + 1; b < letters.size(); ++b) {
            for (int c = b + 1; c < letters.size(); ++c) {
                candidates << QString(letters.at(0)) + letters.at(a) + letters.at(b) + letters.at(c);
            }
        }
    }
    for (const QString& c : candidates) {
        if (c.size() == 4 && !taken.count(c)) {
            return c;
        }
    }
    const QString stem = (letters + "XXX").left(3);
    for (char ch = 'A'; ch <= 'Z'; ++ch) {
        if (!taken.count(stem + QChar(ch))) {
            return stem + QChar(ch);
        }
    }
    return QString();
}

static QString starscoreJsonString(const QString& s)
{
    // like Python's json.dump (which the organizer uses): ASCII only, other characters as \uXXXX
    QString out = "\"";
    for (const QChar ch : s) {
        const ushort u = ch.unicode();
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += ch;
        } else if (u < 0x20 || u > 0x7e) {
            out += QString("\\u%1").arg(u, 4, 16, QChar('0'));
        } else {
            out += ch;
        }
    }
    return out + "\"";
}

QString StarScoreService::bandFolder() const
{
    const StyleSettings settings = loadStyleSettings();
    if (!settings.bandFolder.isEmpty() && QFileInfo(settings.bandFolder).isDir()) {
        return settings.bandFolder;
    }

    // The usual place on Joel's Mac: ~/Library/CloudStorage/GoogleDrive-*/My Drive/The Starsign Drive/Sheets and Demos
    const QDir cloud(QDir::homePath() + "/Library/CloudStorage");
    for (const QString& drive : cloud.entryList({ "GoogleDrive-*" }, QDir::Dirs)) {
        const QString candidate = cloud.filePath(drive + "/My Drive/The Starsign Drive/Sheets and Demos");
        if (QFileInfo(candidate).isDir()) {
            return candidate;
        }
    }
    return QString();
}

void StarScoreService::setBandFolder(const QString& path)
{
    StyleSettings settings = loadStyleSettings();
    settings.bandFolder = path;
    saveStyleSettings(settings);
}

INotationProjectPtr StarScoreService::exportSourceProject() const
{
    return m_mainProject ? m_mainProject : globalContext()->currentProject();
}

RetVal<StarScoreBandExportPlan> StarScoreService::planBandExport() const
{
    StarScoreBandExportPlan plan;
    INotationProjectPtr project = exportSourceProject();
    if (!project) {
        return RetVal<StarScoreBandExportPlan>::make_ret(Ret::Code::InternalError);
    }

    plan.bandFolder = bandFolder();
    if (plan.bandFolder.isEmpty()) {
        return RetVal<StarScoreBandExportPlan>::make_ret(Ret::Code::UnknownError,
                                                         muse::trc("starscore", "Choose the Sheets and Demos folder first."));
    }

    // --- which song folder? codes.json maps "1 Amplitudes" -> "AMPL"
    const std::map<QString, QString> folderToCode = starscoreReadCodes(plan.bandFolder);

    const QString fileBase = QFileInfo(project->path().toQString()).completeBaseName();
    const QString title = starscoreSongTitle(project);
    QString codeFromName;
    const QRegularExpressionMatch codeMatch = QRegularExpression("^([A-Z]{3,4})\\s*-\\s*(.*)$").match(fileBase);
    if (codeMatch.hasMatch()) {
        codeFromName = codeMatch.captured(1);
    }

    auto plainName = [](const QString& folder) {
        QString n = folder.section('/', -1);
        n.remove(QRegularExpression("^\\d+\\s+"));
        return n.trimmed().toLower();
    };

    for (const auto& [folder, code] : folderToCode) {
        if (!codeFromName.isEmpty() && code == codeFromName) {
            plan.songFolder = folder;
            plan.code = code;
            break;
        }
    }
    if (plan.songFolder.isEmpty()) {
        for (const auto& [folder, code] : folderToCode) {
            if (plainName(folder) == starscoreSafeFileName(title).toLower()) {
                plan.songFolder = folder;
                plan.code = code;
                break;
            }
        }
    }
    if (plan.songFolder.isEmpty()) {
        // A new song: the dialog asks where it goes and what its code is (registerBandSong), then plans again
        std::set<QString> taken;
        for (const auto& [folder, code] : folderToCode) {
            taken.insert(code);
        }
        plan.newSong = true;
        plan.title = title;
        plan.suggestedCode = !codeFromName.isEmpty() && !taken.count(codeFromName) ? codeFromName
                             : starscoreSuggestCode(title, taken);
        return RetVal<StarScoreBandExportPlan>::make_ok(plan);
    }
    plan.title = title;

    // --- one entry per sheet
    engraving::MasterScore* ms = project->masterNotation()->masterScore();
    const Data data = loadFrom(ms);
    const QString code = plan.code;

    auto partById = [ms](const QString& id) {
        return ms->partById(ID(id));
    };
    auto addFile = [&](const QString& folder, const QString& name, const QStringList& parts, bool isScore) {
        plan.files.push_back({ folder + "/" + code + " - " + starscoreSafeFileName(name) + ".pdf", parts, isScore });
    };

    std::map<QString, QStringList> familyParts;   // "Big Band" etc: all instruments, for one score
    QStringList anyFolders;                        // "NH Any Horns" folders: older sheet names there get archived

    for (const StarScoreSection& sec : data.sections) {
        if (sec.partIds.isEmpty()) {
            continue;
        }
        const QString key = sec.templateKey;

        if (key == "lead-sheet") {
            if (sec.partIds.size() == 1) {
                addFile("1 Lead Sheet", "Lead Sheet", sec.partIds, false);
            } else {
                for (const QString& pid : sec.partIds) {
                    if (engraving::Part* p = partById(pid)) {
                        addFile("1 Lead Sheet", "Lead Sheet (" + p->partName().toQString() + ")", { pid }, false);
                    }
                }
            }
            continue;
        }

        if (key == "rhythm") {
            std::map<QString, int> counts;
            for (const QString& pid : sec.partIds) {
                if (engraving::Part* p = partById(pid)) {
                    counts[starscoreRhythmName(p->instrumentId().toQString(), p->partName().toQString())]++;
                }
            }
            for (const QString& pid : sec.partIds) {
                engraving::Part* p = partById(pid);
                if (!p) {
                    continue;
                }
                const QString role = starscoreRhythmName(p->instrumentId().toQString(), p->partName().toQString());
                QString name = role;
                if (counts[name] > 1) {
                    name += " (" + p->partName().toQString() + ")";
                }
                addFile("1 Rhythm", name, { pid }, false);
                // Finished rhythm section with "No Drums / Percussion / Keys Sheet": that player reads the lead
                // sheet, so the sheet starts unticked
                const QStringList& skipSheets = sec.autoStatus ? sec.autoSkipSheets : sec.skipSheets;
                if (sec.status == StarScoreStatus::Finished && !skipSheets.isEmpty()) {
                    const QString id = p->instrumentId().toQString();
                    QString kind;
                    if (id == "drumset" || id == "drum-kit") {
                        kind = "drums";
                    } else if (id == "congas" || id == "bongos" || id == "percussion" || id == "timbales" || id == "cajon"
                               || id.contains("shaker") || id.contains("tambourine") || id.contains("cowbell")) {
                        kind = "percussion";
                    } else if (role != "Guitar" && role != "Bass" && role != "Bass Synth") {
                        kind = "keys";
                    }
                    if (!kind.isEmpty() && skipSheets.contains(kind)) {
                        plan.files.back().defaultUnchecked = true;
                    }
                }
            }
            continue;
        }

        if (key.startsWith("bigband-") || key.startsWith("orch-") || key.startsWith("marching-")) {
            const QString folder = key.startsWith("bigband-") ? "Big Band" : key.startsWith("orch-") ? "Full Orchestra" : "Marching Band";
            for (const QString& pid : sec.partIds) {
                if (engraving::Part* p = partById(pid)) {
                    addFile(folder, p->partName().toQString(), { pid }, false);
                    familyParts[folder] << pid;
                }
            }
            continue;
        }

        static const QRegularExpression anyRe("^(\\d+)-horn-any$");
        if (anyRe.match(key).hasMatch()) {
            // Chairs: "Horn N" staves (concert pitch); a staff with "Flute" in its name is Horn 1's flute variation
            static const QRegularExpression chairRe("Horn\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
            std::vector<std::pair<QString, int> > chairs;   // (partId, chair number)
            QString flutePid;
            for (const QString& pid : sec.partIds) {
                engraving::Part* p = partById(pid);
                if (!p) {
                    continue;
                }
                const QString name = p->partName().toQString();
                if (name.contains("flute", Qt::CaseInsensitive)) {
                    flutePid = pid;
                    continue;
                }
                const QRegularExpressionMatch cm = chairRe.match(name);
                chairs.emplace_back(pid, cm.hasMatch() ? cm.captured(1).toInt() : int(chairs.size()) + 1);
            }
            if (chairs.empty()) {
                continue;
            }
            const int horns = int(chairs.size());
            const QString folder = QString("%1H Any Horns").arg(horns);
            const QString arr = QString("%1-Horn Arr: ").arg(horns);

            QStringList scoreParts;
            for (const auto& c : chairs) {
                scoreParts << c.first;
            }
            addFile(folder, "Score", scoreParts, true);

            // Each chair as a sheet for every instrument that can sit in it (Starsign Band Guide, page 3)
            struct Seat {
                const char* file;    // file name part, e.g. "Trumpet in Bb"
                const char* sheet;   // printed name
                int dia;
                int chrom;
                int clef;            // 0 treble, 1 bass, 2 alto
            };
            static const Seat SOP { "Soprano Sax", "Soprano Saxophone", -1, -2, 0 };
            static const Seat CLA { "Clarinet in Bb", "Clarinet in B\u266D", -1, -2, 0 };
            static const Seat TPT { "Trumpet in Bb", "Trumpet in B\u266D", -1, -2, 0 };
            static const Seat ALT { "Alto Sax", "Alto Saxophone", -5, -9, 0 };
            static const Seat VLN { "Violin", "Violin", 0, 0, 0 };
            static const Seat TEN { "Tenor Sax", "Tenor Saxophone", -8, -14, 0 };
            static const Seat VLA { "Viola", "Viola", 0, 0, 2 };
            static const Seat BAR { "Bari Sax", "Baritone Saxophone", -12, -21, 0 };
            static const Seat TBN { "Trombone", "Trombone", 0, 0, 1 };
            static const Seat BCL { "Bass Clarinet in Bb", "Bass Clarinet in B\u266D", -8, -14, 0 };
            static const Seat VC { "Cello", "Cello", 0, 0, 1 };
            const std::vector<Seat> HIGH2 { SOP, CLA, TPT, ALT, VLN };
            const std::vector<Seat> HIGH3 { CLA, SOP, TPT, ALT, VLN };
            const std::vector<Seat> MIDDLE { TPT, CLA, ALT, TEN, VLA };
            const std::vector<Seat> LOW { TEN, BAR, TBN, BCL, VC };
            const QString right = QString("Flexible %1-Horn Arrangement").arg(horns);

            auto addSeat = [&](const QString& pid, int number, const Seat& seat) {
                StarScoreBandFile f;
                const QString name = QString("Horn %1 - %2").arg(number).arg(QString::fromUtf8(seat.file));
                f.relativePath = folder + "/" + code + " - " + starscoreSafeFileName(name) + ".pdf";
                f.partIds = { pid };
                f.isVersion = true;
                f.transposeDiatonic = seat.dia;
                f.transposeChromatic = seat.chrom;
                f.clef = seat.clef;
                f.header = arr + name;
                f.sheetLeft = QString::fromUtf8(seat.sheet);
                f.sheetRight = right;
                plan.files.push_back(f);
            };

            for (const auto& [pid, number] : chairs) {
                const std::vector<Seat>& seats = (number == 1 && horns > 1) ? (horns >= 3 ? HIGH3 : HIGH2)
                                                 : (number >= horns && horns > 1) ? LOW : MIDDLE;
                for (const Seat& seat : seats) {
                    addSeat(pid, number, seat);
                }
                if (number == 1 && !flutePid.isEmpty()) {
                    addSeat(flutePid, 1, Seat { "Flute", "Flute", 0, 0, 0 });
                }
            }
            anyFolders << folder;
            continue;
        }

        // 1-Horn: one melody sheet per horn, each played alone with the rhythm section (no score)
        if (sec.templateKey == "1-horn") {
            for (const QString& pid : sec.partIds) {
                engraving::Part* p = partById(pid);
                const QString horn = p ? starscoreHornName(p->instrumentId().toQString()) : QString();
                const QString name = horn.isEmpty() && p ? p->partName().toQString() : horn;
                if (name.isEmpty()) {
                    continue;
                }
                addFile("1H", name, { pid }, false);
                plan.files.back().sheetLeft = starscoreSheetHornName(name);
                plan.files.back().sheetRight = QString("1-Horn Arrangement");
            }
            continue;
        }

        // Horn sections (template or custom): named from their instruments
        bool allHorns = true;
        std::vector<std::pair<QString, QString> > horns;   // (partId, horn name)
        for (const QString& pid : sec.partIds) {
            engraving::Part* p = partById(pid);
            const QString horn = p ? starscoreHornName(p->instrumentId().toQString()) : QString();
            if (horn.isEmpty()) {
                allHorns = false;
                break;
            }
            horns.emplace_back(pid, horn);
        }

        if (allHorns && !horns.empty()) {
            std::map<QString, int> counts;
            int players = 0;   // stand-in versions (7-Horn Baritone / Bass Saxophone) aren't extra players
            std::map<QString, int> playerCounts;
            for (const auto& h : horns) {
                counts[h.second]++;
                if (!sec.alternates.count(h.first)) {
                    ++players;
                    playerCounts[h.second]++;
                }
            }
            QStringList codes;
            for (const auto& [name, abbr] : STARSCORE_HORN_ORDER) {
                if (playerCounts.count(name)) {
                    codes << (playerCounts[name] > 1 ? QString::number(playerCounts[name]) : QString()) + abbr;
                }
            }
            const QString folder = QString("%1H %2").arg(players).arg(codes.join(' '));
            QStringList scoreParts;
            for (const QString& pid : sec.partIds) {
                if (!sec.alternates.count(pid)) {
                    scoreParts << pid;
                }
            }
            addFile(folder, "Score", scoreParts, true);

            std::map<QString, int> seen;
            for (const auto& [pid, horn] : horns) {
                QString name = horn;
                if (counts[horn] > 1) {
                    name += QString(" %1").arg(++seen[horn]);
                }
                addFile(folder, name, { pid }, false);
                plan.files.back().sheetLeft = starscoreSheetHornName(name);
                plan.files.back().sheetRight = QString("%1-Horn Arrangement").arg(players);
            }
            continue;
        }

        // Anything else goes to Extras, one sheet per instrument
        for (const QString& pid : sec.partIds) {
            if (engraving::Part* p = partById(pid)) {
                addFile("Extras", p->partName().toQString(), { pid }, false);
            }
        }
    }

    for (const auto& [folder, parts] : familyParts) {
        addFile(folder, "Score", parts, true);
    }

    if (!solos().empty()) {
        plan.notes << muse::qtrc("starscore", "Solo transcriptions are not exported (Sheets and Demos has no place for them yet).");
    }

    // Reference PDFs go as they are into "Reference PDFs/"
    for (const StarScoreReference& ref : references()) {
        StarScoreBandFile f;
        f.relativePath = "Reference PDFs/" + starscoreSafeFileName(ref.name) + ".pdf";
        f.sourceFile = referencePath(ref.id).toQString();
        plan.files.push_back(f);
    }

    // A path should appear once only
    std::set<QString> seenPaths;
    std::vector<StarScoreBandFile> unique;
    for (const StarScoreBandFile& f : plan.files) {
        if (seenPaths.insert(f.relativePath).second) {
            unique.push_back(f);
        } else {
            plan.notes << muse::qtrc("starscore", "Two sheets would both be called %1; only the first is exported.").arg(f.relativePath);
        }
    }
    plan.files = unique;
    plan.anyHornFolders = anyFolders;

    return RetVal<StarScoreBandExportPlan>::make_ok(plan);
}

//! The sheet's title frame: the horn's name top left (the part name text), the arrangement top right
static void starscoreRetitleSheet(mu::engraving::Score* score, const QString& left, const QString& right)
{
    if (!score) {
        return;
    }
    mu::engraving::Box* box = nullptr;
    for (mu::engraving::MeasureBase* mb = score->first(); mb; mb = mb->next()) {
        if (mb->isVBox()) {
            box = mu::engraving::toBox(mb);
            break;
        }
        if (mb->isMeasure()) {
            break;
        }
    }
    if (!box) {
        return;
    }
    auto escape = [](const QString& t) {
        return muse::String::fromQString(t.toHtmlEscaped());
    };

    mu::engraving::Text* partText = nullptr;
    for (mu::engraving::EngravingItem* e : box->el()) {
        if (e && e->isText() && mu::engraving::toText(e)->textStyleType() == mu::engraving::TextStyleType::INSTRUMENT_EXCERPT) {
            partText = mu::engraving::toText(e);
            break;
        }
    }
    if (!left.isEmpty()) {
        if (partText) {
            partText->undoChangeProperty(mu::engraving::Pid::TEXT, escape(left));
        } else {
            mu::engraving::Text* t = mu::engraving::Factory::createText(box, mu::engraving::TextStyleType::INSTRUMENT_EXCERPT);
            t->setParent(box);
            t->setTrack(0);
            t->setXmlText(escape(left));
            score->undoAddElement(t);
            partText = t;
        }
    }
    if (!right.isEmpty()) {
        mu::engraving::Text* t = mu::engraving::Factory::createText(box, mu::engraving::TextStyleType::INSTRUMENT_EXCERPT);
        t->setParent(box);
        t->setTrack(0);
        t->setXmlText(escape(right));
        t->setAlign(mu::engraving::Align(mu::engraving::AlignH::RIGHT, mu::engraving::AlignV::TOP));
        t->setPropertyFlags(mu::engraving::Pid::ALIGN, mu::engraving::PropertyFlags::UNSTYLED);
        score->undoAddElement(t);
    }
    score->setLayoutAll();
    score->doLayout();
}

Ret StarScoreService::writePdf(const INotationPtr& notation, const QString& path) const
{
    INotationWriterPtr writer = writers()->writer("pdf");
    if (!writer || !notation) {
        return make_ret(Ret::Code::InternalError);
    }

    engraving::Score* score = notation->elements()->msScore();
    if (score && !score->autoLayoutEnabled()) {
        score->doLayout();
    }

    const ViewMode oldMode = notation->painting()->viewMode();
    notation->painting()->setViewMode(ViewMode::PAGE);

    io::FileStream out { io::path_t(path) };
    Ret ret = make_ret(Ret::Code::UnknownError);
    if (out.open(io::IODevice::WriteOnly)) {
        INotationWriter::Options options { { INotationWriter::OptionKey::UNIT_TYPE, Val(INotationWriter::UnitType::PER_PART) } };
        ret = writer->write(notation, out, options);
        out.close();
    }

    notation->painting()->setViewMode(oldMode);
    return ret;
}

QStringList StarScoreService::bandExportUnticked(const QString& code) const
{
    QStringList paths;
    for (const QJsonValue& v : loadStyleSettings().exportUnticked.value(code).toArray()) {
        paths << v.toString();
    }
    return paths;
}

void StarScoreService::setBandExportUnticked(const QString& code, const QStringList& paths)
{
    if (code.isEmpty()) {
        return;
    }
    StyleSettings settings = loadStyleSettings();
    settings.exportUnticked[code] = QJsonArray::fromStringList(paths);
    saveStyleSettings(settings);
}

RetVal<QString> StarScoreService::exportToBandFolder(const QStringList& onlyPaths)
{
    RetVal<StarScoreBandExportPlan> plan = planBandExport();
    if (!plan.ret) {
        return RetVal<QString>::make_ret(plan.ret);
    }
    if (!onlyPaths.isEmpty()) {
        std::vector<StarScoreBandFile> chosen;
        for (const StarScoreBandFile& f : plan.val.files) {
            if (onlyPaths.contains(f.relativePath)) {
                chosen.push_back(f);
            }
        }
        plan.val.files = chosen;
    }

    INotationProjectPtr project = exportSourceProject();
    IMasterNotationPtr master = project->masterNotation();
    engraving::MasterScore* ms = master->masterScore();

    const QString songDir = plan.val.bandFolder + "/" + plan.val.songFolder;
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const QString tmpDir = QDir::tempPath() + "/StarScoreExport-" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(tmpDir);

    // Part books by instrument: prefer a single-instrument part book, named like the part
    ExcerptNotationList excerpts = master->excerpts();
    ExcerptNotationList potential = master->potentialExcerpts();
    std::map<QString, IExcerptNotationPtr> bookForPart;
    for (const IExcerptNotationPtr& e : excerpts) {
        INotationPtr n = e->notation();
        engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
        if (!es || es->parts().size() != 1) {
            continue;
        }
        for (const engraving::Staff* staff : es->parts().front()->staves()) {
            if (const engraving::Staff* linked = staff->findLinkedInScore(ms)) {
                const QString pid = idText(linked->part());
                if (!bookForPart.count(pid) || e->name() == linked->part()->partName().toQString()) {
                    bookForPart[pid] = e;
                }
                break;
            }
        }
    }

    // Scores of a few instruments come from a scratch copy with only those instruments showing
    INotationProjectPtr scratch;
    const QString copyPath = tmpDir + "/copy.mscz";
    bool copySaved = false;
    auto loadCopy = [&]() -> INotationProjectPtr {
        if (!copySaved) {
            if (!project->save(io::path_t(copyPath), SaveMode::SaveCopy, false)) {
                return nullptr;
            }
            copySaved = true;
        }
        INotationProjectPtr p = projectCreator()->newProject(iocContext());
        if (!p->load(io::path_t(copyPath))) {
            return nullptr;
        }
        return p;
    };
    auto scratchProject = [&]() -> INotationProjectPtr {
        if (!scratch) {
            scratch = loadCopy();
        }
        return scratch;
    };

    // An "Any Horns" chair re-written for one transposition and clef, as a part book of its own
    auto writeVersion = [&](const StarScoreBandFile& file, const QString& pdfPath) -> Ret {
        INotationProjectPtr p = loadCopy();
        if (!p) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't make a copy of the score"));
        }
        IMasterNotationPtr vm = p->masterNotation();
        engraving::MasterScore* vs = vm->masterScore();
        engraving::Part* part = vs->partById(ID(file.partIds.value(0)));
        if (!part || !part->instrument()) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "instrument not found"));
        }

        vm->setExcerpts({});
        if (!part->show()) {
            vm->parts()->setPartsVisible({ { part->id(), true } }, TranslatableString::untranslatable("Show"));
        }

        engraving::Instrument instrument = *part->instrument();
        instrument.setTranspose(engraving::Interval(file.transposeDiatonic, file.transposeChromatic));
        const engraving::ClefType clef = file.clef == 1 ? engraving::ClefType::F
                                         : file.clef == 2 ? engraving::ClefType::C3 : engraving::ClefType::G;
        instrument.setClefType(0, engraving::ClefTypeList(clef, clef));
        const InstrumentKey key { part->instrumentId(), part->id(), engraving::Fraction(0, 1) };
        vm->parts()->replaceInstrument(key, instrument);
        vm->parts()->setInstrumentName(InstrumentKey { part->instrumentId(), part->id(), engraving::Fraction(0, 1) }, file.header);
        part->setPartName(String::fromQString(file.header));

        IExcerptNotationPtr book;
        for (const IExcerptNotationPtr& e : vm->potentialExcerpts()) {
            if (e->name() == file.header) {
                book = e;
                break;
            }
        }
        if (!book) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't make the part"));
        }
        vm->initExcerpts({ book });
        INotationPtr n = book->notation();
        if (!n) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't make the part"));
        }

        // Same look and line breaks as the chair's own part book
        auto src = bookForPart.find(file.partIds.value(0));
        if (src != bookForPart.end() && src->second->notation()) {
            const QString mss = tmpDir + "/chair.mss";
            if (src->second->notation()->style()->saveStyle(io::path_t(mss))) {
                n->style()->loadStyle(io::path_t(mss), true);
            }
            engraving::Score* es = n->elements()->msScore();
            const engraving::Score* srcScore = src->second->notation()->elements()->msScore();
            if (es && srcScore) {
                n->undoStack()->prepareChanges(TranslatableString::untranslatable("Copy layout"));
                starscore::copyLayout(srcScore, { es }, starscore::LayoutCopyOptions());
                n->undoStack()->commitChanges();
            }
        } else {
            const QString def = defaultStylePath();
            if (!def.isEmpty() && QFileInfo::exists(def)) {
                n->style()->loadStyle(io::path_t(def), true);
            }
        }

        // Written pitch, not concert pitch
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Concert pitch"));
        n->style()->setStyleValue(StyleId::concertPitch, false);
        n->undoStack()->commitChanges();
        if (!file.sheetLeft.isEmpty() || !file.sheetRight.isEmpty()) {
            n->undoStack()->prepareChanges(TranslatableString::untranslatable("Sheet title"));
            starscoreRetitleSheet(n->elements()->msScore(), file.sheetLeft, file.sheetRight);
            n->undoStack()->commitChanges();
        }

        return writePdf(n, pdfPath);
    };

    auto supersede = [&](const QString& rel) {
        const QString target = songDir + "/" + rel;
        if (!QFileInfo::exists(target)) {
            return;
        }
        QString archived = songDir + "/Version History/Superseded " + today + "/" + rel;
        QDir().mkpath(QFileInfo(archived).absolutePath());
        const QString base = archived.left(archived.length() - 4);
        for (int i = 2; QFileInfo::exists(archived); ++i) {
            archived = QString("%1 (%2).pdf").arg(base).arg(i);
        }
        QFile::rename(target, archived);
    };

    QStringList written;
    QStringList problems = plan.val.notes;

    for (const StarScoreBandFile& file : plan.val.files) {
        const QString tmpPdf = tmpDir + "/" + QString::number(written.size() + problems.size()) + ".pdf";
        Ret ret;

        if (!file.sourceFile.isEmpty()) {
            ret = QFile::copy(file.sourceFile, tmpPdf) ? make_ok()
                  : make_ret(Ret::Code::UnknownError, muse::trc("starscore", "the reference PDF is missing; save and reopen the score"));
        } else if (file.isVersion) {
            ret = writeVersion(file, tmpPdf);
        } else if (!file.isScore) {
            auto it = bookForPart.find(file.partIds.value(0));
            if (it == bookForPart.end()) {
                // no part book yet: use the potential one MuseScore would make
                for (const IExcerptNotationPtr& e : potential) {
                    if (e->name() == ms->partById(ID(file.partIds.value(0)))->partName().toQString()) {
                        master->initExcerpts({ e });
                        it = bookForPart.emplace(file.partIds.value(0), e).first;
                        break;
                    }
                }
            }
            if (it == bookForPart.end()) {
                problems << muse::qtrc("starscore", "%1: no part book for this instrument.").arg(file.relativePath);
                continue;
            }
            INotationPtr bookNotation = it->second->notation();
            const bool retitle = bookNotation && (!file.sheetLeft.isEmpty() || !file.sheetRight.isEmpty());
            if (retitle) {
                // Only for printing: undone right after, so the part score itself doesn't change
                bookNotation->undoStack()->prepareChanges(TranslatableString::untranslatable("Sheet title"));
                starscoreRetitleSheet(bookNotation->elements()->msScore(), file.sheetLeft, file.sheetRight);
            }
            ret = writePdf(bookNotation, tmpPdf);
            if (retitle) {
                bookNotation->undoStack()->rollbackChanges();
                if (engraving::Score* bs = bookNotation->elements()->msScore()) {
                    bs->setLayoutAll();
                    bs->doLayout();
                }
                bookNotation->notationChanged().notify();
            }
        } else {
            INotationProjectPtr p = scratchProject();
            if (!p) {
                problems << muse::qtrc("starscore", "%1: couldn't make the score copy.").arg(file.relativePath);
                continue;
            }
            engraving::MasterScore* cs = p->masterNotation()->masterScore();
            std::vector<std::pair<muse::ID, bool> > vis;
            for (const engraving::Part* part : cs->parts()) {
                vis.emplace_back(part->id(), file.partIds.contains(idText(part)));
            }
            p->masterNotation()->parts()->setPartsVisible(vis, TranslatableString::untranslatable("Export"));
            ret = writePdf(p->masterNotation()->notation(), tmpPdf);
        }

        if (!ret) {
            problems << muse::qtrc("starscore", "%1: %2").arg(file.relativePath).arg(QString::fromStdString(ret.toString()));
            continue;
        }

        supersede(file.relativePath);
        const QString target = songDir + "/" + file.relativePath;
        QDir().mkpath(QFileInfo(target).absolutePath());
        QFile::remove(target);
        if (!QFile::copy(tmpPdf, target)) {
            problems << muse::qtrc("starscore", "%1: couldn't write the file.").arg(file.relativePath);
            continue;
        }
        written << file.relativePath;
    }

    // "NH Any Horns": sheets under older names (e.g. "Horn 1 in Bb" before each instrument got its own sheet)
    // are archived, as replaced sheets are
    {
        QStringList current;
        const RetVal<StarScoreBandExportPlan> full = planBandExport();
        for (const StarScoreBandFile& f : (full.ret ? full.val.files : plan.val.files)) {
            current << f.relativePath;
        }
        for (const QString& folder : plan.val.anyHornFolders) {
            const QDir dir(songDir + "/" + folder);
            for (const QString& fileName : dir.entryList({ plan.val.code + " - Horn *.pdf" }, QDir::Files)) {
                const QString rel = folder + "/" + fileName;
                if (!current.contains(rel)) {
                    supersede(rel);
                }
            }
        }
    }

    QDir(tmpDir).removeRecursively();

    QString summary = muse::qtrc("starscore", "Wrote %1 PDF(s) to %2.").arg(written.size()).arg(plan.val.songFolder);
    if (!problems.isEmpty()) {
        summary += "\n\n" + muse::qtrc("starscore", "Skipped:") + "\n• " + problems.join("\n• ");
    }
    return RetVal<QString>::make_ok(summary);
}

// ---------------------------------------------------------------------------
//  Version number: "Version x.y.z" in the copyright text
// ---------------------------------------------------------------------------

//! Songs in "1 Starsign Originals" and "2 Starsign Covers" on 27 Sep 2026: they start at 4.0.0.
//! Any other song (new ones) starts at 1.0.0.
static const std::set<QString> STARSCORE_V4_CODES {
    "AMPL", "BRAN", "CIAO", "CUMU", "DEEP", "DEIM", "DBLE", "MUSH", "EVPR", "FEBR", "FYKB", "FISH", "GIJO", "HTLS",
    "IPDW", "INTN", "PINT", "MXTR", "OKAN", "PORC", "ROYL", "SEAG", "SHTR", "TYFT", "COUR", "UPDG",
    "ALWT", "ANRC", "BALK", "BRKO", "CHAM", "ISPY", "LOUI", "LVST", "PEAS", "PUTP", "SHOF", "SNTY", "CHKN", "TWOO", "UFTS",
};
static const std::set<QString> STARSCORE_V4_TITLES {
    "amplitudes", "branston pickle", "ciao ferrari", "cumulonimbus", "deep speech", "deimos", "double entendre",
    "dream of mushroom", "everpresent", "february", "feed your kid bugs", "feed your kids bugs", "fish oil", "g.i. jorge",
    "hit list", "industrial park driveway", "intern", "last pint", "mxter shirts", "okane", "porcupine", "royal", "seagrass",
    "shatter", "thank you for your time", "the courier", "updog",
    "always there", "anarchy rainbow", "another star", "balkan wedding", "bet", "break out", "chameleon", "i'm a spy",
    "little louie", "live strong + strasbourg", "move on up", "peasant funk", "pick up the pieces", "playground", "semente",
    "shofukan", "standing next to you", "the chicken", "the essential", "two", "up from the south", "watermelon man",
};

QString StarScoreService::scoreVersion() const
{
    INotationProjectPtr project = exportSourceProject();
    if (!project) {
        return QString("1.0.0");
    }
    engraving::MasterScore* ms = project->masterNotation()->masterScore();
    const Data data = loadFrom(ms);
    if (!data.version.isEmpty()) {
        return data.version;
    }
    // Earlier builds put it in the copyright text
    const QString fromCopyright = starscore::versionFromCopyright(ms->metaTag(u"copyright").toQString());
    if (!fromCopyright.isEmpty()) {
        return fromCopyright;
    }

    const QString fileBase = QFileInfo(project->path().toQString()).completeBaseName();
    const QRegularExpressionMatch m = QRegularExpression("^([A-Z]{3,4})\\s*-\\s*(.*)$").match(fileBase);
    const QString code = m.hasMatch() ? m.captured(1) : QString();
    const QString title = starscoreSongTitle(project);
    const bool existingSong = STARSCORE_V4_CODES.count(code) || STARSCORE_V4_TITLES.count(title.toLower());
    return existingSong ? QString("4.0.0") : QString("1.0.0");
}

void StarScoreService::setScoreVersion(const QString& version)
{
    INotationProjectPtr project = exportSourceProject();
    if (!project || version.isEmpty()) {
        return;
    }
    IMasterNotationPtr master = project->masterNotation();
    engraving::MasterScore* ms = master->masterScore();

    Data data = loadFrom(ms);
    data.version = version;
    storeTo(ms, data, project);

    auto update = [&](INotationPtr n) {
        if (!n) {
            return;
        }
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Version"));
        starscore::applyVersionFooter(n->elements()->msScore(), version);
        n->undoStack()->commitChanges();
        n->notationChanged().notify();
    };

    update(master->notation());
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        if (e->isInited()) {
            update(e->notation());
        }
    }
    project->markAsUnsaved();
}

// ---------------------------------------------------------------------------
//  A new song in Sheets and Demos
// ---------------------------------------------------------------------------

Ret StarScoreService::registerBandSong(const QString& titleIn, int category, const QString& codeIn)
{
    const QString band = bandFolder();
    if (band.isEmpty()) {
        return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "Choose the Sheets and Demos folder first."));
    }
    const QString title = starscoreSafeFileName(titleIn.simplified());
    const QString code = codeIn.trimmed().toUpper();
    if (starscoreIsUntitled(title)) {
        return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "Type the song's title."));
    }
    if (!QRegularExpression("^[A-Z]{4}$").match(code).hasMatch()) {
        return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "The code must be four capital letters, like AMPL."));
    }
    if (category < 1 || category > 4) {
        return make_ret(Ret::Code::UnknownError);
    }

    const QString organizer = band + "/6 Inbox/.organizer";
    const QString codesPath = organizer + "/codes.json";
    if (!QFileInfo::exists(codesPath)) {
        return make_ret(Ret::Code::UnknownError,
                        muse::qtrc("starscore", "Couldn't find %1. Is the Sheets and Demos folder right?").arg(codesPath).toStdString());
    }
    std::map<QString, QString> codes = starscoreReadCodes(band);

    const QString folder = category == 4 ? "4 Works In Progress/" + title : QString("%1 %2").arg(category).arg(title);
    for (const auto& [f, c] : codes) {
        if (c == code && f != folder) {
            return make_ret(Ret::Code::UnknownError,
                            muse::qtrc("starscore", "%1 is already the code for “%2”. Choose another code.").arg(code, f).toStdString());
        }
        QString plain = f.section('/', -1);
        plain.remove(QRegularExpression("^\\d+\\s+"));
        if (plain.trimmed().toLower() == title.toLower() && f != folder) {
            return make_ret(Ret::Code::UnknownError,
                            muse::qtrc("starscore", "“%1” is already in Sheets and Demos as “%2”.").arg(title, f).toStdString());
        }
    }

    // The song folder, with the folders every song has before its first export
    const QString songDir = band + "/" + folder;
    if (!QDir().mkpath(songDir) || !QDir().mkpath(songDir + "/Demos") || !QDir().mkpath(songDir + "/Version History")) {
        return make_ret(Ret::Code::UnknownError,
                        muse::qtrc("starscore", "Couldn't make the folder %1.").arg(songDir).toStdString());
    }

    // codes.json, written the way the organizer writes it (sorted, one-space indent)
    codes[folder] = code;
    QString json = "{\n";
    int i = 0;
    for (const auto& [f, c] : codes) {
        json += " " + starscoreJsonString(f) + ": " + starscoreJsonString(c);
        json += (++i < int(codes.size())) ? ",\n" : "\n";
    }
    json += "}";
    QFile out(codesPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return make_ret(Ret::Code::UnknownError,
                        muse::qtrc("starscore", "Couldn't write %1.").arg(codesPath).toStdString());
    }
    out.write(json.toUtf8());
    out.close();

    // Give the score its title so the sheets and later exports use it
    INotationProjectPtr project = exportSourceProject();
    if (project && starscoreIsUntitled(project->metaInfo().title)) {
        ProjectMeta meta = project->metaInfo();
        meta.title = titleIn.simplified();
        project->setMetaInfo(meta);
    }
    return make_ok();
}

// ---------------------------------------------------------------------------
//  Every arrangement as its own .mscz
// ---------------------------------------------------------------------------

RetVal<QString> StarScoreService::exportArrangementsAsMscz(const QString& folder)
{
    INotationProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return RetVal<QString>::make_ret(Ret::Code::InternalError);
    }
    const std::vector<StarScoreArrangement> list = arrangements();
    if (list.empty()) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "This score has no arrangements."));
    }

    // "AMPL - 3-Horn Standard.mscz": the song's code when the file name or Sheets and Demos has one, else its title
    QString prefix;
    const QString fileBase = QFileInfo(project->path().toQString()).completeBaseName();
    const QRegularExpressionMatch m = QRegularExpression("^([A-Z]{3,4})\\s*-\\s*(.*)$").match(fileBase);
    if (m.hasMatch()) {
        prefix = m.captured(1);
    } else {
        const QString band = bandFolder();
        const QString title = starscoreSongTitle(project);
        if (!band.isEmpty() && !title.isEmpty()) {
            for (const auto& [f, c] : starscoreReadCodes(band)) {
                QString plain = f.section('/', -1);
                plain.remove(QRegularExpression("^\\d+\\s+"));
                if (plain.trimmed().toLower() == starscoreSafeFileName(title).toLower()) {
                    prefix = c;
                    break;
                }
            }
        }
        if (prefix.isEmpty()) {
            prefix = !title.isEmpty() ? starscoreSafeFileName(title) : fileBase;
        }
    }

    QDir().mkpath(folder);
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    QStringList written;
    QStringList failed;
    QStringList superseded;
    for (const StarScoreArrangement& a : list) {
        const QString name = prefix + " - " + starscoreSafeFileName(a.name) + ".mscz";
        const QString target = folder + "/" + name;

        // An older copy moves to Version History/Superseded <today>/ instead of being overwritten
        if (QFileInfo::exists(target)) {
            QString archived = folder + "/Version History/Superseded " + today + "/" + name;
            QDir().mkpath(QFileInfo(archived).absolutePath());
            const QString base = archived.left(archived.length() - 5);
            for (int n = 2; QFileInfo::exists(archived); ++n) {
                archived = QString("%1 (%2).mscz").arg(base).arg(n);
            }
            if (QFile::rename(target, archived)) {
                superseded << name;
            }
        }

        const Ret ret = exportArrangement(a.id, io::path_t(target));
        if (ret) {
            written << name;
        } else {
            failed << QString("%1 (%2)").arg(name, QString::fromStdString(ret.toString()));
        }
    }

    QString summary = muse::qtrc("starscore", "Wrote %1 MuseScore file(s) to %2:").arg(written.size()).arg(folder);
    for (const QString& w : written) {
        summary += "\n  • " + w;
    }
    if (!superseded.isEmpty()) {
        summary += "\n\n" + muse::qtrc("starscore", "Older copies of %1 file(s) moved to Version History/Superseded %2.")
                   .arg(superseded.size()).arg(today);
    }
    if (!failed.isEmpty()) {
        summary += "\n\n" + muse::qtrc("starscore", "Couldn't export:") + "\n  • " + failed.join("\n  • ");
    }
    return RetVal<QString>::make_ok(summary);
}

// ---------------------------------------------------------------------------
//  Songbooks: sheets of one song, rendered from a copy of its file
// ---------------------------------------------------------------------------

//! The rhythm-section role of an instrument: "keys", "guitar", "bass", "drums", "percussion"
static QString songbookRhythmRole(const QString& id)
{
    if (id == "drumset" || id == "drum-kit" || id.startsWith("drum")) {
        return "drums";
    }
    if (id == "congas" || id == "bongos" || id == "percussion" || id == "timbales" || id == "cajon" || id.contains("shaker")
        || id.contains("tambourine") || id.contains("cowbell")) {
        return "percussion";
    }
    if (id.contains("bass") || id == "contrabass") {
        return "bass";
    }
    if (id.contains("guitar")) {
        return "guitar";
    }
    return "keys";
}

void StarScoreService::songbookRenderSheets(const QString& songPath, std::vector<StarScoreSongbookSheet>& sheets)
{
    const QString tmpDir = QDir::tempPath() + "/StarScoreSongbook-" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(tmpDir);
    const QString copyPath = tmpDir + "/song.mscz";
    auto failAll = [&](const QString& why) {
        for (StarScoreSongbookSheet& s : sheets) {
            s.error = why;
        }
        QDir(tmpDir).removeRecursively();
    };
    if (!QFile::copy(songPath, copyPath)) {
        failAll(muse::qtrc("starscore", "couldn't read the file"));
        return;
    }
    auto load = [&]() -> INotationProjectPtr {
        INotationProjectPtr p = projectCreator()->newProject(iocContext());
        if (!p->load(io::path_t(copyPath))) {
            return nullptr;
        }
        return p;
    };

    // One copy stays as it is: part books to take style and layout from, the song's sections, and plain parts
    INotationProjectPtr base = load();
    if (!base || !base->masterNotation() || !base->masterNotation()->masterScore()) {
        failAll(muse::qtrc("starscore", "couldn't open the file"));
        return;
    }
    IMasterNotationPtr baseMaster = base->masterNotation();
    engraving::MasterScore* bms = baseMaster->masterScore();
    const Data data = loadFrom(bms);

    auto bookFor = [](IMasterNotationPtr master, const engraving::Part* part) -> INotationPtr {
        engraving::MasterScore* ms = master->masterScore();
        INotationPtr best;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            INotationPtr n = e ? e->notation() : nullptr;
            engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
            if (!es || es->parts().size() != 1) {
                continue;
            }
            for (engraving::Staff* staff : es->parts().front()->staves()) {
                if (engraving::Staff* linked = staff->findLinkedInScore(ms)) {
                    if (linked->part() == part && (!best || e->name() == part->partName().toQString())) {
                        best = n;
                    }
                    break;
                }
            }
        }
        return best;
    };

    // Which part a sheet is
    auto resolvePart = [&](const StarScoreSongbookSheet& sheet) -> QString {
        if (sheet.kind == "part") {
            return sheet.partId;
        }
        for (const StarScoreSection& sec : data.sections) {
            if (sheet.kind == "solo" && sec.templateKey == "1-horn") {
                for (const QString& pid : sec.partIds) {
                    const engraving::Part* p = bms->partById(ID(pid));
                    if (p && p->instrumentId().toQString() == sheet.role) {
                        return pid;
                    }
                }
            }
            if (sheet.kind == "lead" && sec.templateKey == "lead-sheet" && !sec.partIds.isEmpty()) {
                return sec.partIds.first();
            }
            if (sheet.kind == "rhythm" && (sec.templateKey == "rhythm" || sec.templateKey.endsWith("-rhythm"))) {
                for (const QString& pid : sec.partIds) {
                    const engraving::Part* p = bms->partById(ID(pid));
                    if (p && songbookRhythmRole(p->instrumentId().toQString()) == sheet.role
                        && (sec.shownPartIds.isEmpty() || sec.shownPartIds.contains(pid))) {
                        return pid;
                    }
                }
            }
            if (sheet.kind == "chair" && sec.templateKey == sheet.sectionKey) {
                static const QRegularExpression chairRe("Horn\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
                int order = 0;
                for (const engraving::Part* p : bms->parts()) {
                    const QString pid = idText(p);
                    if (!sec.partIds.contains(pid)) {
                        continue;
                    }
                    const QString name = p->partName().toQString();
                    if (name.contains("flute", Qt::CaseInsensitive)) {
                        continue;
                    }
                    ++order;
                    const QRegularExpressionMatch m = chairRe.match(name);
                    if ((m.hasMatch() ? m.captured(1).toInt() : order) == sheet.chair) {
                        return pid;
                    }
                }
            }
        }
        return QString();
    };

    auto finish = [&](INotationPtr n, StarScoreSongbookSheet& sheet) {
        engraving::Score* score = n->elements()->msScore();
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Songbook sheet"));
        n->style()->setStyleValue(StyleId::showPageNumber, false);   // the book numbers its pages
        starscoreRetitleSheet(score, sheet.left, sheet.right);
        n->undoStack()->commitChanges();
        QDir().mkpath(QFileInfo(sheet.pdfPath).absolutePath());
        QFile::remove(sheet.pdfPath);
        const Ret ret = writePdf(n, sheet.pdfPath);
        if (!ret) {
            sheet.error = QString::fromStdString(ret.toString());
        } else {
            sheet.pages = starscore::pdfPageCount(sheet.pdfPath);
        }
    };

    for (StarScoreSongbookSheet& sheet : sheets) {
        sheet.error.clear();
        sheet.pages = 0;

        if (sheet.kind == "score") {
            INotationProjectPtr p = load();
            const StarScoreArrangement* arr = nullptr;
            for (const StarScoreArrangement& a : data.arrangements) {
                if (a.templateKey == sheet.sectionKey) {
                    arr = &a;
                }
            }
            INotationPtr n;
            if (p && arr) {
                for (const IExcerptNotationPtr& e : p->masterNotation()->excerpts()) {
                    if (e && e->name() == arr->scoreName) {
                        n = e->notation();
                    }
                }
            }
            if (!n) {
                sheet.error = muse::qtrc("starscore", "the arrangement has no score (use “Make / update arrangement scores”)");
                continue;
            }
            finish(n, sheet);
            continue;
        }

        const QString pid = resolvePart(sheet);
        const engraving::Part* basePart = pid.isEmpty() ? nullptr : bms->partById(ID(pid));
        if (!basePart) {
            sheet.error = sheet.kind == "chair" ? muse::qtrc("starscore", "no Horn %1 in the Flexible section").arg(sheet.chair)
                          : sheet.kind == "rhythm" ? muse::qtrc("starscore", "no %1 part in the rhythm section").arg(sheet.role)
                          : sheet.kind == "solo" ? muse::qtrc("starscore", "no sheet for this instrument in the 1-Horn section")
                          : muse::qtrc("starscore", "part not found");
            continue;
        }
        INotationPtr srcBook = bookFor(baseMaster, basePart);

        if (!sheet.transpose) {
            // The part score as it is (in a fresh copy, so retitling can't touch anything else)
            INotationProjectPtr p = load();
            engraving::Part* part = p ? p->masterNotation()->masterScore()->partById(ID(pid)) : nullptr;
            INotationPtr n = part ? bookFor(p->masterNotation(), part) : nullptr;
            if (!n && part) {
                for (const IExcerptNotationPtr& e : p->masterNotation()->potentialExcerpts()) {
                    if (e->name() == part->partName().toQString()) {
                        p->masterNotation()->initExcerpts({ e });
                        n = e->notation();
                        break;
                    }
                }
            }
            if (!n) {
                sheet.error = muse::qtrc("starscore", "no part score for this instrument");
                continue;
            }
            finish(n, sheet);
            continue;
        }

        // Rewritten for another instrument: as the Any-horn export does
        INotationProjectPtr p = load();
        if (!p) {
            sheet.error = muse::qtrc("starscore", "couldn't open the file");
            continue;
        }
        IMasterNotationPtr vm = p->masterNotation();
        engraving::Part* part = vm->masterScore()->partById(ID(pid));
        if (!part || !part->instrument()) {
            sheet.error = muse::qtrc("starscore", "part not found");
            continue;
        }
        vm->setExcerpts({});
        if (!part->show()) {
            vm->parts()->setPartsVisible({ { part->id(), true } }, TranslatableString::untranslatable("Show"));
        }
        engraving::Instrument instrument = *part->instrument();
        instrument.setTranspose(engraving::Interval(sheet.transposeDiatonic, sheet.transposeChromatic));
        const engraving::ClefType clef = sheet.clef == 1 ? engraving::ClefType::F
                                         : sheet.clef == 2 ? engraving::ClefType::C3 : engraving::ClefType::G;
        instrument.setClefType(0, engraving::ClefTypeList(clef, clef));
        const InstrumentKey key { part->instrumentId(), part->id(), engraving::Fraction(0, 1) };
        vm->parts()->replaceInstrument(key, instrument);
        // Horn books read the lead sheet in treble clef: bars it writes in bass clef (a bass riff) become rests
        // marked "(bass)", and its clef changes go
        if (sheet.kind == "lead" && sheet.clef == 0 && !part->staves().empty()) {
            engraving::MasterScore* vs = vm->masterScore();
            engraving::Staff* st = part->staves().front();
            const engraving::staff_idx_t sidx = st->idx();
            auto isBass = [](engraving::ClefType ct) {
                return int(ct) >= int(engraving::ClefType::F) && int(ct) <= int(engraving::ClefType::F_19C);
            };
            std::vector<std::pair<engraving::Measure*, engraving::Measure*> > regions;
            engraving::Measure* start = nullptr;
            engraving::Measure* last = nullptr;
            for (engraving::Measure* m = vs->firstMeasure(); m; m = m->nextMeasure()) {
                const bool bass = isBass(st->clef(m->tick()));
                if (bass && !start) {
                    start = m;
                } else if (!bass && start) {
                    regions.emplace_back(start, last);
                    start = nullptr;
                }
                last = m;
            }
            if (start) {
                regions.emplace_back(start, last);
            }
            vm->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Bass bars as rests"));
            for (const auto& [first, lastM] : regions) {
                engraving::Segment* s1 = first->first(engraving::SegmentType::ChordRest);
                engraving::Measure* after = lastM->nextMeasure();
                engraving::Segment* s2 = after ? after->first(engraving::SegmentType::ChordRest) : nullptr;
                if (!s1) {
                    continue;
                }
                vs->selection().setRange(s1, s2, sidx, sidx + 1);
                vs->cmdDeleteSelection();
                vs->deselectAll();
                engraving::Segment* at = first->first(engraving::SegmentType::ChordRest);
                if (at) {
                    engraving::StaffText* t = engraving::Factory::createStaffText(at);
                    t->setTrack(sidx * engraving::VOICES);
                    t->setParent(at);
                    t->setPlainText(u"(bass)");
                    vs->undoAddElement(t);
                }
            }
            std::vector<engraving::EngravingItem*> clefs;
            for (engraving::Segment* seg = vs->firstSegment(engraving::SegmentType::Clef | engraving::SegmentType::HeaderClef); seg;
                 seg = seg->next1(engraving::SegmentType::Clef | engraving::SegmentType::HeaderClef)) {
                engraving::EngravingItem* e = seg->element(sidx * engraving::VOICES);
                if (e && e->isClef() && !e->generated()) {
                    clefs.push_back(e);
                }
            }
            for (engraving::EngravingItem* e : clefs) {
                vs->undoRemoveElement(e);
            }
            vm->notation()->undoStack()->commitChanges();
        }

        const QString bookName = "StarScore songbook " + QUuid::createUuid().toString(QUuid::Id128);
        vm->parts()->setInstrumentName(InstrumentKey { part->instrumentId(), part->id(), engraving::Fraction(0, 1) }, bookName);
        part->setPartName(String::fromQString(bookName));
        IExcerptNotationPtr book;
        for (const IExcerptNotationPtr& e : vm->potentialExcerpts()) {
            if (e->name() == bookName) {
                book = e;
                break;
            }
        }
        if (!book) {
            sheet.error = muse::qtrc("starscore", "couldn't make the part");
            continue;
        }
        vm->initExcerpts({ book });
        INotationPtr n = book->notation();
        if (!n) {
            sheet.error = muse::qtrc("starscore", "couldn't make the part");
            continue;
        }
        if (srcBook) {
            const QString mss = tmpDir + "/part.mss";
            if (srcBook->style()->saveStyle(io::path_t(mss))) {
                n->style()->loadStyle(io::path_t(mss), true);
            }
            engraving::Score* es = n->elements()->msScore();
            const engraving::Score* srcScore = srcBook->elements()->msScore();
            if (es && srcScore) {
                n->undoStack()->prepareChanges(TranslatableString::untranslatable("Copy layout"));
                starscore::copyLayout(srcScore, { es }, starscore::LayoutCopyOptions());
                n->undoStack()->commitChanges();
            }
        } else {
            const QString def = defaultStylePath();
            if (!def.isEmpty() && QFileInfo::exists(def)) {
                n->style()->loadStyle(io::path_t(def), true);
            }
        }
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Concert pitch"));
        n->style()->setStyleValue(StyleId::concertPitch, false);
        n->undoStack()->commitChanges();
        finish(n, sheet);
    }

    QDir(tmpDir).removeRecursively();
}

RetVal<std::vector<StarScoreSongbookSheet> > StarScoreService::songbookChartSheets(const QString& songPath,
                                                                                const QString& arrangementTemplateKey,
                                                                                const QString& chartTitle,
                                                                                const QString& outDir)
{
    using Out = RetVal<std::vector<StarScoreSongbookSheet> >;
    const QString tmpDir = QDir::tempPath() + "/StarScoreChart-" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(tmpDir);
    const QString copyPath = tmpDir + "/song.mscz";
    if (!QFile::copy(songPath, copyPath)) {
        QDir(tmpDir).removeRecursively();
        return Out::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't read the file"));
    }
    INotationProjectPtr p = projectCreator()->newProject(iocContext());
    if (!p->load(io::path_t(copyPath)) || !p->masterNotation()) {
        QDir(tmpDir).removeRecursively();
        return Out::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't open the file"));
    }
    engraving::MasterScore* ms = p->masterNotation()->masterScore();
    const Data data = loadFrom(ms);
    QDir(tmpDir).removeRecursively();

    const StarScoreArrangement* arr = nullptr;
    for (const StarScoreArrangement& a : data.arrangements) {
        if (a.templateKey == arrangementTemplateKey) {
            arr = &a;
        }
    }
    if (!arr) {
        return Out::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "the song has no such arrangement"));
    }

    const QString code = QFileInfo(songPath).completeBaseName().section(" - ", 0, 0);
    const QString prefix = code.size() >= 3 && code.size() <= 4 && code == code.toUpper() ? code + " - " : QString();
    std::vector<StarScoreSongbookSheet> sheets;

    StarScoreSongbookSheet score;
    score.kind = "score";
    score.sectionKey = arrangementTemplateKey;
    score.left = muse::qtrc("starscore", "Score");
    score.right = chartTitle;
    score.pdfPath = outDir + "/" + prefix + "Score.pdf";
    sheets.push_back(score);

    std::map<QString, int> used;
    for (const QString& sid : arr->sectionIds) {
        for (const StarScoreSection& sec : data.sections) {
            if (sec.id != sid) {
                continue;
            }
            for (const engraving::Part* part : ms->parts()) {
                const QString pid = idText(part);
                if (!sec.partIds.contains(pid) || sec.alternates.count(pid)
                    || (!sec.shownPartIds.isEmpty() && !sec.shownPartIds.contains(pid) && sec.templateKey != "lead-sheet")) {
                    continue;
                }
                const QString iid = part->instrumentId().toQString();
                // Horns by the band's name for them plus their number ("Trumpet 1 in B♭", "Alto Saxophone 2")
                static const QRegularExpression numberRe("\\s(\\d+)$");
                const QRegularExpressionMatch num = numberRe.match(part->partName().toQString());
                const QString horn = starscoreHornName(iid);
                QString name = sec.templateKey == "lead-sheet" ? QString("Lead Sheet")
                               : !horn.isEmpty() ? starscoreSheetHornName(horn + (num.hasMatch() ? " " + num.captured(1) : QString()))
                               : part->partName().toQString();
                QString file = starscoreSafeFileName(QString(name).replace(QString::fromUtf8("♭"), "b"));
                if (used[file]++ > 0) {
                    file += QString(" (%1)").arg(used[file]);
                }
                StarScoreSongbookSheet s;
                s.kind = "part";
                s.partId = pid;
                s.left = name;
                s.right = chartTitle;
                s.pdfPath = outDir + "/" + prefix + file + ".pdf";
                sheets.push_back(s);
            }
        }
    }
    return Out::make_ok(sheets);
}
