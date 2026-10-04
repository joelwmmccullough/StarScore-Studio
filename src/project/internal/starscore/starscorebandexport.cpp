/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — export a .starscore into the band's "Sheets and Demos" folder,
 * named and filed exactly like the existing sheets there (see 6 Inbox/.organizer/RULES.md):
 *
 *   <N> <Song>/1 Lead Sheet/CODE - Lead Sheet.pdf
 *   <N> <Song>/1 Rhythm/CODE - Bass.pdf, Drums, Guitar, Keys, Congas, ...
 *   <N> <Song>/3H Tpt Alt Ten/CODE - Score.pdf, CODE - Trumpet.pdf, CODE - Alto Sax.pdf, ...
 *   <N> <Song>/3H Flexible/CODE - Score.pdf, CODE - Horn 1 - Trumpet in Bb.pdf, ... (were "3H Any Horns")
 *   <N> <Song>/Big Band, Full Orchestra, Marching Band, Extras
 *
 * A file that would be replaced is first moved to "<Song>/Version History/Superseded <date>/".
 */
#include "starscoreservice.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <vector>

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
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
#include "engraving/editing/editpart.h"
#include "starscorehouse.h"
#include "starscorechordchart.h"
#include "starscorepdf.h"
#include "organizer/orgplatform.h"

#include "io/buffer.h"
#include "global/serialization/zipreader.h"
#include "translation.h"
#include "log.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;

// Order and short codes used in horn folder names ("5H 2Tpt Alt Ten Tbn")
static const std::vector<std::pair<QString, QString> > STARSCORE_HORN_ORDER {
    { "Trumpet", "Tpt" }, { "Flugelhorn", "Flg" }, { "Flute", "Flu" }, { "Clarinet", "Cla" }, { "Soprano Sax", "Sop" },
    { "Alto Sax", "Alt" }, { "Tenor Sax", "Ten" }, { "Bari Sax", "Bar" }, { "Bass Sax", "Bsx" }, { "Bassoon", "Bsn" }, { "Bass Clarinet", "Bcl" },
    { "Contrabass Clarinet", "Cbcl" }, { "Contrabassoon", "Cbsn" },
    { "Trombone", "Tbn" }, { "Bass Trombone", "Btb" }, { "Tuba", "Tba" },
};

//! 7-Horn arrangement: the subfolder for the Bass Trombone and its stand-in versions
static const QString STARSCORE_BASS_HORNS_FOLDER = QStringLiteral("Bass Horns (Horn #7)");

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
    if (id.contains("contrabass-clarinet")) {
        return "Contrabass Clarinet";
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
    if (id == "bassoon") {
        return "Bassoon";
    }
    if (id == "contrabassoon") {
        return "Contrabassoon";
    }
    if (id.contains("tuba") && !id.contains("wagner") && id != "tubaphone") {
        return "Tuba";
    }
    return QString();
}

//! The band's name for a horn (shared with the rest of StarScore), or empty when the instrument is not a horn
QString mu::project::starscore::bandHornName(const QString& instrumentId)
{
    return starscoreHornName(instrumentId);
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
        || name.startsWith("Bass Clarinet") || name.startsWith("Contrabass Clarinet")) {
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
    // Every keyboard (piano, electric piano, organ, clavinet, synth) is "Keys"
    if (id == "electric-piano" || id.contains("organ") || id == "clavinet" || id.contains("piano") || id.contains("keyboard")
        || id.contains("synth") || id == "harpsichord" || id == "celesta") {
        return "Keys";
    }
    if (id.contains("drum")) {
        return "Drums";
    }
    if (id == "congas" || id.contains("conga")) {
        return "Percussion";
    }
    if (id.contains("percussion") || id == "bongos" || id == "timbales" || id == "cajon" || id == "shaker") {
        return "Percussion";
    }
    return partName;
}

static QString starscoreSafeFileName(QString s)
{
    static const QRegularExpression unsafe("[/:\\\\]");
    s.replace(unsafe, "-");
    return s.trimmed();
}

//! "AMPL - Amplitudes" -> (AMPL, Amplitudes): the song code and the rest of a file name
static const QRegularExpression& starscoreCodedNameRe()
{
    static const QRegularExpression re("^([A-Z]{3,4})\\s*-\\s*(.*)$");
    return re;
}

//! "1 Amplitudes" -> "amplitudes": a song folder's name without its number, in lower case
static QString starscorePlainFolderName(const QString& folder)
{
    static const QRegularExpression number("^\\d+\\s+");
    QString n = folder.section('/', -1);
    n.remove(number);
    return n.trimmed().toLower();
}

static bool starscoreIsUntitled(const QString& title)
{
    const QString t = title.trimmed().toLower();
    return t.isEmpty() || t == "untitled score" || t == "untitled";
}

//! The text of the Title in the score's title frame (the first vertical frame; a horizontal or text frame before it,
//! as starscoreNeedsRetitle skips, doesn't hide it)
static QString starscoreTitleFrameText(const mu::engraving::MasterScore* ms)
{
    const mu::engraving::MeasureBase* first = ms ? ms->first() : nullptr;
    while (first && !first->isVBox() && !first->isMeasure()) {
        first = first->next();
    }
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
        const QRegularExpressionMatch m = starscoreCodedNameRe().match(fileBase);
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
    static const QRegularExpression separators("[^A-Z0-9]+");
    QStringList words;
    for (const QString& w : title.normalized(QString::NormalizationForm_D).toUpper().split(separators, Qt::SkipEmptyParts)) {
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

std::optional<starscore::org::ExportInfo> StarScoreService::takeLastExport()
{
    std::optional<starscore::org::ExportInfo> info = m_lastExport;
    m_lastExport.reset();
    return info;
}

QString StarScoreService::projectsFolder() const
{
    const QString saved = QSettings().value("StarScore/projectsFolder").toString();
    if (!saved.isEmpty() && QFileInfo(saved).isDir()) {
        return saved;
    }
    const QString audit = QSettings().value("StarScore/auditLibraryFolder").toString();
    if (audit.endsWith("Projects and Sheets") && QFileInfo(audit).isDir()) {
        return audit;
    }
    // The usual places on Joel's Mac: …/My Drive/Music/Projects and Sheets, or next to Sheets and Demos
    const QDir cloud(QDir::homePath() + "/Library/CloudStorage");
    for (const QString& drive : cloud.entryList({ "GoogleDrive-*" }, QDir::Dirs)) {
        for (const QString& rel : { QString("/My Drive/Music/Projects and Sheets"), QString("/My Drive/Projects and Sheets") }) {
            const QString candidate = cloud.filePath(drive + rel);
            if (QFileInfo(candidate).isDir()) {
                return candidate;
            }
        }
    }
    const QString band = bandFolder();
    if (!band.isEmpty()) {
        const QString sibling = QFileInfo(band).absolutePath() + "/Projects and Sheets";
        if (QFileInfo(sibling).isDir()) {
            return sibling;
        }
    }
    return QString();
}

void StarScoreService::setProjectsFolder(const QString& path)
{
    QSettings().setValue("StarScore/projectsFolder", path);
}

QString StarScoreService::songCode() const
{
    INotationProjectPtr project = exportSourceProject();
    if (!project) {
        return QString();
    }
    const RetVal<StarScoreBandExportPlan> plan = planBandExport();
    if (plan.ret && !plan.val.code.isEmpty() && !plan.val.newSong) {
        return plan.val.code;
    }
    const QString fileBase = QFileInfo(project->path().toQString()).completeBaseName();
    static const QRegularExpression codeRe("^([A-Z]{4})\\s*-\\s*");
    const QRegularExpressionMatch m = codeRe.match(fileBase);
    return m.hasMatch() ? m.captured(1) : QString();
}

QJsonObject StarScoreService::songRecordings() const
{
    INotationProjectPtr project = exportSourceProject();
    if (!project) {
        return QJsonObject();
    }
    return loadFrom(project->masterNotation()->masterScore()).recordings;
}

void StarScoreService::setSongRecordings(const QJsonObject& recordings)
{
    INotationProjectPtr project = exportSourceProject();
    if (!project) {
        return;
    }
    engraving::MasterScore* ms = project->masterNotation()->masterScore();
    Data data = loadFrom(ms);
    if (data.recordings == recordings) {
        return;
    }
    data.recordings = recordings;
    storeTo(ms, data, project);
    project->markAsUnsaved();
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
    const QRegularExpressionMatch codeMatch = starscoreCodedNameRe().match(fileBase);
    if (codeMatch.hasMatch()) {
        codeFromName = codeMatch.captured(1);
    }

    for (const auto& [folder, code] : folderToCode) {
        if (!codeFromName.isEmpty() && code == codeFromName) {
            plan.songFolder = folder;
            plan.code = code;
            break;
        }
    }
    if (plan.songFolder.isEmpty()) {
        for (const auto& [folder, code] : folderToCode) {
            if (starscorePlainFolderName(folder) == starscoreSafeFileName(title).toLower()) {
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
            const QString folder = QString("%1H Flexible").arg(horns);
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

            // 7-Horn: the main low horn (Bass Trombone unless the song chose another 7th horn) and its stand-in versions
            // (Bari Sax, Bass Sax, Bassoon…) in a folder of their own. Any of the eight low horns goes there, so the
            // main sheet sits in the same folder before and after its versions are made.
            std::set<QString> bassHorns;
            if (sec.templateKey == "7-horn" || players == 7) {
                for (const auto& [alt, main] : sec.alternates) {
                    bassHorns.insert(alt);
                    bassHorns.insert(main);
                }
                for (const auto& [pid, horn] : horns) {
                    const engraving::Part* p = ms->partById(ID(pid));
                    if (horn == "Bass Trombone" || (p && !StarScoreService::lowHornName(p->instrumentId().toQString()).isEmpty())) {
                        bassHorns.insert(pid);
                    }
                }
            }

            std::map<QString, int> seen;
            for (const auto& [pid, horn] : horns) {
                QString name = horn;
                if (counts[horn] > 1) {
                    name += QString(" %1").arg(++seen[horn]);
                }
                addFile(bassHorns.count(pid) ? folder + "/" + STARSCORE_BASS_HORNS_FOLDER : folder, name, { pid }, false);
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

//! Whether the title frame doesn't show these names yet (told without laying anything out)
static bool starscoreNeedsRetitle(const mu::engraving::Score* score, const QString& left, const QString& right)
{
    const mu::engraving::MeasureBase* mb = score ? score->first() : nullptr;
    while (mb && !mb->isVBox() && !mb->isMeasure()) {
        mb = mb->next();
    }
    if (!mb || !mb->isVBox()) {
        return false;   // no title frame to show them in
    }
    const muse::String l = muse::String::fromQString(left.toHtmlEscaped());
    const muse::String r = muse::String::fromQString(right.toHtmlEscaped());
    bool leftOk = left.isEmpty(), rightOk = right.isEmpty();
    for (const mu::engraving::EngravingItem* e : mb->el()) {
        if (!e || !e->isText() || mu::engraving::toText(e)->textStyleType() != mu::engraving::TextStyleType::INSTRUMENT_EXCERPT) {
            continue;
        }
        const mu::engraving::Text* t = mu::engraving::toText(e);
        if (t->position() == mu::engraving::AlignH::RIGHT) {
            rightOk |= t->xmlText() == r;
        } else {
            leftOk |= t->xmlText() == l;
        }
    }
    return !(leftOk && rightOk);
}

//! The sheet's title frame texts: the horn's name top left (the part name text), the arrangement top right. Running
//! it again with the same names changes nothing: an arrangement label already there gets the new text. Only edits
//! (inside a command): nothing is laid out, so the caller can lay the sheet out once with everything else it changes.
//! Returns whether anything changed.
static bool starscoreRetitleTexts(mu::engraving::Score* score, const QString& left, const QString& right)
{
    if (!score || (left.isEmpty() && right.isEmpty())) {
        return false;
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
        return false;
    }
    auto escape = [](const QString& t) {
        return muse::String::fromQString(t.toHtmlEscaped());
    };

    mu::engraving::Text* partText = nullptr;
    mu::engraving::Text* label = nullptr;
    for (mu::engraving::EngravingItem* e : box->el()) {
        if (!e || !e->isText() || mu::engraving::toText(e)->textStyleType() != mu::engraving::TextStyleType::INSTRUMENT_EXCERPT) {
            continue;
        }
        mu::engraving::Text* t = mu::engraving::toText(e);
        if (t->position() == mu::engraving::AlignH::RIGHT) {
            if (!label) {
                label = t;
            }
        } else if (!partText) {
            partText = t;
        }
    }
    bool changed = false;
    if (!left.isEmpty()) {
        if (partText) {
            if (partText->xmlText() != escape(left)) {
                partText->undoChangeProperty(mu::engraving::Pid::TEXT, escape(left));
                changed = true;
            }
        } else {
            mu::engraving::Text* t = mu::engraving::Factory::createText(box, mu::engraving::TextStyleType::INSTRUMENT_EXCERPT);
            t->setParent(box);
            t->setTrack(0);
            t->setPlacement(mu::engraving::PlacementV::ABOVE);
            t->setPropertyFlags(mu::engraving::Pid::PLACEMENT, mu::engraving::PropertyFlags::UNSTYLED);
            t->setXmlText(escape(left));
            score->undoAddElement(t);
            partText = t;
            changed = true;
        }
    }
    if (!right.isEmpty()) {
        if (label) {
            if (label->xmlText() != escape(right)) {
                label->undoChangeProperty(mu::engraving::Pid::TEXT, escape(right));
                changed = true;
            }
        } else {
            mu::engraving::Text* t = mu::engraving::Factory::createText(box, mu::engraving::TextStyleType::INSTRUMENT_EXCERPT);
            t->setParent(box);
            t->setTrack(0);
            t->setXmlText(escape(right));
            // placed at the frame's right edge (position), and right-justified (align); on the same line as the
            // instrument name: its vertical alignment and its offset (often moved by hand in the part book)
            const mu::engraving::AlignV alignV = partText ? partText->align().vertical : mu::engraving::AlignV::TOP;
            t->setAlign(mu::engraving::Align(mu::engraving::AlignH::RIGHT, alignV));
            t->setPropertyFlags(mu::engraving::Pid::ALIGN, mu::engraving::PropertyFlags::UNSTYLED);
            t->setPosition(mu::engraving::AlignH::RIGHT);
            t->setPropertyFlags(mu::engraving::Pid::POSITION, mu::engraving::PropertyFlags::UNSTYLED);
            // placement: a text made here defaults to "below", which drops it by a staff height
            t->setPlacement(partText ? partText->placement() : mu::engraving::PlacementV::ABOVE);
            t->setPropertyFlags(mu::engraving::Pid::PLACEMENT, mu::engraving::PropertyFlags::UNSTYLED);
            if (partText) {
                t->setOffset(mu::engraving::PointF(0.0, partText->offset().y()));
                t->setPropertyFlags(mu::engraving::Pid::OFFSET, mu::engraving::PropertyFlags::UNSTYLED);
                t->setSize(partText->size());
                t->setPropertyFlags(mu::engraving::Pid::FONT_SIZE, mu::engraving::PropertyFlags::UNSTYLED);
            }
            score->undoAddElement(t);
            changed = true;
        }
    }
    return changed;
}

//! The sheet's title frame as the exported sheet prints it, for the part score in StarScore: the texts
//! (starscoreRetitleTexts), and when they changed, the sheet laid out, the label levelled with the instrument name
//! and the composer credit moved clear of it. Already titled: nothing is laid out.
static void starscoreRetitleSheet(mu::engraving::Score* score, const QString& left, const QString& right)
{
    if (!starscoreRetitleTexts(score, left, right)) {
        return;
    }
    score->setLayoutAll();
    score->doLayout();
    starscore::levelArrangementLabel(score);
    starscore::clearComposerCredit(score);
}

//! The horn part scores show their exported title: the instrument name the sheet prints top left ("Trumpet 1 in
//! B♭", "Baritone Saxophone") and the arrangement top right ("7-Horn Arrangement"), with the composer credit clear of
//! it. Changes nothing when they already do. Returns how many part scores changed.
int StarScoreService::labelPartBooks()
{
    INotationProjectPtr project = exportSourceProject();
    if (!project || project != globalContext()->currentProject() || bandFolder().isEmpty()) {
        return 0;
    }
    IMasterNotationPtr master = project->masterNotation();
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms || loadFrom(ms).sections.empty()) {
        return 0;
    }
    const RetVal<StarScoreBandExportPlan> plan = planBandExport();
    if (!plan.ret) {
        return 0;
    }

    // part score by part: one instrument, preferably the one named like the part
    std::map<QString, IExcerptNotationPtr> bookForPart;
    for (const IExcerptNotationPtr& e : master->excerpts()) {
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

    // Only part scores whose title isn't what the sheet prints yet are touched (and laid out), all in one edit
    int changed = 0;
    bool open = false;
    std::vector<INotationPtr> touched;
    // Flexible sections: their chairs' part scores ("Horn 1", concert pitch) are printed as one sheet per instrument
    // that can sit in the chair; the part score shows the chair and the arrangement
    std::vector<StarScoreBandFile> files;
    std::set<QString> chairsDone;
    static const QRegularExpression chairRe("Horn\\s*(\\d+)");
    for (const StarScoreBandFile& f : plan.val.files) {
        if (!f.isVersion) {
            files.push_back(f);
            continue;
        }
        if (f.partIds.size() != 1 || chairsDone.count(f.partIds.front())) {
            continue;
        }
        chairsDone.insert(f.partIds.front());
        StarScoreBandFile chair = f;
        const QRegularExpressionMatch m = chairRe.match(f.header);
        chair.sheetLeft = m.hasMatch() ? QString("Horn %1").arg(m.captured(1)) : QString();
        chair.isVersion = false;
        files.push_back(chair);
    }
    for (const StarScoreBandFile& f : files) {
        if (f.isScore || f.isVersion || !f.sourceFile.isEmpty() || f.partIds.size() != 1
            || (f.sheetLeft.isEmpty() && f.sheetRight.isEmpty())) {
            continue;
        }
        auto it = bookForPart.find(f.partIds.front());
        if (it == bookForPart.end() || !it->second->notation()) {
            continue;
        }
        INotationPtr n = it->second->notation();
        engraving::Score* es = n->elements()->msScore();
        if (!starscoreNeedsRetitle(es, f.sheetLeft, f.sheetRight)) {
            continue;
        }
        if (!open) {
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Sheet titles"));
            open = true;
        }
        starscoreRetitleSheet(es, f.sheetLeft, f.sheetRight);
        touched.push_back(n);
        ++changed;
    }
    if (open) {
        master->notation()->undoStack()->commitChanges();
        for (const INotationPtr& n : touched) {
            n->notationChanged().notify();
        }
    }
    return changed;
}

// ---------------------------------------------------------------------------
//  Re-exports that change nothing
// ---------------------------------------------------------------------------

//! A PDF with the parts that differ between two exports of the same pages blanked out: the dates
//! (/CreationDate, /ModDate and the XMP dates), the document UUID and the /ID pair.
static QByteArray starscorePdfWithoutStamps(const QByteArray& pdf)
{
    QString s = QString::fromLatin1(pdf);   // one char per byte, so binary streams survive
    static const QRegularExpression pdfDate("\\(D:[0-9+\\-Z' ]*\\)");
    static const QRegularExpression xmpDate("(xmp:[A-Za-z]+Date)=\"[^\"]*\"");
    static const QRegularExpression uuid("uuid:[0-9A-Fa-f\\-]+");
    static const QRegularExpression ids("/ID\\s*\\[\\s*<[0-9A-Fa-f]*>\\s*<[0-9A-Fa-f]*>\\s*\\]");
    // the app that made it (StarScore and MuseScore versions): a new version alone doesn't make a sheet different
    static const QRegularExpression creator("/Creator\\s*(\\((?:\\\\.|[^\\\\)])*\\)|<[0-9A-Fa-f]*>)");
    static const QRegularExpression xmpCreator("(xmp:CreatorTool)(=\"[^\"]*\"|>[^<]*<)");
    s.replace(creator, "/Creator ()");
    s.replace(xmpCreator, "\\1");
    // a creator of another length moves every object after it: the byte offsets (xref table, startxref) and the
    // metadata stream's length change with it. The streams themselves are still compared.
    static const QRegularExpression xref("\\nxref\\s[\\s\\S]*?\\ntrailer\\b");
    static const QRegularExpression startxref("\\nstartxref\\s+\\d+");
    static const QRegularExpression length("/Length\\s+\\d+(?!\\s+\\d+\\s+R)");
    static const QRegularExpression numberObject("(\\b\\d+\\s+0\\s+obj\\s*)\\d+(\\s*endobj)");
    s.replace(xref, "\nxref trailer");
    s.replace(startxref, "\nstartxref");
    s.replace(length, "/Length");
    s.replace(numberObject, "\\1\\2");
    s.replace(pdfDate, "(D:)");
    s.replace(xmpDate, "\\1=\"\"");
    s.replace(uuid, "uuid:");
    s.replace(ids, "/ID[]");
    return s.toLatin1();
}

//! What a PDF draws, page by page, for comparing two exports of the same sheet: each page's drawing instructions
//! (unpacked), with the font's name in place of Qt's numbered font resource ("/F11"), and every number rounded to
//! a hundredth of a unit. Two exports of an unchanged sheet differ in ways that don't show: the fonts are stored
//! in another order with other subset prefixes, and a position can come out a ten-thousandth of a unit off
//! (Bumper Cars' sheets in 1.15.8). Empty when the file can't be read this way.
static QStringList starscorePdfDrawing(const QByteArray& pdf)
{
    const QString s = QString::fromLatin1(pdf);
    // every object: its number, its dictionary, and where its stream starts
    static const QRegularExpression object("(\\d+)\\s+0\\s+obj\\b");
    std::map<int, qsizetype> objectAt;
    for (auto it = object.globalMatch(s); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        objectAt[m.captured(1).toInt()] = m.capturedEnd();
    }
    auto dictOf = [&](int number) -> QString {
        auto at = objectAt.find(number);
        if (at == objectAt.end()) {
            return QString();
        }
        const qsizetype end = s.indexOf("endobj", at->second);
        const qsizetype streamAt = s.indexOf("stream", at->second);
        const qsizetype stop = streamAt >= 0 && (end < 0 || streamAt < end) ? streamAt : end;
        return stop < 0 ? QString() : s.mid(at->second, stop - at->second);
    };
    auto streamOf = [&](int number) -> QByteArray {
        auto at = objectAt.find(number);
        if (at == objectAt.end()) {
            return QByteArray();
        }
        const qsizetype end = s.indexOf("endobj", at->second);
        qsizetype from = s.indexOf("stream", at->second);
        if (from < 0 || (end >= 0 && from > end)) {
            return QByteArray();
        }
        from += 6;
        if (s.mid(from, 2) == "\r\n") {
            from += 2;
        } else if (s.mid(from, 1) == "\n") {
            from += 1;
        }
        const qsizetype to = s.indexOf("endstream", from);
        if (to < 0) {
            return QByteArray();
        }
        // The stream is as long as its /Length says (given directly, or as the number in another object). Only
        // without one is it taken as everything up to the line end before "endstream": the packed data itself can
        // end in line-end bytes, and cutting every one of them off broke the unpacking, so such a sheet counted as
        // changed and was archived and rewritten on every export (Bumper Cars' Horn 2 Tenor Sax version, one sheet
        // in twenty)
        qsizetype length = -1;
        static const QRegularExpression lengthRe("/Length\\s+(\\d+)(?:\\s+0\\s+R)?");
        const QRegularExpressionMatch lm = lengthRe.match(dictOf(number));
        if (lm.hasMatch()) {
            if (lm.captured(0).endsWith('R')) {
                const QString other = dictOf(lm.captured(1).toInt()).trimmed();
                bool ok = false;
                const qsizetype n = other.toLongLong(&ok);
                length = ok ? n : -1;
            } else {
                length = lm.captured(1).toLongLong();
            }
        }
        QByteArray data;
        // (the length is trusted when "endstream" does follow it, at most a line end away)
        const qsizetype after = length >= 0 && from + length <= pdf.size() ? s.indexOf("endstream", from + length) : -1;
        if (after >= 0 && after - (from + length) <= 2) {
            data = pdf.mid(from, length);
        } else {
            data = pdf.mid(from, to - from);
            if (data.endsWith("\r\n")) {
                data.chop(2);
            } else if (data.endsWith('\n') || data.endsWith('\r')) {
                data.chop(1);
            }
        }
        if (dictOf(number).contains("/FlateDecode")) {
            // qUncompress wants the unpacked size up front; a generous guess is enough
            QByteArray sized(4, '\0');
            const quint32 guess = quint32(std::min<qint64>(qint64(data.size()) * 40 + 4096, 64 * 1024 * 1024));
            sized[0] = char((guess >> 24) & 0xff);
            sized[1] = char((guess >> 16) & 0xff);
            sized[2] = char((guess >> 8) & 0xff);
            sized[3] = char(guess & 0xff);
            data = qUncompress(sized + data);
        }
        return data;
    };

    // the fonts: resource name -> font name without its subset prefix ("QBBAAA+PetalumaText" -> "PetalumaText")
    std::map<QString, QString> fontName;
    static const QRegularExpression fontRef("/(F\\d+)\\s+(\\d+)\\s+0\\s+R");
    static const QRegularExpression baseFont("/BaseFont\\s*/(?:[A-Z]{6}\\+)?([^\\s/<>\\[\\]()]+)");
    for (auto it = fontRef.globalMatch(s); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        const QRegularExpressionMatch b = baseFont.match(dictOf(m.captured(2).toInt()));
        if (b.hasMatch()) {
            fontName[m.captured(1)] = b.captured(1);
        }
    }

    // the pages in the order they are stored, each with its drawing instructions
    static const QRegularExpression page("/Type\\s*/Page\\b(?!s)");
    static const QRegularExpression contents("/Contents\\s*(\\[[^\\]]*\\]|\\d+\\s+0\\s+R)");
    static const QRegularExpression ref("(\\d+)\\s+0\\s+R");
    static const QRegularExpression fontUse("/(F\\d+)(?=[\\s/\\[<(])");
    static const QRegularExpression number("-?\\d*\\.\\d+|-?\\d+(?=[\\s\\]\\[/<>()]|$)");
    QStringList pages;
    for (const auto& [n, at] : objectAt) {
        Q_UNUSED(at);
        const QString dict = dictOf(n);
        if (!page.match(dict).hasMatch()) {
            continue;
        }
        const QRegularExpressionMatch c = contents.match(dict);
        if (!c.hasMatch()) {
            return QStringList();
        }
        QString drawing;
        for (auto it = ref.globalMatch(c.captured(1)); it.hasNext();) {
            const QByteArray data = streamOf(it.next().captured(1).toInt());
            if (data.isEmpty()) {
                return QStringList();
            }
            drawing += QString::fromLatin1(data);
        }
        QString out;
        qsizetype last = 0;
        // fonts by name, numbers rounded
        QString named;
        for (auto it = fontUse.globalMatch(drawing); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            named += drawing.mid(last, m.capturedStart() - last);
            auto f = fontName.find(m.captured(1));
            named += "/" + (f != fontName.end() ? f->second : m.captured(1));
            last = m.capturedEnd();
        }
        named += drawing.mid(last);
        last = 0;
        for (auto it = number.globalMatch(named); it.hasNext();) {
            const QRegularExpressionMatch m = it.next();
            out += named.mid(last, m.capturedStart() - last);
            out += QString::number(std::round(m.captured(0).toDouble() * 100.0) / 100.0, 'f', 2);
            last = m.capturedEnd();
        }
        out += named.mid(last);
        pages << out;
    }
    return pages;
}

//! True when a freshly exported PDF (its bytes) shows exactly what the existing file shows: the same bytes apart
//! from the export date and the random document id, or else the same drawing on every page
static bool starscoreSamePdf(const QByteArray& fresh, const QString& existingPath)
{
    QFile b(existingPath);
    if (!b.open(QIODevice::ReadOnly)) {
        return false;
    }
    // (sizes can differ by the creator text alone: a sheet made by a newer StarScore)
    const qint64 freshSize = fresh.size();
    if (qAbs(freshSize - b.size()) > std::max<qint64>(4096, std::max(freshSize, b.size()) / 50)) {
        return false;
    }
    const QByteArray y = b.readAll();
    if (fresh == y || starscorePdfWithoutStamps(fresh) == starscorePdfWithoutStamps(y)) {
        return true;
    }
    const QStringList drawn = starscorePdfDrawing(fresh);
    return !drawn.isEmpty() && drawn == starscorePdfDrawing(y);
}

//! True when two .mscz files hold the same files with the same contents (the zip's own dates are ignored)
static bool starscoreSameMscz(const QString& freshPath, const QString& existingPath)
{
    if (!QFileInfo::exists(freshPath) || !QFileInfo::exists(existingPath)) {
        return false;
    }
    ZipReader a { io::path_t(freshPath) };
    ZipReader b { io::path_t(existingPath) };
    if (a.hasError() || b.hasError()) {
        return false;
    }
    std::set<std::string> names;
    for (const ZipReader::FileInfo& f : a.fileInfoList()) {
        if (f.isFile) {
            names.insert(f.filePath.toStdString());
        }
    }
    std::set<std::string> other;
    for (const ZipReader::FileInfo& f : b.fileInfoList()) {
        if (f.isFile) {
            other.insert(f.filePath.toStdString());
        }
    }
    if (names != other || names.empty()) {
        return false;
    }
    for (const std::string& n : names) {
        if (a.fileData(n) != b.fileData(n)) {
            return false;
        }
    }
    return true;
}

//! The part score MuseScore would make for one part (not made yet), by name. MuseScore keeps that list from before
//! a part is renamed for printing, so the part can be listed under its new name or its old one (the "Any Horns"
//! sheets, "Horn 1 - Trumpet in Bb", were looked up by the new name only and never found: only Score.pdf was written).
static mu::notation::IExcerptNotationPtr starscorePotentialBook(const mu::notation::IMasterNotationPtr& master,
                                                                const QStringList& names)
{
    if (!master) {
        return nullptr;
    }
    for (const QString& name : names) {
        for (const mu::notation::IExcerptNotationPtr& e : master->potentialExcerpts()) {
            if (e->name() == name) {
                return e;
            }
        }
    }
    return nullptr;
}

//! The PDF of the score as it is laid out now, as bytes: nothing is laid out here, so the caller decides when the one
//! layout a sheet needs happens (and the bytes are at hand for comparing with the file already exported, for the
//! sheet record's md5 and for writing the file, without a temporary file read back from disk)
static Ret starscorePdfBytes(const INotationWriterPtr& writer, const INotationPtr& notation, QByteArray& pdf)
{
    if (!writer || !notation) {
        return make_ret(Ret::Code::InternalError);
    }
    io::Buffer out;
    if (!out.open(io::IODevice::WriteOnly)) {
        return make_ret(Ret::Code::UnknownError);
    }
    INotationWriter::Options options { { INotationWriter::OptionKey::UNIT_TYPE, Val(INotationWriter::UnitType::PER_PART) } };
    const Ret ret = writer->write(notation, out, options);
    out.close();
    pdf = out.data().toQByteArray();
    return ret;
}

//! A sheet as the export prints it, in ONE full layout. Inside a command and in page view: the title texts (the
//! instrument name top left, the arrangement top right) are set, the sheet is laid out once, the arrangement label is
//! levelled with the instrument name and the composer credit moved clear of it (both measure that layout; each lays
//! out again only when it moves something), and the PDF is taken from that layout. The sheet goes back to its view
//! afterwards. A Score (partSheet false) has no sheet title or label; only its credit is placed.
//!
//! The levelling and the credit rely on measuring a page-view layout of the sheet's current state: that is kept, the
//! layout just isn't repeated (an export of 74 sheets laid each one out up to six times).
static Ret starscorePrintSheet(const INotationWriterPtr& writer, const INotationPtr& notation, const QString& left,
                               const QString& right, bool partSheet, QByteArray& pdf)
{
    mu::engraving::Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    if (!score) {
        return make_ret(Ret::Code::InternalError);
    }
    if (partSheet) {
        starscoreRetitleTexts(score, left, right);
    }
    // the one full layout, in page view (switching the view lays out by itself)
    const ViewMode oldMode = notation->painting()->viewMode();
    if (oldMode != ViewMode::PAGE) {
        notation->painting()->setViewMode(ViewMode::PAGE);
    } else {
        score->doLayout();
    }
    if (partSheet) {
        starscore::levelArrangementLabel(score);
    }
    starscore::clearComposerCredit(score);
    const Ret ret = starscorePdfBytes(writer, notation, pdf);
    if (oldMode != ViewMode::PAGE) {
        notation->painting()->setViewMode(oldMode);
    }
    return ret;
}

//! Writes the score as it is to a PDF file, laid out in page view. Lays it out only when that changes the view,
//! or when the score isn't kept laid out by MuseScore (a part book not open in a tab) or hasn't been laid out yet.
Ret StarScoreService::writePdf(const INotationPtr& notation, const QString& path) const
{
    INotationWriterPtr writer = writers()->writer("pdf");
    if (!writer || !notation) {
        return make_ret(Ret::Code::InternalError);
    }

    engraving::Score* score = notation->elements()->msScore();
    const ViewMode oldMode = notation->painting()->viewMode();
    if (oldMode != ViewMode::PAGE) {
        notation->painting()->setViewMode(ViewMode::PAGE);
    } else if (score && (!score->autoLayoutEnabled() || score->pages().empty())) {
        score->doLayout();
    }

    QByteArray pdf;
    Ret ret = starscorePdfBytes(writer, notation, pdf);
    if (ret) {
        QFile out(path);
        ret = out.open(QIODevice::WriteOnly | QIODevice::Truncate) && out.write(pdf) == pdf.size()
              ? make_ok() : make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't write the file"));
    }

    if (oldMode != ViewMode::PAGE) {
        notation->painting()->setViewMode(oldMode);
    }
    return ret;
}

//! Turns `part` of a throwaway copy of the song into the instrument a version sheet is for: shown, with this
//! transposition and clef (0 treble, 1 bass, 2 alto). The copy's own part books go first: there is nothing to keep
//! in step with the change, and the version's book is made fresh from the part.
static void starscoreRewritePartInstrument(const IMasterNotationPtr& vm, mu::engraving::Part* part, int diatonic, int chromatic,
                                           int clefKind)
{
    vm->setExcerpts({});
    if (!part->show()) {
        vm->parts()->setPartsVisible({ { part->id(), true } }, TranslatableString::untranslatable("Show"));
    }
    mu::engraving::Instrument instrument = *part->instrument();
    instrument.setTranspose(mu::engraving::Interval(diatonic, chromatic));
    const mu::engraving::ClefType clef = clefKind == 1 ? mu::engraving::ClefType::F
                                     : clefKind == 2 ? mu::engraving::ClefType::C3 : mu::engraving::ClefType::G;
    instrument.setClefType(0, mu::engraving::ClefTypeList(clef, clef));
    const InstrumentKey key { part->instrumentId(), part->id(), mu::engraving::Fraction(0, 1) };
    vm->parts()->replaceInstrument(key, instrument);
}

//! The part's book made fresh (the one MuseScore would make for the part), named `bookName` like the part itself,
//! with the look and line breaks of `srcBook` (the chair's own part book; the default style when there is none),
//! at written pitch. MuseScore may still list the book under the part's earlier name, `oldName` (see
//! starscorePotentialBook). Null when MuseScore has no book for the part. `tmpDir` holds the style file copied over.
static INotationPtr starscoreMakeVersionBook(const IMasterNotationPtr& vm, mu::engraving::Part* part, const QString& bookName,
                                             const QString& oldName, const INotationPtr& srcBook, const QString& tmpDir,
                                             const QString& defaultStyle)
{
    vm->parts()->setInstrumentName(InstrumentKey { part->instrumentId(), part->id(), mu::engraving::Fraction(0, 1) }, bookName);
    part->setPartName(String::fromQString(bookName));

    IExcerptNotationPtr book = starscorePotentialBook(vm, { bookName, oldName });
    if (!book) {
        return nullptr;
    }
    book->setName(bookName);
    vm->initExcerpts({ book });
    INotationPtr n = book->notation();
    if (!n) {
        return nullptr;
    }

    if (srcBook) {
        const QString mss = tmpDir + "/version.mss";
        if (srcBook->style()->saveStyle(io::path_t(mss))) {
            n->style()->loadStyle(io::path_t(mss), true);
        }
        mu::engraving::Score* es = n->elements()->msScore();
        const mu::engraving::Score* srcScore = srcBook->elements()->msScore();
        if (es && srcScore) {
            n->undoStack()->prepareChanges(TranslatableString::untranslatable("Copy layout"));
            starscore::copyLayout(srcScore, { es }, starscore::LayoutCopyOptions());
            n->undoStack()->commitChanges();
        }
    } else if (!defaultStyle.isEmpty() && QFileInfo::exists(defaultStyle)) {
        n->style()->loadStyle(io::path_t(defaultStyle), true);
    }

    // Written pitch, not concert pitch
    n->undoStack()->prepareChanges(TranslatableString::untranslatable("Concert pitch"));
    n->style()->setStyleValue(StyleId::concertPitch, false);
    n->undoStack()->commitChanges();
    return n;
}

//! The md5 of a PDF the export wrote, remembered for the sheet record (starscoresheetrecord.cpp) so the record
//! doesn't read the file back from Drive. Defined there; the service header is shared, so it isn't declared in it.
namespace mu::project::starscore {
void noteExportedPdf(const QString& absolutePath, const QByteArray& pdf);
void forgetExportedPdfs();
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
    // The plan is made once: the sheets to write come from it, and so do the archiving of sheets under older names
    // and the sheet record, which need every sheet, not only the ticked ones (each plan reads codes.json from Drive)
    const RetVal<StarScoreBandExportPlan> fullPlan = planBandExport();
    if (!fullPlan.ret) {
        return RetVal<QString>::make_ret(fullPlan.ret);
    }
    if (fullPlan.val.newSong || fullPlan.val.songFolder.isEmpty()) {
        // An unregistered song has no folder: the export would have written next to the song folders, into
        // "Sheets and Demos/". The dialog registers a new song first (registerBandSong) and plans again.
        return RetVal<QString>::make_ret(Ret::Code::UnknownError,
                                         muse::trc("starscore", "This song isn't in Sheets and Demos yet. Add it there first "
                                                                "(Export to Sheets and Demos asks where it goes)."));
    }
    const StarScoreBandExportPlan& full = fullPlan.val;
    StarScoreBandExportPlan plan = full;
    if (!onlyPaths.isEmpty()) {
        std::vector<StarScoreBandFile> chosen;
        for (const StarScoreBandFile& f : full.files) {
            if (onlyPaths.contains(f.relativePath)) {
                chosen.push_back(f);
            }
        }
        plan.files = chosen;
    }

    INotationProjectPtr project = exportSourceProject();
    IMasterNotationPtr master = project->masterNotation();
    engraving::MasterScore* ms = master->masterScore();
    INotationWriterPtr writer = writers()->writer("pdf");
    if (!writer) {
        return RetVal<QString>::make_ret(Ret::Code::InternalError);
    }
    starscore::forgetExportedPdfs();

    // The score is shown again once, at the end, whatever changed in it below; a part book that changed is shown
    // again once too (a notification per sheet redrew the app 74 times)
    bool masterChanged = false;
    std::vector<INotationPtr> touchedBooks;

    // Every part book numbers its bars like the main score. MuseScore keeps "exclude from measure count" per score,
    // so a pickup bar excluded in the main score after the part books were made stayed counted in them, and those
    // sheets' bar numbers ran one ahead of the others (Bet, Two). The fix is kept in the file (one undo step). The
    // part books aren't laid out here: each sheet is laid out once when it is printed.
    int renumbered = 0;
    if (starscore::syncBarNumbering(ms, starscore::BarNumberingSync::Check) > 0) {
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Bar numbers in the parts like the score"));
        renumbered = starscore::syncBarNumbering(ms, starscore::BarNumberingSync::Undoable);
        master->notation()->undoStack()->commitChanges();
        masterChanged = true;
    }

    // Every staff with the same barlines (double barlines missing from staves added later, e.g. the Bass Trombone's
    // stand-in versions); kept in the file
    int barlinesSynced = 0;
    if (starscore::syncEndBarlines(ms, false) > 0) {
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Barlines the same on every staff"));
        barlinesSynced = starscore::syncEndBarlines(ms, true);
        master->notation()->undoStack()->commitChanges();
        project->markAsUnsaved();
        masterChanged = true;
    }

    // The Keys sheet: bass staff "Always hide", empty staves hidden from the first system on (set directly, not
    // through the undo stack; the scores are marked for layout and laid out when printed)
    if (starscore::applyKeysStaffRules(ms) > 0) {
        project->markAsUnsaved();
        masterChanged = true;
    }

    const QString songDir = plan.bandFolder + "/" + plan.songFolder;
    const QString today = QDate::currentDate().toString(Qt::ISODate);
    const QString tmpDir = QDir::tempPath() + "/StarScoreExport-" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(tmpDir);

    // Part books by instrument: prefer a single-instrument part book, named like the part
    ExcerptNotationList excerpts = master->excerpts();
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

    // Sheets that aren't one of the song's own part books are made from throwaway copies of the song without its
    // part books: the Scores (a copy with only the score's instruments showing), the Flexible version sheets (a chair
    // re-written for one instrument, as a fresh part book) and the sheet of a part that has no part book yet (the
    // book MuseScore would make, made in the copy so nothing is left behind in the open score). The song is saved
    // once, loaded once, stripped of its part books and saved again as the file the copies load: each load of the
    // full file read all 74 part books (and every edit in such a copy laid them all out again), and a Balkan Wedding
    // export loaded it once per version sheet (16 times) and once more for the Scores; that was most of its ten
    // minutes. The copies are loaded fresh from the stripped file (a fraction of the full load): a copy edited for
    // one version sheet isn't reused for the next.
    const QString copyPath = tmpDir + "/copy.mscz";
    const QString strippedPath = tmpDir + "/copy-noparts.mscz";
    bool strippedSaved = false;
    bool strippedFailed = false;
    auto loadProject = [&](const QString& path) -> INotationProjectPtr {
        INotationProjectPtr p = projectCreator()->newProject(iocContext());
        if (!p->load(io::path_t(path)) || !p->masterNotation() || !p->masterNotation()->masterScore()) {
            return nullptr;
        }
        return p;
    };
    auto loadStripped = [&]() -> INotationProjectPtr {
        if (strippedFailed) {
            return nullptr;
        }
        if (!strippedSaved) {
            INotationProjectPtr p = project->save(io::path_t(copyPath), SaveMode::SaveCopy, false) ? loadProject(copyPath) : nullptr;
            if (p) {
                p->masterNotation()->setExcerpts({});
                strippedSaved = bool(p->save(io::path_t(strippedPath), SaveMode::SaveCopy, false));
            }
            if (!strippedSaved) {
                strippedFailed = true;
                return nullptr;
            }
        }
        INotationProjectPtr p = loadProject(strippedPath);
        if (!p) {
            strippedFailed = true;
        }
        return p;
    };
    // Scores of a few instruments come from one such copy with only those instruments showing
    INotationProjectPtr scratch;
    auto scratchProject = [&]() -> INotationProjectPtr {
        if (!scratch) {
            scratch = loadStripped();
        }
        return scratch;
    };

    // One of the song's own part books (kept in the file: the sheet title it gets now stays)
    auto writePartBook = [&](const StarScoreBandFile& file, const INotationPtr& book, QByteArray& pdf) -> Ret {
        // Lead and rhythm sheets have no sheet title to add, but their credit is placed the same way (Balkan Wedding's
        // long credit printed above the instrument name on the Lead Sheet and every rhythm sheet). A sheet titled
        // already but not laid out since (a new Bass Trombone version printed with the composer credit over its label)
        // gets its label on the instrument name's line and the credit below it; both change nothing when in place.
        // The edits are kept, never undone: an edit undone after a layout could leave stray bars in the score (Balkan
        // Wedding gained two empty bars).
        book->undoStack()->prepareChanges(TranslatableString::untranslatable("Sheet title"));
        const Ret ret = starscorePrintSheet(writer, book, file.sheetLeft, file.sheetRight, true, pdf);
        book->undoStack()->commitChanges();
        touchedBooks.push_back(book);
        return ret;
    };

    // A part without a part book: the book MuseScore would make for it, in a copy
    auto writePotentialBook = [&](const StarScoreBandFile& file, QByteArray& pdf) -> Ret {
        INotationProjectPtr p = loadStripped();
        if (!p) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't make a copy of the score"));
        }
        const engraving::Part* part = p->masterNotation()->masterScore()->partById(ID(file.partIds.value(0)));
        IExcerptNotationPtr book = part ? starscorePotentialBook(p->masterNotation(), { part->partName().toQString() }) : nullptr;
        if (!book) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "no part book for this instrument"));
        }
        p->masterNotation()->initExcerpts({ book });
        INotationPtr n = book->notation();
        if (!n) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "no part book for this instrument"));
        }
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Sheet title"));
        const Ret ret = starscorePrintSheet(writer, n, file.sheetLeft, file.sheetRight, true, pdf);
        n->undoStack()->commitChanges();
        return ret;
    };

    // A Flexible chair re-written for one transposition and clef, as a part book of its own
    auto writeVersion = [&](const StarScoreBandFile& file, QByteArray& pdf) -> Ret {
        INotationProjectPtr p = loadStripped();
        if (!p) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't make a copy of the score"));
        }
        IMasterNotationPtr vm = p->masterNotation();
        engraving::Part* part = vm->masterScore()->partById(ID(file.partIds.value(0)));
        if (!part || !part->instrument()) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "instrument not found"));
        }
        const QString oldName = part->partName().toQString();
        starscoreRewritePartInstrument(vm, part, file.transposeDiatonic, file.transposeChromatic, file.clef);

        // Same look and line breaks as the chair's own part book
        auto src = bookForPart.find(file.partIds.value(0));
        const INotationPtr srcBook = src != bookForPart.end() ? src->second->notation() : nullptr;
        INotationPtr n = starscoreMakeVersionBook(vm, part, file.header, oldName, srcBook, tmpDir, defaultStylePath());
        if (!n) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't make the part"));
        }
        // a copy of the song, so nothing of this is kept
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Sheet title"));
        const Ret ret = starscorePrintSheet(writer, n, file.sheetLeft, file.sheetRight, true, pdf);
        n->undoStack()->commitChanges();
        return ret;
    };

    // A Score: the scratch copy with only the score's instruments showing
    auto writeScore = [&](const StarScoreBandFile& file, QByteArray& pdf) -> Ret {
        INotationProjectPtr p = scratchProject();
        if (!p) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't make the score copy"));
        }
        engraving::MasterScore* cs = p->masterNotation()->masterScore();
        std::vector<std::pair<muse::ID, bool> > vis;
        for (const engraving::Part* part : cs->parts()) {
            vis.emplace_back(part->id(), file.partIds.contains(idText(part)));
        }
        p->masterNotation()->parts()->setPartsVisible(vis, TranslatableString::untranslatable("Export"));
        // The score names its staves without StarScore's section prefix: "Trumpet 1", not "7H: Trumpet 1"
        // (this is a copy of the file, so the names change only for the printing)
        p->masterNotation()->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Staff names"));
        for (engraving::Part* part : cs->parts()) {
            const QString name = part->longName().toQString();
            const int at = name.lastIndexOf(": ");
            if (file.partIds.contains(idText(part)) && at >= 0) {
                engraving::EditPart::setInstrumentName(cs, part, engraving::Fraction(0, 1),
                                                       String::fromQString(name.mid(at + 2).trimmed()));
            }
        }
        p->masterNotation()->notation()->undoStack()->commitChanges();
        // A part can also be hidden staff by staff (the eye on each staff in the Instruments panel); a part on
        // this score with every staff hidden would leave it empty (Balkan Wedding's 2- to 5-Horn scores were
        // blank pages). Its staves show; a part with some staves showing keeps its choice. (One edit per staff, as
        // MuseScore does it: showing a staff also drops the system locks that hold a multimeasure rest, judged on
        // the layout after the staff before it was shown.)
        for (engraving::Part* part : cs->parts()) {
            if (!file.partIds.contains(idText(part))) {
                continue;
            }
            const bool anyShown = std::any_of(part->staves().begin(), part->staves().end(),
                                              [](const engraving::Staff* st) { return st->visible(); });
            if (!anyShown) {
                for (engraving::Staff* st : part->staves()) {
                    p->masterNotation()->parts()->setStaffVisible(st->id(), true);
                }
            }
        }
        // The composer credit at its house position (last line on the subtitle's baseline), measured in page
        // view: the main score's style can carry a credit moved too far (1.15.7's Apply Styles measured it in
        // continuous view and printed Bumper Cars' credit in the music)
        p->masterNotation()->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Composer credit"));
        const Ret ret = starscorePrintSheet(writer, p->masterNotation()->notation(), QString(), QString(), false, pdf);
        p->masterNotation()->notation()->undoStack()->commitChanges();
        return ret;
    };

    QStringList archivedPaths;
    QStringList problems = plan.notes;
    // The file already at `rel` goes to Version History (never deleted). Returns false, with the file left where
    // it is, when it can't be moved there: a rename Drive refuses is retried as a copy and a remove.
    auto supersede = [&](const QString& rel, QString* archivedTo = nullptr) -> bool {
        const QString target = songDir + "/" + rel;
        if (!QFileInfo::exists(target)) {
            return true;
        }
        QString archived = songDir + "/Version History/Superseded " + today + "/" + rel;
        QDir().mkpath(QFileInfo(archived).absolutePath());
        // (the suffix is kept: the chord charts archive .html files through here too)
        const QString suffix = "." + QFileInfo(archived).suffix();
        const QString base = archived.left(archived.length() - suffix.length());
        for (int i = 2; QFileInfo::exists(archived); ++i) {
            archived = QString("%1 (%2)%3").arg(base).arg(i).arg(suffix);
        }
        bool moved = QFile::rename(target, archived);
        if (!moved) {
            moved = QFile::copy(target, archived) && QFile::remove(target);
            if (!moved) {
                QFile::remove(archived);   // a half-made copy
            }
        }
        if (!moved) {
            problems << muse::qtrc("starscore", "%1: the file already there couldn't be moved to Version History, so it was "
                                                "left as it was.").arg(rel);
            return false;
        }
        archivedPaths << rel;
        if (archivedTo) {
            *archivedTo = archived;
        }
        return true;
    };

    QStringList written;
    QStringList unchanged;

    for (const StarScoreBandFile& file : plan.files) {
        QByteArray pdf;
        Ret ret;

        if (!file.sourceFile.isEmpty()) {
            QFile source(file.sourceFile);
            ret = source.open(QIODevice::ReadOnly) ? make_ok()
                  : make_ret(Ret::Code::UnknownError, muse::trc("starscore", "the reference PDF is missing; save and reopen the score"));
            if (ret) {
                pdf = source.readAll();
            }
        } else if (file.isVersion) {
            ret = writeVersion(file, pdf);
        } else if (!file.isScore) {
            auto it = bookForPart.find(file.partIds.value(0));
            ret = it != bookForPart.end() && it->second->notation() ? writePartBook(file, it->second->notation(), pdf)
                  : writePotentialBook(file, pdf);
        } else {
            ret = writeScore(file, pdf);
        }

        if (!ret) {
            problems << muse::qtrc("starscore", "%1: %2").arg(file.relativePath).arg(QString::fromStdString(ret.toString()));
            continue;
        }

        const QString target = songDir + "/" + file.relativePath;
        // the same pages as the file already there (a re-export with nothing changed in this sheet):
        // the existing file stays, and nothing is archived
        if (QFileInfo::exists(target) && starscoreSamePdf(pdf, target)) {
            unchanged << file.relativePath;
            continue;
        }
        // The new file is written under a temporary name next to the target, the old file moves to Version History,
        // and only then does the new one take its name. An old file that can't be archived is left as it is.
        QDir().mkpath(QFileInfo(target).absolutePath());
        QSaveFile out(target);
        if (!out.open(QIODevice::WriteOnly) || out.write(pdf) != pdf.size()) {
            out.cancelWriting();
            problems << muse::qtrc("starscore", "%1: couldn't write the file.").arg(file.relativePath);
            continue;
        }
        QString archivedTo;
        if (!supersede(file.relativePath, &archivedTo)) {
            out.cancelWriting();
            continue;
        }
        if (!out.commit()) {
            if (!archivedTo.isEmpty()) {
                QFile::rename(archivedTo, target);   // the old file back where it was
                archivedPaths.removeAll(file.relativePath);
            }
            problems << muse::qtrc("starscore", "%1: couldn't write the file.").arg(file.relativePath);
            continue;
        }
        starscore::noteExportedPdf(target, pdf);
        written << file.relativePath;
    }

    // "NH Any Horns": sheets under older names (e.g. "Horn 1 in Bb" before each instrument got its own sheet)
    // are archived, as replaced sheets are
    {
        QStringList current;
        for (const StarScoreBandFile& f : full.files) {
            current << f.relativePath;
        }
        // Keyboard sheets under their older names ("Elec Piano", "Organ", "Clavinet", "Piano"), once a Keys sheet is there
        {
            const QString prefix = "1 Rhythm/" + plan.code + " - Keys";
            const bool keysThere = std::any_of(current.begin(), current.end(), [&](const QString& rel) {
                return rel.startsWith(prefix) && QFileInfo::exists(songDir + "/" + rel);
            });
            if (keysThere) {
                const QDir dir(songDir + "/1 Rhythm");
                for (const QString& fileName : dir.entryList({ plan.code + " - Elec Piano*.pdf", plan.code + " - Organ*.pdf",
                                                               plan.code + " - Clavinet*.pdf", plan.code + " - Piano*.pdf" },
                                                             QDir::Files)) {
                    const QString rel = "1 Rhythm/" + fileName;
                    if (!current.contains(rel)) {
                        supersede(rel);
                    }
                }
            }
        }
        // Percussion sheets under older names ("Congas", "Bongos"…), once a Percussion sheet is there
        {
            const QString prefix = "1 Rhythm/" + plan.code + " - Percussion";
            const bool percThere = std::any_of(current.begin(), current.end(), [&](const QString& rel) {
                return rel.startsWith(prefix) && QFileInfo::exists(songDir + "/" + rel);
            });
            if (percThere) {
                const QDir dir(songDir + "/1 Rhythm");
                for (const QString& fileName : dir.entryList({ plan.code + " - Congas*.pdf", plan.code + " - Bongos*.pdf",
                                                               plan.code + " - Timbales*.pdf", plan.code + " - Cajon*.pdf" },
                                                             QDir::Files)) {
                    const QString rel = "1 Rhythm/" + fileName;
                    if (!current.contains(rel)) {
                        supersede(rel);
                    }
                }
            }
        }
        // 7-Horn bass horns: the sheets that sat in the arrangement folder before they got their own subfolder
        for (const QString& rel : current) {
            const QString marker = "/" + STARSCORE_BASS_HORNS_FOLDER + "/";
            const int at = rel.indexOf(marker);
            if (at < 0 || !QFileInfo::exists(songDir + "/" + rel)) {
                continue;
            }
            const QString old = rel.left(at) + "/" + rel.mid(at + marker.size());
            if (!current.contains(old) && QFileInfo::exists(songDir + "/" + old)) {
                supersede(old);
            }
        }
        for (const QString& folder : plan.anyHornFolders) {
            // the folder's old name ("3H Any Horns", before 1.15.7): its sheets are archived once the new one is there
            const QString oldFolder = QString(folder).replace("Flexible", "Any Horns");
            const QDir oldDir(songDir + "/" + oldFolder);
            if (oldFolder != folder && oldDir.exists()) {
                for (const QString& fileName : oldDir.entryList({ "*.pdf", "*.PDF" }, QDir::Files)) {
                    supersede(oldFolder + "/" + fileName);
                }
                // the old folder goes once nothing is left in it (anything else in it keeps it, untouched)
                QDir(songDir).rmdir(oldFolder);
            }
            const QDir dir(songDir + "/" + folder);
            for (const QString& fileName : dir.entryList({ plan.code + " - Horn *.pdf" }, QDir::Files)) {
                const QString rel = folder + "/" + fileName;
                if (!current.contains(rel)) {
                    supersede(rel);
                }
            }
        }
    }

    QDir(tmpDir).removeRecursively();

    // --- for the organizer: what changed in each sheet since its last export, bar by bar
    {
        Data data = loadFrom(ms);
        starscore::org::ExportInfo info;
        info.songRoot = plan.songFolder;
        info.code = plan.code;
        info.title = starscoreSongTitle(project);
        info.version = data.version;
        info.written = written;
        info.archived = archivedPaths;
        QJsonObject sigs = data.exportSignatures;
        for (const StarScoreBandFile& f : plan.files) {
            if (!f.sourceFile.isEmpty()) {
                continue;
            }
            if (unchanged.contains(f.relativePath)) {
                // same pages as before: no changelog entry, but the bar signatures are kept from now on
                if (!sigs.contains(f.relativePath)) {
                    QJsonObject stored = organizerSignature(ms, f.partIds);
                    stored["version"] = data.version;
                    sigs[f.relativePath] = stored;
                }
                continue;
            }
            if (!written.contains(f.relativePath)) {
                continue;
            }
            const QJsonObject now = organizerSignature(ms, f.partIds);
            QJsonObject before = sigs.value(f.relativePath).toObject();
            if (before.isEmpty() && f.relativePath.contains("/" + STARSCORE_BASS_HORNS_FOLDER + "/")) {
                // the same sheet before it moved into the bass horns subfolder
                QString old = f.relativePath;
                old.remove(STARSCORE_BASS_HORNS_FOLDER + "/");
                before = sigs.value(old).toObject();
            }
            starscore::org::SheetChange c;
            c.relativePath = f.relativePath;
            c.isScore = f.isScore;
            c.arrangement = f.sheetRight;
            QString part = f.relativePath.section('/', -1);
            if (part.startsWith(plan.code + " - ")) {
                part.remove(0, plan.code.size() + 3);
            }
            if (part.endsWith(".pdf")) {
                part.chop(4);
            }
            c.part = part;
            if (before.isEmpty()) {
                // first export since bar signatures were kept: new, or replacing a sheet made before
                c.kind = archivedPaths.contains(f.relativePath) ? starscore::org::SheetChange::Changed : starscore::org::SheetChange::Added;
                c.barsKnown = false;
            } else {
                const QJsonArray a = before.value("bars").toArray(), b = now.value("bars").toArray();
                const QJsonObject marks = now.value("marks").toObject();
                std::vector<std::pair<int, int> > ranges;
                const int n = std::max(a.size(), b.size());
                for (int i = 0; i < n; ++i) {
                    if (i < a.size() && i < b.size() && a[i] == b[i]) {
                        continue;
                    }
                    if (!ranges.empty() && ranges.back().second == i) {    // i is 0-based; ranges are 1-based
                        ranges.back().second = i + 1;
                    } else {
                        ranges.push_back({ i + 1, i + 1 });
                    }
                }
                c.kind = ranges.empty() ? starscore::org::SheetChange::Same : starscore::org::SheetChange::Changed;
                c.bars = ranges;
                // the rehearsal marks those bars fall under
                for (const auto& [from, to] : ranges) {
                    QString current;
                    for (int bar = 1; bar <= to; ++bar) {
                        if (marks.contains(QString::number(bar))) {
                            current = marks.value(QString::number(bar)).toString();
                        }
                        if (bar >= from && !current.isEmpty() && !c.letters.contains(current)) {
                            c.letters << current;
                        }
                    }
                }
            }
            info.sheets.push_back(c);
            QJsonObject stored = now;
            stored["version"] = data.version;
            sigs[f.relativePath] = stored;
        }
        info.hornAnalysis = organizerHornAnalysis(ms, plan);
        if (!info.hornAnalysis.isEmpty()) {
            info.hornAnalysis["scoreVersion"] = data.version;
        }
        info.recordings = data.recordings;
        data.exportSignatures = sigs;
        storeTo(ms, data, project);
        m_lastExport = info;

        // --- for the folder colours: each sheet's status as exported, and what each colour needs
        writeSheetRecord(ms, data, full, written + unchanged);
    }

    // --- the chord charts (starscorechordchart.cpp): the lead sheet's chords as a phone-sized PDF and as iReal Pro
    // links, in "Chord Charts/". Only once the Lead Sheet is finished (before that the chords are still moving), and
    // not for Works In Progress (not gig-ready, so nobody should learn them yet). Replaced files go to Version
    // History like the sheets; a file that would come out the same is left as it is. The chord chart files stay
    // out of the sheet record on purpose: they aren't sheets.
    starscore::ChordChartResult charts;
    bool chartsTried = false;
    {
        const Data data = loadFrom(ms);
        bool hasLeadSheet = false;
        StarScoreStatus leadStatus = StarScoreStatus::Finished;
        for (const StarScoreSection& s : data.sections) {
            if (s.templateKey == "lead-sheet") {
                hasLeadSheet = true;
                leadStatus = std::min(leadStatus, s.status);
            }
        }
        const bool wip = plan.songFolder.startsWith("4 ");
        if (hasLeadSheet && leadStatus == StarScoreStatus::Finished && !wip) {
            chartsTried = true;
            charts = starscore::writeChordCharts(ms, songDir, plan.code, starscoreSongTitle(project), scoreVersion(),
                                                 supersede, &starscoreSamePdf, problems);
        }
    }

    // shown again once, with everything this export changed in them
    if (masterChanged) {
        master->notation()->notationChanged().notify();
    }
    for (const INotationPtr& n : touchedBooks) {
        n->notationChanged().notify();
    }

    // --- the song's to-do list as a PDF, next to the .starscore in Projects and Sheets ("BALK - To-Do.pdf"); it's about
    // the work on the song, so it stays out of Sheets and Demos. Made fresh each export.
    QString todoNote;
    {
        const QString projects = projectsFolder();
        const QString source = project->path().toQString();
        const QString dir = source.isEmpty() ? QString() : QFileInfo(source).absolutePath();
        if (!projects.isEmpty() && !dir.isEmpty() && QDir::cleanPath(dir).startsWith(QDir::cleanPath(projects) + "/")
            && !plan.code.isEmpty() && starscore::org::canRenderPdf()) {
            starscore::org::RenderJob job;
            job.html = todoPdfHtml(starscoreSongTitle(project), plan.code, loadFrom(ms).version);
            job.pdfPath = dir + "/" + plan.code + " - To-Do.pdf";
            if (!job.html.isEmpty()) {
                starscore::org::renderPdfs({ job }, [](int, bool) {}, []() {});
                todoNote = muse::qtrc("starscore", "The to-do list is in %1.").arg(QFileInfo(job.pdfPath).fileName()
                                                                                    + " (" + QDir(projects).relativeFilePath(dir) + ")");
            }
        }
    }

    QString renumberNote;
    if (renumbered > 0) {
        renumberNote = muse::qtrc("starscore", "%1 part book(s) numbered their bars differently from the score (a bar excluded from "
                                               "the measure count in the score but counted in the part); they now match the score. "
                                               "Save the file to keep this.").arg(renumbered);
    }
    QString summary = written.isEmpty() && !unchanged.isEmpty()
                      ? muse::qtrc("starscore", "Nothing to write: every sheet is the same as the file already in %1.")
                      .arg(plan.songFolder)
                      : muse::qtrc("starscore", "Wrote %1 PDF(s) to %2.").arg(written.size()).arg(plan.songFolder);
    if (!written.isEmpty() && !unchanged.isEmpty()) {
        summary += " " + muse::qtrc("starscore", "%1 sheet(s) came out the same as before, so those files were left as they were.")
                   .arg(unchanged.size());
    }
    if (chartsTried) {
        QStringList names;
        for (const QString& rel : charts.written) {
            names << QFileInfo(rel).fileName();
        }
        if (!names.isEmpty()) {
            summary += " " + muse::qtrc("starscore", "+ chord charts: %1.").arg(names.join(", "));
        } else if (!charts.unchanged.isEmpty()) {
            summary += " " + muse::qtrc("starscore", "+ chord charts: the same as before.");
        }
    }
    if (!renumberNote.isEmpty()) {
        summary += "\n\n" + renumberNote;
    }
    if (barlinesSynced > 0) {
        summary += "\n\n" + muse::qtrc("starscore", "%1 barline(s) were plain on some staves and double (or final) on others; "
                                                   "they now match on every staff. Save the file to keep this.").arg(barlinesSynced);
    }
    if (!todoNote.isEmpty()) {
        summary += "\n\n" + todoNote;
    }
    if (!problems.isEmpty()) {
        summary += "\n\n" + muse::qtrc("starscore", "Skipped:") + "\n• " + problems.join("\n• ");
    }
    if (m_lastExport) {
        m_lastExport->summary = summary;
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
    const QRegularExpressionMatch m = starscoreCodedNameRe().match(fileBase);
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
    static const QRegularExpression fourLetters("^[A-Z]{4}$");
    if (!fourLetters.match(code).hasMatch()) {
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
        if (starscorePlainFolderName(f) == title.toLower() && f != folder) {
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
    // written whole or not at all: a write cut short (Drive syncing the file) would leave the organizer without any
    // of the song codes
    QSaveFile out(codesPath);
    if (!out.open(QIODevice::WriteOnly) || out.write(json.toUtf8()) < 0 || !out.commit()) {
        return make_ret(Ret::Code::UnknownError,
                        muse::qtrc("starscore", "Couldn't write %1.").arg(codesPath).toStdString());
    }

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
    const QRegularExpressionMatch m = starscoreCodedNameRe().match(fileBase);
    if (m.hasMatch()) {
        prefix = m.captured(1);
    } else {
        const QString band = bandFolder();
        const QString title = starscoreSongTitle(project);
        if (!band.isEmpty() && !title.isEmpty()) {
            for (const auto& [f, c] : starscoreReadCodes(band)) {
                if (starscorePlainFolderName(f) == starscoreSafeFileName(title).toLower()) {
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
    QStringList unchanged;
    const QString freshDir = QDir::tempPath() + "/StarScoreMscz-" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(freshDir);
    for (const StarScoreArrangement& a : list) {
        const QString name = prefix + " - " + starscoreSafeFileName(a.name) + ".mscz";
        const QString target = folder + "/" + name;

        // Written to a temporary file first: when it holds the same as the file already there, nothing changes
        const QString fresh = freshDir + "/" + name;
        QFile::remove(fresh);
        const Ret ret = exportArrangement(a.id, io::path_t(fresh));
        if (!ret) {
            QFile::remove(fresh);
            failed << QString("%1 (%2)").arg(name, QString::fromStdString(ret.toString()));
            continue;
        }
        if (starscoreSameMscz(fresh, target)) {
            QFile::remove(fresh);
            unchanged << name;
            continue;
        }

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

        const bool placed = !QFileInfo::exists(target) && QFile::copy(fresh, target);
        QFile::remove(fresh);
        if (!placed) {
            failed << QString("%1 (%2)").arg(name, muse::qtrc("starscore", "couldn't replace the existing file"));
            continue;
        }
        written << name;
    }
    QDir(freshDir).removeRecursively();

    QString summary = muse::qtrc("starscore", "Wrote %1 MuseScore file(s) to %2:").arg(written.size()).arg(folder);
    for (const QString& w : written) {
        summary += "\n  • " + w;
    }
    if (!superseded.isEmpty()) {
        summary += "\n\n" + muse::qtrc("starscore", "Older copies of %1 file(s) moved to Version History/Superseded %2.")
                   .arg(superseded.size()).arg(today);
    }
    if (!unchanged.isEmpty()) {
        summary += "\n\n" + muse::qtrc("starscore", "Left as they were (the same as the existing files):") + "\n  • "
                   + unchanged.join("\n  • ");
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
        // part books number their bars like the main score (see exportToBandFolder)
        if (p->masterNotation() && p->masterNotation()->masterScore()) {
            starscore::syncBarNumbering(p->masterNotation()->masterScore(), starscore::BarNumberingSync::Direct);
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

    const INotationWriterPtr writer = writers()->writer("pdf");
    // Titled, laid out once in page view, the arrangement label levelled with the instrument name and the composer
    // credit clear of it, as the band export prints its sheets (inside the edit: the levelling and the credit are edits)
    auto finish = [&](INotationPtr n, StarScoreSongbookSheet& sheet) {
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Songbook sheet"));
        n->style()->setStyleValue(StyleId::showPageNumber, false);   // the book numbers its pages
        QByteArray pdf;
        Ret ret = starscorePrintSheet(writer, n, sheet.left, sheet.right, true, pdf);
        n->undoStack()->commitChanges();
        if (ret) {
            QDir().mkpath(QFileInfo(sheet.pdfPath).absolutePath());
            QFile::remove(sheet.pdfPath);
            QFile out(sheet.pdfPath);
            ret = out.open(QIODevice::WriteOnly | QIODevice::Truncate) && out.write(pdf) == pdf.size()
                  ? make_ok() : make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't write the file"));
        }
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
        starscoreRewritePartInstrument(vm, part, sheet.transposeDiatonic, sheet.transposeChromatic, sheet.clef);
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
        INotationPtr n = starscoreMakeVersionBook(vm, part, bookName, part->partName().toQString(), srcBook, tmpDir,
                                                  defaultStylePath());
        if (!n) {
            sheet.error = muse::qtrc("starscore", "couldn't make the part");
            continue;
        }
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
