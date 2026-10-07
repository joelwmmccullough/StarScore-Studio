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
#include <QElapsedTimer>
#include <tuple>
#include <QCryptographicHash>
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
#include "engraving/editing/editsystemlocks.h"
#include "engraving/dom/page.h"
#include "engraving/dom/bracket.h"
#include "engraving/dom/measurenumber.h"
#include "engraving/dom/system.h"
#include "engraving/dom/box.h"
#include "engraving/dom/text.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/select.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "engraving/dom/harmony.h"

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
#include "io/file.h"
#include "global/serialization/zipreader.h"
#include "engraving/infrastructure/mscwriter.h"
#include "engraving/rw/rwregister.h"
#include "engraving/rw/inoutdata.h"
#include "engraving/dom/chordlist.h"
#include "engraving/dom/imageStore.h"
#include "translation.h"
#include "log.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;

// Order and short codes used in horn folder names ("5H 2Tpt Alt Ten Tbn")
static const std::vector<std::pair<QString, QString> > STARSCORE_HORN_ORDER {
    { "Trumpet", "Tpt" }, { "Flugelhorn", "Flg" }, { "Piccolo", "Pic" }, { "Flute", "Flu" }, { "Clarinet", "Cla" }, { "Soprano Sax", "Sop" },
    { "Alto Sax", "Alt" }, { "Tenor Sax", "Ten" }, { "Bari Sax", "Bar" }, { "Bass Sax", "Bsx" }, { "Bassoon", "Bsn" }, { "Bass Clarinet", "Bcl" },
    { "Contrabass Clarinet", "Cbcl" }, { "Contrabassoon", "Cbsn" },
    { "Trombone", "Tbn" }, { "Bass Trombone", "Btb" }, { "Tuba", "Tba" },
};

//! 7-Horn arrangement: the subfolder for the Bass Trombone and its stand-in versions
static const QString STARSCORE_BASS_HORNS_FOLDER = QStringLiteral("Bass Horns (Horn #7)");

//! A Flexible chair as one instrument can play it (Starsign Band Guide, page 3)
struct StarScoreSeat {
    const char* file;           // file name part, e.g. "Trumpet in Bb"
    const char* sheet;          // printed name
    int dia;                    // transposition, sounding relative to written
    int chrom;
    int clef;                   // 0 treble, 1 bass, 2 alto, 3 tenor
    const char* instrumentId;   // the instrument, for a sheet made into a part of its own
};

//! The instruments a Flexible chair is printed for: chair `number` of a `horns`-chair section. Horn 1 of 3 also on
//! Flute (made from Horn 1 like the others since 1.18.2; before, from a hidden "Horn 1 (Flute)" staff of its own).
static std::vector<StarScoreSeat> starscoreFlexibleSeats(int horns, int number)
{
    static const StarScoreSeat SOP { "Soprano Sax", "Soprano Saxophone", -1, -2, 0, "soprano-saxophone" };
    static const StarScoreSeat CLA { "Clarinet in Bb", "Clarinet in B\u266D", -1, -2, 0, "bb-clarinet" };
    static const StarScoreSeat TPT { "Trumpet in Bb", "Trumpet in B\u266D", -1, -2, 0, "bb-trumpet" };
    static const StarScoreSeat ALT { "Alto Sax", "Alto Saxophone", -5, -9, 0, "alto-saxophone" };
    static const StarScoreSeat VLN { "Violin", "Violin", 0, 0, 0, "violin" };
    static const StarScoreSeat FLU { "Flute", "Flute", 0, 0, 0, "flute" };
    static const StarScoreSeat TEN { "Tenor Sax", "Tenor Saxophone", -8, -14, 0, "tenor-saxophone" };
    static const StarScoreSeat VLA { "Viola", "Viola", 0, 0, 2, "viola" };
    static const StarScoreSeat BAR { "Bari Sax", "Baritone Saxophone", -12, -21, 0, "baritone-saxophone" };
    static const StarScoreSeat TBN { "Trombone", "Trombone", 0, 0, 1, "trombone" };
    // also in tenor clef: most jazz trombonists read bass clef, a sizeable minority prefer tenor clef
    static const StarScoreSeat TBN_TENOR { "Trombone (Tenor Clef)", "Trombone", 0, 0, 3, "trombone" };
    static const StarScoreSeat BCL { "Bass Clarinet in Bb", "Bass Clarinet in B\u266D", -8, -14, 0, "bb-bass-clarinet" };
    static const StarScoreSeat VC { "Cello", "Cello", 0, 0, 1, "violoncello" };
    if (number == 1 && horns > 1) {
        return horns >= 3 ? std::vector<StarScoreSeat> { CLA, SOP, TPT, ALT, VLN, FLU } : std::vector<StarScoreSeat> { SOP, CLA, TPT, ALT, VLN };
    }
    if (number >= horns && horns > 1) {
        return { TEN, BAR, TBN, TBN_TENOR, BCL, VC };
    }
    return { TPT, CLA, ALT, TEN, VLA };
}

//! "Horn 1 - Alto Sax": the sheet's name in its file name and in the section's menu
static QString starscoreSeatSheetName(int number, const StarScoreSeat& seat)
{
    return QString("Horn %1 - %2").arg(number).arg(QString::fromUtf8(seat.file));
}

//! A Flexible section's chairs in score order with their numbers ("Horn N" in the part name). Left out: a hidden
//! "Horn 1 (Flute)" staff from before 1.18.2, and parts made to edit one sheet by hand (stand-ins of a chair).
static std::vector<std::pair<QString, int> > starscoreFlexibleChairs(const mu::engraving::MasterScore* ms, const StarScoreSection& sec)
{
    static const QRegularExpression chairRe("Horn\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption);
    std::vector<std::pair<QString, int> > chairs;
    for (const mu::engraving::Part* p : ms->parts()) {
        const QString pid = p->id().toQString();
        if (!sec.partIds.contains(pid) || sec.alternates.count(pid)) {
            continue;
        }
        const QString name = p->partName().toQString();
        if (name.contains("flute", Qt::CaseInsensitive)) {
            continue;
        }
        const QRegularExpressionMatch cm = chairRe.match(name);
        chairs.emplace_back(pid, cm.hasMatch() ? cm.captured(1).toInt() : int(chairs.size()) + 1);
    }
    return chairs;
}

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
    // (a piccolo is its own horn, not a flute: until 1.17.1 it was labelled "Flute" on export)
    if (id == "piccolo") {
        return "Piccolo";
    }
    if (id == "flute" || id == "c-flute") {
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
QString mu::project::starscore::oneHornName(const QString& instrumentId)
{
    const QString name = bandHornName(instrumentId);
    if (name == "Alto Sax") {
        return "Eb Saxophone";
    }
    if (name == "Tenor Sax") {
        return "Bb Saxophone";
    }
    return name;
}

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
    if (id.contains("percussion") || id == "bongos" || id == "timbales" || id == "cajon" || id.contains("shaker")
        || id.contains("tambourine") || id.contains("cowbell")) {
        return "Percussion";
    }
    // Anything else in the rhythm section is the keyboard player's sheet: "Keys" (Joel, 6 Oct 2026: Last Pint's accordion
    // came out as "PINT - Accordion"), as the sheet record and the songbooks already counted it
    Q_UNUSED(partName);
    return "Keys";
}

//! The name StarScore gave a rhythm-section sheet before 1.18.19: the part's own name for an instrument it didn't know
static QString starscoreRhythmNameBefore11819(const QString& id, const QString& partName)
{
    if (id == "electric-piano" || id.contains("organ") || id == "clavinet" || id.contains("piano") || id.contains("keyboard")
        || id.contains("synth") || id == "harpsichord" || id == "celesta" || id.contains("drum") || id.contains("conga")
        || id.contains("percussion") || id == "bongos" || id == "timbales" || id == "cajon" || id == "shaker"
        || id.contains("guitar") || id.contains("bass") || id == "contrabass") {
        return QString();
    }
    return partName;
}

//! A Score's name top left on its page (Joel, 6 Oct 2026): "Concert Score", "B♭ Score"…
static QString starscoreScoreLabel(const QString& name)
{
    return QString(name).replace("Bb ", QString::fromUtf8("B\u266D ")).replace("Eb ", QString::fromUtf8("E\u266D "));
}

//! A section's Score file (Joel, 6 Oct 2026, 1.18.19): "CODE - Section Score (Concert).pdf", "(Transposing)", "(Bb)",
//! "(Eb)", "(Treble Clef)", "(Bass Clef)". The page still says "Concert Score", "B♭ Score"… top left.
static QString starscoreSectionScoreName(const QString& type)
{
    return "Section Score (" + type + ")";
}

//! The same Score under its 1.18.18 name ("CODE - Concert Score.pdf"…), or empty when rel isn't a section Score
static QString starscoreScoreNameBefore11819(const QString& rel)
{
    static const QRegularExpression re("^(.*) - Section Score \\((Concert|Transposing|Bb|Eb|Treble Clef|Bass Clef)\\)\\.pdf$");
    const QRegularExpressionMatch m = re.match(rel);
    return m.hasMatch() ? m.captured(1) + " - " + m.captured(2) + " Score.pdf" : QString();
}

//! The section folder a sheet belongs to: its own folder, or the one above for a Score in "Section Scores" or a
//! Flexible horn's sheet in "Horn 1"… (1.18.22)
QString starscoreSectionFolderOf(const QString& rel)
{
    static const QRegularExpression sub("/(Section Scores|Horn \\d+)$");
    QString folder = rel.section('/', 0, -2);
    folder.remove(sub);
    return folder;
}

//! Where the same sheet was before StarScore moved or renamed it, newest first: a Score at the top of its section
//! folder (1.18.19 to 1.18.21) and under its 1.18.18 name; a Flexible horn's sheet at the top of the Flexible folder
static QStringList starscoreFormerPathsOf(const StarScoreBandFile& f)
{
    QStringList out;
    const QString folder = f.relativePath.section('/', 0, -2);
    const QString file = f.relativePath.section('/', -1);
    static const QString scoresSub = "/Section Scores";
    if (f.isScore && folder.endsWith(scoresSub)) {
        const QString top = folder.left(folder.size() - scoresSub.size()) + "/" + file;
        out << top;
        const QString v18 = starscoreScoreNameBefore11819(top);
        if (!v18.isEmpty()) {
            out << v18;
        }
    }
    static const QRegularExpression hornSub("^(\\d+H Flexible)/Horn \\d+/(.+)$");
    const QRegularExpressionMatch m = hornSub.match(f.relativePath);
    if (m.hasMatch()) {
        out << m.captured(1) + "/" + m.captured(2);
    }
    return out;
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
    if (m_exportSource) {
        return m_exportSource;   // a half-time or double-time copy being exported (exportTimeVariants)
    }
    return m_mainProject ? m_mainProject : globalContext()->currentProject();
}

//! A Half-Time or Double-Time sheet's place: in a subfolder of its part folder ("4H/Double-Time/CODE - Trumpet.pdf",
//! "4H/Double-Time/Section Scores/…"); a file at the top of the song folder goes into the subfolder there
static QString starscoreVariantPath(const QString& rel, const QString& variant)
{
    if (variant.isEmpty()) {
        return rel;
    }
    const int slash = rel.indexOf('/');
    return slash < 0 ? variant + "/" + rel : rel.left(slash) + "/" + variant + rel.mid(slash);
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

    // A section's six Scores (Joel, 6 Oct 2026): concert, transposing, every instrument in B♭ or in E♭, and every
    // instrument in treble or in bass clef
    auto addScores = [&](const QString& folder, const QStringList& parts, const QString& right) {
        for (const auto& [type, key] : std::vector<std::pair<QString, QString> > {
                { "Concert", QString() }, { "Transposing", "T" }, { "Bb", "Bb" }, { "Eb", "Eb" },
                { "Treble Clef", "Treble" }, { "Bass Clef", "Bass" } }) {
            addFile(folder, starscoreSectionScoreName(type), parts, true);
            plan.files.back().sheetLeft = starscoreScoreLabel(type + " Score");
            plan.files.back().sheetRight = right;
            plan.files.back().transposingScore = key == "T";
            plan.files.back().scoreKey = key == "T" ? QString() : key;
        }
    };
    std::map<QString, QStringList> familyParts;   // "Big Band" etc: all instruments, for one score
    QStringList anyFolders;                        // "NH Any Horns" folders: older sheet names there get archived
    std::map<QString, QString> renamedFolders;     // older horn folder name -> its name now (a Piccolo was a "Flute")

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
                const QString former = starscoreRhythmNameBefore11819(p->instrumentId().toQString(), p->partName().toQString());
                if (!former.isEmpty() && starscoreSafeFileName(former) != starscoreSafeFileName(name)) {
                    plan.files.back().formerPath = "1 Rhythm/" + code + " - " + starscoreSafeFileName(former) + ".pdf";
                }
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
            const std::vector<std::pair<QString, int> > chairs = starscoreFlexibleChairs(ms, sec);   // (partId, chair number)
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
            const QString right = QString("Flexible %1-Horn Arrangement").arg(horns);
            // Four Scores (Joel, 5 Oct 2026): concert, for B♭ horns, for E♭ horns, and in bass clef; each prints the
            // chairs in its own clefs, whatever clefs the chairs are written in
            for (const auto& [type, key] : std::vector<std::pair<QString, QString> > {
                    { "Concert", "C" }, { "Bb", "Bb" }, { "Eb", "Eb" }, { "Bass Clef", "Bass" } }) {
                addFile(folder, starscoreSectionScoreName(type), scoreParts, true);
                plan.files.back().sheetLeft = starscoreScoreLabel(type + " Score");
                plan.files.back().sheetRight = right;
                plan.files.back().flexibleScoreKey = key;
            }
            for (const auto& [pid, number] : chairs) {
                for (const StarScoreSeat& seat : starscoreFlexibleSeats(horns, number)) {
                    StarScoreBandFile f;
                    const QString name = starscoreSeatSheetName(number, seat);
                    f.relativePath = folder + "/" + code + " - " + starscoreSafeFileName(name) + ".pdf";
                    f.header = arr + name;
                    f.sheetLeft = QString::fromUtf8(seat.sheet);
                    f.sheetRight = right;
                    // a sheet made into a part of its own to edit by hand (the section's menu) prints as that part
                    auto own = sec.sheetParts.find(name);
                    if (own != sec.sheetParts.end() && partById(own->second)) {
                        f.partIds = { own->second };
                    } else {
                        f.partIds = { pid };
                        f.isVersion = true;
                        f.transposeDiatonic = seat.dia;
                        f.transposeChromatic = seat.chrom;
                        f.clef = seat.clef;
                        // the Flute sheet: as the chair is when every note is in a flute's range, else all of it an
                        // octave up (Bet's Horn 1 goes down to E3)
                        if (QString::fromUtf8(seat.instrumentId) == "flute"
                            && !starscore::pitchesWithin(partById(pid), starscore::FLUTE_LOWEST, starscore::FLUTE_HIGHEST)) {
                            f.transposeDiatonic -= 7;
                            f.transposeChromatic -= 12;
                        }
                    }
                    plan.files.push_back(f);
                }
            }
            anyFolders << folder;
            continue;
        }

        // Strings (String Duo … Quintet): a folder each, a sheet per instrument (without the "Trio: " in front) and a Score
        if (sec.templateKey.startsWith("string-")) {
            const QString folder = sec.name;
            QStringList scoreParts;
            for (const QString& pid : sec.partIds) {
                engraving::Part* p = partById(pid);
                if (!p) {
                    continue;
                }
                scoreParts << pid;
                const QString name = p->partName().toQString().section(": ", -1);
                addFile(folder, name, { pid }, false);
                plan.files.back().sheetRight = sec.name;
            }
            if (scoreParts.size() > 1) {
                addFile(folder, starscoreSectionScoreName("Concert"), scoreParts, true);
                plan.files.back().sheetLeft = starscoreScoreLabel("Concert Score");
                plan.files.back().sheetRight = sec.name;
            }
            continue;
        }

        // 1-Horn: one melody sheet per horn, each played alone with the rhythm section (no score)
        if (sec.templateKey == "1-horn") {
            for (const QString& pid : sec.partIds) {
                engraving::Part* p = partById(pid);
                const QString horn = p ? starscoreHornName(p->instrumentId().toQString()) : QString();
                QString name = horn.isEmpty() && p ? p->partName().toQString() : horn;
                if (name.isEmpty()) {
                    continue;
                }
                // "Eb Saxophone" and "Bb Saxophone" (Joel, 5 Oct 2026)
                const QString one = p ? starscore::oneHornName(p->instrumentId().toQString()) : QString();
                const bool sax = one.endsWith("Saxophone");
                if (sax) {
                    name = one;
                }
                addFile("1H", name, { pid }, false);
                plan.files.back().sheetLeft = sax ? QString(name).replace("Eb ", QString::fromUtf8("E\u266D "))
                                                    .replace("Bb ", QString::fromUtf8("B\u266D "))
                                                  : starscoreSheetHornName(name);
                plan.files.back().sheetRight = QString("1-Horn Arrangement");
                // the Trombone sheet in tenor clef too (Joel, 5 Oct 2026), as the Flexible trombone sheets
                if (name == "Trombone") {
                    addFile("1H", "Trombone (Tenor Clef)", { pid }, false);
                    StarScoreBandFile& f = plan.files.back();
                    f.sheetLeft = starscoreSheetHornName(name);
                    f.sheetRight = QString("1-Horn Arrangement");
                    f.isVersion = true;
                    f.transposeDiatonic = 0;
                    f.transposeChromatic = 0;
                    f.clef = 3;
                }
            }
            // sheets under the names from before 1.18.11 ("Alto Sax", "Tenor Sax") are archived once these are written
            anyFolders << "1H";
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
            if (codes.contains("Pic") && !codes.contains("Flu")) {
                // the folder's name before 1.17.1, when the piccolo was labelled a flute
                QStringList old = codes;
                old.replace(old.indexOf("Pic"), "Flu");
                renamedFolders[QString("%1H %2").arg(players).arg(old.join(' '))] = folder;
            }
            QStringList scoreParts;
            for (const QString& pid : sec.partIds) {
                // stand-in versions aren't on the Score, except a piccolo's Flute: that's what's usually played
                const auto alt = sec.alternates.find(pid);
                const engraving::Part* mainPart = alt != sec.alternates.end() ? partById(alt->second) : nullptr;
                if (alt == sec.alternates.end()
                    || (mainPart && starscoreHornName(mainPart->instrumentId().toQString()) == "Piccolo")) {
                    scoreParts << pid;
                }
            }
            // the six Scores, the arrangement top right as on the parts
            addScores(folder, scoreParts, QString("%1-Horn Arrangement").arg(players));

            // 7-Horn: the main low horn (Bass Trombone unless the song chose another 7th horn) and its stand-in versions
            // (Bari Sax, Bass Sax, Bassoon…) in a folder of their own. Any of the eight low horns goes there, so the
            // main sheet sits in the same folder before and after its versions are made.
            std::set<QString> bassHorns;
            if (sec.templateKey == "7-horn" || players == 7) {
                for (const auto& [alt, main] : sec.alternates) {
                    bassHorns.insert(alt);
                    bassHorns.insert(main);
                }
                // the 7th chair: the last low horn that isn't a stand-in (a Bass Clarinet doubler stays in the main folder)
                QString seventh;
                for (const auto& [pid, horn] : horns) {
                    const engraving::Part* p = ms->partById(ID(pid));
                    if (!sec.alternates.count(pid) && p && !StarScoreService::lowHornName(p->instrumentId().toQString()).isEmpty()) {
                        seventh = pid;
                    }
                }
                if (!seventh.isEmpty()) {
                    bassHorns.insert(seventh);
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
                // 4- to 7-Horn trombones in tenor clef too (Joel, 6 Oct 2026), as the 1-Horn and Flexible ones; not the
                // bass trombone, which reads bass clef
                if (horn == "Trombone" && players >= 4 && players <= 7) {
                    const QString tenor = name + " (Tenor Clef)";
                    addFile(bassHorns.count(pid) ? folder + "/" + STARSCORE_BASS_HORNS_FOLDER : folder, tenor, { pid }, false);
                    StarScoreBandFile& f = plan.files.back();
                    f.sheetLeft = starscoreSheetHornName(name);
                    f.sheetRight = QString("%1-Horn Arrangement").arg(players);
                    f.isVersion = true;
                    f.transposeDiatonic = 0;
                    f.transposeChromatic = 0;
                    f.clef = 3;
                }
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
        const QString right = (folder == "Full Orchestra" ? QString("Orchestra") : folder) + " Arrangement";
        addScores(folder, parts, right);
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

    // Joel, 6 Oct 2026 (1.18.22): a folder's Scores in its "Section Scores" subfolder, and a Flexible folder's sheets in
    // a subfolder per horn ("2H Flexible/Horn 1/CODE - Horn 1 - Trumpet in Bb.pdf")
    {
        static const QRegularExpression flexibleHorn("^(\\d+H Flexible)/(.+ - Horn (\\d+) - .+)$");
        for (StarScoreBandFile& f : plan.files) {
            if (!f.sourceFile.isEmpty()) {
                continue;
            }
            if (f.isScore) {
                f.relativePath = f.relativePath.section('/', 0, -2) + "/Section Scores/" + f.relativePath.section('/', -1);
                continue;
            }
            const QRegularExpressionMatch m = flexibleHorn.match(f.relativePath);
            if (m.hasMatch()) {
                f.relativePath = m.captured(1) + "/Horn " + m.captured(3) + "/" + m.captured(2);
            }
        }
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
    plan.renamedFolders = renamedFolders;

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
            // the label as given, or as shortened to clear the title (fitArrangementLabel)
            for (const QString& v : starscore::arrangementLabelVariants(right)) {
                rightOk |= t->xmlText() == muse::String::fromQString(v.toHtmlEscaped());
            }
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
        bool fitted = false;   // already the label, or a shortened form of it (fitArrangementLabel)
        if (label) {
            for (const QString& v : starscore::arrangementLabelVariants(right)) {
                fitted |= label->xmlText() == escape(v);
            }
        }
        if (label) {
            if (!fitted) {
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

//! How a horn-section Score is laid out: its staves top to bottom, the brackets, and the small staves (Joel's list,
//! 5 Oct 2026). valid false: the Score keeps the song's own order and brackets (custom sections).
struct StarScoreScoreLayout {
    bool valid = false;
    QStringList order;                    // part ids, top to bottom
    struct Group {
        QStringList partIds;              // consecutive in order
        mu::engraving::BracketType type = mu::engraving::BracketType::NORMAL;
        size_t column = 0;                // 0 next to the staves; a higher column is further left
    };
    std::vector<Group> groups;            // added in this order
    QStringList small;                    // parts on small staves (the piccolo)
    std::map<QString, QString> shortNames;   // part id -> short name on the systems after the first (Flexible: "H1")
};

static StarScoreScoreLayout starscoreScoreLayout(const mu::engraving::MasterScore* ms, const std::vector<StarScoreSection>& sections,
                                                 const QStringList& partIds)
{
    using mu::engraving::BracketType;
    StarScoreScoreLayout out;
    std::map<QString, const StarScoreSection*> secOf;
    for (const StarScoreSection& s : sections) {
        for (const QString& pid : s.partIds) {
            if (partIds.contains(pid) && !secOf.count(pid)) {
                secOf[pid] = &s;
            }
        }
    }
    if (!ms || partIds.isEmpty() || secOf.size() != size_t(partIds.size())) {
        return out;
    }
    auto partOf = [&](const QString& pid) { return ms->partById(muse::ID(pid)); };
    auto numberOf = [&](const QString& pid) {
        static const QRegularExpression numRe("(\\d+)\\s*$");
        const mu::engraving::Part* p = partOf(pid);
        const QRegularExpressionMatch m = numRe.match(p ? p->partName().toQString() : QString());
        return m.hasMatch() ? m.captured(1).toInt() : 0;
    };
    auto bracket = [&](const QStringList& pids, BracketType type = BracketType::NORMAL, size_t column = 0) {
        if (pids.size() >= 2) {
            out.groups.push_back({ pids, type, column });
        }
    };
    const StarScoreSection* first = secOf[partIds.front()];
    const QString key = first->templateKey;

    // Big Band, Orchestra, Marching Band: section by section, each bracketed (the big band's rhythm section isn't)
    static const std::map<QString, QStringList> FAMILY {
        { "bigband", { "bigband-saxes", "bigband-trumpets", "bigband-trombones", "bigband-rhythm" } },
        { "orch", { "orch-woodwinds", "orch-brass", "orch-percussion", "orch-strings" } },
        { "marching", { "marching-woodwinds", "marching-brass", "marching-front", "marching-battery" } },
    };
    const auto family = FAMILY.find(key.section('-', 0, 0));
    if (family != FAMILY.end()) {
        for (const QString& sk : family->second) {
            QStringList group;
            for (const StarScoreSection& s : sections) {
                if (s.templateKey != sk) {
                    continue;
                }
                for (const QString& pid : s.partIds) {
                    if (partIds.contains(pid) && !out.order.contains(pid)) {
                        group << pid;
                    }
                }
            }
            out.order << group;
            if (sk != "bigband-rhythm") {
                bracket(group);
            }
        }
        for (const QString& pid : partIds) {
            if (!out.order.contains(pid)) {
                out.order << pid;
            }
        }
        out.valid = true;
        return out;
    }

    // Flexible: Horn 1, Horn 2 (, Horn 3), all bracketed
    static const QRegularExpression anyRe("^\\d+-horn-any$");
    if (anyRe.match(key).hasMatch()) {
        for (const auto& [pid, number] : starscoreFlexibleChairs(ms, *first)) {
            if (partIds.contains(pid)) {
                out.order << pid;
                out.shortNames[pid] = QString("H%1").arg(number);   // a chair, not an instrument: not "Tpt. 1"
            }
        }
        for (const QString& pid : partIds) {
            if (!out.order.contains(pid)) {
                out.order << pid;
            }
        }
        bracket(out.order);
        out.valid = true;
        return out;
    }

    // Standard 2- to 7-Horn
    static const QRegularExpression stdRe("^([2-7])-horn$");
    const QRegularExpressionMatch sm = stdRe.match(key);
    if (!sm.hasMatch()) {
        return out;
    }
    const int horns = sm.captured(1).toInt();
    // the 7th chair ("bass inst."): the section's last low horn that isn't a stand-in version
    QString bassPid;
    if (horns == 7) {
        for (const QString& pid : first->partIds) {
            const mu::engraving::Part* p = partOf(pid);
            if (p && partIds.contains(pid) && !first->alternates.count(pid)
                && !StarScoreService::lowHornName(p->instrumentId().toQString()).isEmpty()) {
                bassPid = pid;
            }
        }
    }
    auto nameOf = [&](const QString& pid) {
        const mu::engraving::Part* p = partOf(pid);
        return pid == bassPid ? QString("bass") : p ? starscoreHornName(p->instrumentId().toQString()) : QString();
    };
    static const std::map<QString, int> RANK {
        { "Piccolo", 0 }, { "Flute", 1 }, { "Trumpet", 2 }, { "Flugelhorn", 3 }, { "Clarinet", 4 }, { "Soprano Sax", 5 },
        { "Alto Sax", 6 }, { "Tenor Sax", 7 }, { "Bari Sax", 8 }, { "Trombone", 9 }, { "Bass Trombone", 10 },
        { "Bass Clarinet", 11 }, { "bass", 30 },
    };
    out.order = partIds;
    std::stable_sort(out.order.begin(), out.order.end(), [&](const QString& a, const QString& b) {
        const auto ra = RANK.find(nameOf(a)), rb = RANK.find(nameOf(b));
        const int ka = ra != RANK.end() ? ra->second : 20, kb = rb != RANK.end() ? rb->second : 20;
        return ka != kb ? ka < kb : numberOf(a) < numberOf(b);
    });
    QMap<QString, int> count;
    for (const QString& pid : out.order) {
        count[nameOf(pid)]++;
    }
    QString doubler;
    if (count.value("Piccolo")) {
        doubler = "pic";
    } else if (count.value("Flute")) {
        doubler = "flu";
    } else if (count.value("Clarinet")) {
        doubler = "cla";
    } else if (count.value("Bass Clarinet")) {
        doubler = "bcl";
    } else if (count.value("Tenor Sax") >= 2) {
        doubler = "ten";
    } else {
        doubler = "sax";   // alto (3-5H) or soprano (6-7H)
    }
    auto of = [&](const QStringList& names) {
        QStringList pids;
        for (const QString& pid : out.order) {
            if (names.contains(nameOf(pid))) {
                pids << pid;
            }
        }
        return pids;
    };
    const QStringList trumpets = of({ "Trumpet", "Flugelhorn" });
    const QStringList reeds = of({ "Clarinet", "Soprano Sax", "Alto Sax", "Tenor Sax", "Bari Sax" });
    const QStringList flutes = of({ "Piccolo", "Flute" });
    if (horns <= 4) {
        // all in one bracket; a piccolo and its flute also in a brace, left of the bracket
        bracket(out.order);
        if (doubler == "pic") {
            bracket(flutes, BracketType::BRACE, 1);
        }
    } else if (horns == 5 && (doubler == "flu" || doubler == "bcl")) {
        bracket(out.order);
    } else if (horns == 5 && doubler == "pic") {
        bracket(flutes);
        bracket(trumpets);
    } else {
        if (doubler == "pic") {
            bracket(flutes);
        }
        bracket(trumpets);
        bracket(reeds);
        if (horns == 7 && doubler == "bcl") {
            bracket(of({ "Bass Clarinet", "bass" }));
        }
    }
    out.small = of({ "Piccolo" });
    out.valid = true;
    return out;
}

//! Lays a Score copy out as `layout` says: the parts in that order at the top (the others after them, hidden), every
//! bracket replaced (a part of several staves keeps its brace), the small staves. A throwaway copy: nothing is undone.
static void starscoreApplyScoreLayout(const IMasterNotationPtr& m, const StarScoreScoreLayout& layout)
{
    using namespace mu::engraving;
    MasterScore* cs = m->masterScore();
    // order
    PartInstrumentList list;
    std::vector<const Part*> rest;
    for (const QString& pid : layout.order) {
        if (const Part* p = cs->partById(muse::ID(pid))) {
            PartInstrument pi;
            pi.isExistingPart = true;
            pi.partId = p->id();
            list << pi;
        }
    }
    for (const Part* p : cs->parts()) {
        if (!layout.order.contains(p->id().toQString())) {
            PartInstrument pi;
            pi.isExistingPart = true;
            pi.partId = p->id();
            list << pi;
        }
    }
    ScoreOrder order = m->parts()->scoreOrder();
    order.customized = true;
    m->parts()->setParts(list, order);

    // brackets
    // every column: bracketLevels() is the highest column used, not a count (1.18.13: a big band's sub-brackets and
    // the piano's second brace in column 2 survived, and printed beside the new ones)
    for (Staff* st : cs->staves()) {
        for (size_t c = st->bracketLevels() + 1; c-- > 0;) {
            st->setBracketType(c, BracketType::NO_BRACKET);
        }
    }
    auto setBracket = [&](Staff* top, size_t column, BracketType type, size_t span) {
        top->setBracketType(column, type);
        top->setBracketSpan(column, span);
    };
    bool braced = false;   // a piano's brace takes column 0; section brackets go outside it
    for (const QString& pid : layout.order) {
        const Part* p = cs->partById(muse::ID(pid));
        if (p && p->nstaves() > 1) {
            setBracket(p->staves().front(), 0, BracketType::BRACE, p->nstaves());
            braced = true;
        }
    }
    for (const StarScoreScoreLayout::Group& g : layout.groups) {
        Staff* top = nullptr;
        size_t span = 0;
        for (const QString& pid : g.partIds) {
            if (const Part* p = cs->partById(muse::ID(pid))) {
                if (!top && !p->staves().empty()) {
                    top = p->staves().front();
                }
                span += p->nstaves();
            }
        }
        if (top && span >= 2) {
            setBracket(top, g.column + (braced && g.type == BracketType::NORMAL ? 1 : 0), g.type, span);
        }
    }
    // short names
    for (const auto& [pid, name] : layout.shortNames) {
        if (Part* p = cs->partById(muse::ID(pid))) {
            p->setShortNameAll(muse::String::fromQString(name));
        }
    }
    // small staves
    for (const QString& pid : layout.small) {
        if (const Part* p = cs->partById(muse::ID(pid))) {
            for (Staff* st : p->staves()) {
                st->setProperty(Pid::SMALL, true);
            }
        }
    }
    cs->setLayoutAll();
}

//! A Score's repeated sections (start repeat to end repeat; from the top when there is no start repeat) on one page
//! where they fit: a section running onto the next page gets a page break before it, kept only when the whole
//! section then fits on its page. Lays the score out in page view after each try. Returns how many breaks were kept.
//! What a Score's systems depend on besides the reference part: its bars (count, lengths, time signatures) and where
//! its rehearsal marks are
static QString starscoreSystemStructureKey(const mu::engraving::Score* sc)
{
    QStringList out;
    for (const mu::engraving::Measure* m = sc->firstMeasure(); m; m = m->nextMeasure()) {
        QString bar = m->ticks().toString();
        if (m->timesig() != m->ticks()) {
            bar += "/" + m->timesig().toString();
        }
        for (const mu::engraving::Segment* seg = m->first(mu::engraving::SegmentType::ChordRest); seg;
             seg = seg->next(mu::engraving::SegmentType::ChordRest)) {
            for (const mu::engraving::EngravingItem* a : seg->annotations()) {
                if (a->isRehearsalMark()) {
                    bar += "R" + QString::number(seg->rtick().ticks());
                }
            }
        }
        out << bar;
    }
    return out.join(",");
}

//! Whether a rehearsal mark sits at the very start of the bar
static bool starscoreStartsWithRehearsalMark(const mu::engraving::Measure* m)
{
    const mu::engraving::Segment* seg = m->first(mu::engraving::SegmentType::ChordRest);
    if (!seg || seg->rtick().ticks() != 0) {
        return false;
    }
    for (const mu::engraving::EngravingItem* a : seg->annotations()) {
        if (a->isRehearsalMark()) {
            return true;
        }
    }
    return false;
}

//! The system a bar is laid out on (a multimeasure rest's, for a bar inside one)
static const mu::engraving::System* starscoreSystemOf(const mu::engraving::Measure* m)
{
    const mu::engraving::Measure* shown = m ? m->coveringMMRestOrThis() : nullptr;
    return shown ? shown->system() : nullptr;
}

//! Joel, 5 Oct 2026: a system break between two systems is taken out when both systems then fit on one system,
//! whole: a system of 3 and one of 2 become one of 5, but never a 4 and a 1 (then the break goes back). Repeated, so
//! 4 + 4 can become 8, then take the next system too. A system that starts with a rehearsal mark is never joined to
//! the one before it. Page view, laid out after each try. Returns the end ticks of the bars left with system breaks.
static std::vector<int> starscoreJoinSystems(const INotationPtr& n)
{
    mu::engraving::Score* sc = n && n->elements() ? n->elements()->msScore() : nullptr;
    std::vector<int> ends;
    if (!sc) {
        return ends;
    }
    if (n->painting()->viewMode() != ViewMode::PAGE) {
        n->painting()->setViewMode(ViewMode::PAGE);
    }
    sc->doLayout();
    for (mu::engraving::Measure* m = sc->firstMeasure(); m && m->nextMeasure(); m = m->nextMeasure()) {
        if (!m->lineBreak() || m->pageBreak()) {
            continue;
        }
        mu::engraving::Measure* next = m->nextMeasure();
        if (starscoreStartsWithRehearsalMark(next)) {
            continue;
        }
        // the two systems: from the first bar of this one to the bar ending the next one
        const mu::engraving::System* sys = starscoreSystemOf(m);
        mu::engraving::Measure* first = sys ? sys->firstMeasure() : nullptr;
        if (first && first->isMMRest()) {
            first = first->mmRestFirst();
        }
        mu::engraving::Measure* last = next;
        while (last->nextMeasure() && !last->lineBreak() && !last->pageBreak()) {
            last = last->nextMeasure();
        }
        if (!first) {
            continue;
        }
        // Each try is laid out as MuseScore lays out after an edit: from the system before the two, until the
        // systems after them fall back into line with the layout before. The whole Score laid out after each try
        // (two layouts a break) took 47 s on Branston Pickle's 7-Horn Score; the systems come out the same.
        auto relayout = [&]() {
            sc->doLayoutRange(first->tick(), last->endTick());
        };
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Join systems"));
        m->undoSetBreak(false, mu::engraving::LayoutBreakType::LINE);
        n->undoStack()->commitChanges();
        relayout();
        if (starscoreSystemOf(first) && starscoreSystemOf(first) == starscoreSystemOf(last)) {
            continue;   // joined; the next break tries to join the next system onto this one
        }
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Join systems"));
        m->undoSetBreak(true, mu::engraving::LayoutBreakType::LINE);
        n->undoStack()->commitChanges();
        relayout();
    }
    for (const mu::engraving::Measure* m = sc->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->lineBreak()) {
            ends.push_back(m->endTick().ticks());
        }
    }
    return ends;
}

//! Joel, 6 Oct 2026: bars between two system breaks that run onto more systems than needed (Bumper Cars' 4-Horn
//! Score: 4 bars, then 1 bar alone before the break) are spaced tighter, as his "{" shortcut does: the bars' stretch
//! down 0.1 at a time, to 0.3 at most. MuseScore never spaces notes closer than its minimum distances, so the bars stay
//! readable; when even the tightest spacing doesn't save a system, the bars keep their spacing. Page view, laid out
//! after each try. Returns how many runs of bars were tightened.
static int starscoreTightenWrappedSystems(const INotationPtr& n)
{
    using namespace mu::engraving;
    Score* sc = n && n->elements() ? n->elements()->msScore() : nullptr;
    if (!sc) {
        return 0;
    }
    if (n->painting()->viewMode() != ViewMode::PAGE) {
        n->painting()->setViewMode(ViewMode::PAGE);
    }
    sc->doLayout();   // (the systems were just set; the Score's copy isn't laid out after each edit)
    auto systemsOf = [](const std::vector<Measure*>& run) {
        std::set<const System*> systems;
        for (const Measure* m : run) {
            if (const System* s = starscoreSystemOf(m)) {
                systems.insert(s);
            }
        }
        return int(systems.size());
    };
    // the runs: up to and including each bar with a system or page break (or the last bar). Each lays out on its own
    // systems, so all of them are tried together, one layout per step (a layout per run and step took 15 s a Score)
    struct Run {
        std::vector<Measure*> bars;
        std::vector<double> original;
        int before = 0;
        bool kept = false;
    };
    std::vector<Run> runs;
    {
        Run run;
        for (Measure* m = sc->firstMeasure(); m; m = m->nextMeasure()) {
            run.bars.push_back(m);
            if (m->lineBreak() || m->pageBreak() || m->sectionBreak() || !m->nextMeasure()) {
                // two or three systems between breaks (a long run without breaks is left as it is: the whole of
                // it would be tightened to save one system)
                run.before = systemsOf(run.bars);
                if (run.before >= 2 && run.before <= 3) {
                    for (const Measure* r : run.bars) {
                        run.original.push_back(r->userStretch());
                    }
                    runs.push_back(run);
                }
                run = Run();
            }
        }
    }
    if (runs.empty()) {
        return 0;
    }
    // the "{" shortcut pressed 2, 4, 6 and 7 times (one layout each)
    for (int step : { 2, 4, 6, 7 }) {
        bool any = false;
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Tighter spacing"));
        for (Run& r : runs) {
            if (r.kept) {
                continue;
            }
            any = true;
            for (size_t i = 0; i < r.bars.size(); ++i) {
                r.bars[i]->undoChangeProperty(Pid::USER_STRETCH, std::max(0.3, r.original[i] - 0.1 * step));
            }
        }
        n->undoStack()->commitChanges();
        if (!any) {
            break;
        }
        // Each run sits between two breaks, so its systems depend on its bars alone: each is laid out on its own,
        // from the system before it until the systems after its break line up again with the layout before
        // (MuseScore's layout after an edit), not the whole Score for every step (140 s of The Courier's export,
        // 44 Scores). The decisions are the same; the Score is laid out whole once at the end.
        for (Run& r : runs) {
            if (!r.kept) {
                sc->doLayoutRange(r.bars.front()->tick(), r.bars.back()->endTick());
            }
        }
        for (Run& r : runs) {
            if (!r.kept && systemsOf(r.bars) < r.before) {
                r.kept = true;   // the loosest spacing that saves a system
            }
        }
    }
    int tightened = 0;
    n->undoStack()->prepareChanges(TranslatableString::untranslatable("Tighter spacing"));
    for (Run& r : runs) {
        if (r.kept) {
            ++tightened;
            continue;
        }
        for (size_t i = 0; i < r.bars.size(); ++i) {
            r.bars[i]->undoChangeProperty(Pid::USER_STRETCH, r.original[i]);
        }
    }
    n->undoStack()->commitChanges();
    sc->doLayout();   // (the steps above laid out only the runs; what follows measures the whole page)
    return tightened;
}

//! The system breaks worked out before (starscoreJoinSystems), put back: a line break exactly at those bars
static void starscoreSetSystemBreaks(const INotationPtr& n, const std::vector<int>& ends)
{
    mu::engraving::Score* sc = n && n->elements() ? n->elements()->msScore() : nullptr;
    if (!sc) {
        return;
    }
    const std::set<int> want(ends.begin(), ends.end());
    n->undoStack()->prepareChanges(TranslatableString::untranslatable("System breaks"));
    for (mu::engraving::Measure* m = sc->firstMeasure(); m; m = m->nextMeasure()) {
        const bool on = want.count(m->endTick().ticks()) > 0;
        if (on != m->lineBreak()) {
            m->undoSetBreak(on, mu::engraving::LayoutBreakType::LINE);
        }
    }
    n->undoStack()->commitChanges();
}

//! A Score whose systems don't fit on the page (an orchestra's 28 staves ran off the bottom, and the first page held
//! only the title): the staff size made smaller, a tenth at a time, until every system fits on its page and the first
//! page has music under the title, down to half the size at most. Page view, laid out after each step.
static void starscoreFitSystemsOnPages(const INotationPtr& n)
{
    mu::engraving::Score* sc = n && n->elements() ? n->elements()->msScore() : nullptr;
    if (!sc) {
        return;
    }
    if (n->painting()->viewMode() != ViewMode::PAGE) {
        n->painting()->setViewMode(ViewMode::PAGE);
    }
    sc->doLayout();
    const double start = sc->style().styleD(mu::engraving::Sid::spatium);
    for (int step = 0; step < 8; ++step) {
        bool fits = true;
        const std::vector<mu::engraving::Page*>& pages = sc->pages();
        for (const mu::engraving::Page* page : pages) {
            const double limit = page->height() - page->bm() + 1.0;
            for (const mu::engraving::System* sys : page->systems()) {
                if (!sys->vbox() && sys->pageBoundingRect().bottom() > limit) {
                    fits = false;
                }
            }
        }
        if (pages.size() > 1) {
            const auto& first = pages.front()->systems();
            fits &= std::any_of(first.begin(), first.end(), [](const mu::engraving::System* sys) { return !sys->vbox(); });
        }
        const double now = sc->style().styleD(mu::engraving::Sid::spatium);
        if (fits || now * 0.9 < start * 0.5) {
            return;
        }
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Staff size"));
        sc->undoChangeStyleVal(mu::engraving::Sid::spatium, now * 0.9);
        n->undoStack()->commitChanges();
        sc->doLayout();
    }
}

//! A bar number at the start of a system kept clear of the bracket's top hook (Joel, 5 Oct 2026): measured on the
//! laid-out page, each number that touches a hook (with a quarter space to spare) is raised just enough. Petaluma's
//! hook is 1.48 spaces tall, so the house raise of 1 space still left Bumper Cars' and The Courier's numbers on it.
//! Page view, laid out first. Returns how many numbers moved.
static int starscoreClearBarNumbersOfBrackets(const INotationPtr& n, bool laidOut = false)
{
    mu::engraving::Score* sc = n && n->elements() ? n->elements()->msScore() : nullptr;
    if (!sc) {
        return 0;
    }
    if (n->painting()->viewMode() != ViewMode::PAGE) {
        n->painting()->setViewMode(ViewMode::PAGE);
        laidOut = false;
    }
    if (!laidOut) {
        sc->doLayout();
    }
    const double gap = 0.25 * sc->style().spatium();
    // how far the bar number that sits lowest onto a bracket's hook has to go up (0: none touches)
    auto mostNeeded = [&](int* touching) {
        double most = 0.0;
        int count = 0;
        for (const mu::engraving::Page* page : sc->pages()) {
            for (mu::engraving::System* sys : page->systems()) {
                mu::engraving::Measure* m = sys->firstMeasure();
                if (!m || sys->brackets().empty()) {
                    continue;
                }
                for (mu::engraving::staff_idx_t si = 0; si < sc->nstaves(); ++si) {
                    mu::engraving::MeasureNumber* mn = m->measureNumber(si);
                    if (!mn || !mn->visible() || !mn->ldata()) {
                        continue;
                    }
                    const RectF num = mn->pageBoundingRect();
                    for (const mu::engraving::Bracket* b : sys->brackets()) {
                        if (!b || !b->ldata()) {
                            continue;
                        }
                        // the bracket's page box leaves out the hook, which rises from the first staff's top line by
                        // the music font's bracketTop glyph (1.48 spaces in Petaluma): measured from the glyph itself
                        RectF br = b->pageBoundingRect();
                        if (b->bracketType() == mu::engraving::BracketType::NORMAL) {
                            const RectF hook = b->symBbox(mu::engraving::SymId::bracketTop);
                            const double top = sys->staffYpage(b->firstStaff()) + hook.top();
                            br = RectF(br.left(), std::min(br.top(), top), std::max(br.width(), hook.width()),
                                       br.bottom() - std::min(br.top(), top));
                        }
                        const bool across = num.left() < br.right() && num.right() > br.left();
                        if (across && num.bottom() > br.top() - gap && num.top() < br.bottom()) {
                            most = std::max(most, num.bottom() - (br.top() - gap));
                            ++count;
                        }
                    }
                }
            }
        }
        if (touching) {
            *touching = count;
        }
        return most;
    };
    // Bar numbers are made again at every layout, so a number's own offset doesn't last: all of them go up, in the
    // Score's style. A number can sit higher than its style puts it (pushed up by high notes), so raising the style
    // by what one needs moves it less: measured again and raised again until none touches (a few rounds at most).
    int first = 0;
    for (int round = 0; round < 6; ++round) {
        int touching = 0;
        const double most = mostNeeded(&touching);
        if (round == 0) {
            first = touching;
        }
        if (most <= 0.0) {
            break;
        }
        const double sp = sc->style().spatium();
        const PointF pos = sc->style().styleV(mu::engraving::Sid::measureNumberPosAbove).value<PointF>();
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Bar numbers clear of brackets"));
        sc->undoChangeStyleVal(mu::engraving::Sid::measureNumberPosAbove, PointF(pos.x(), pos.y() - most / sp - 0.05));
        n->undoStack()->commitChanges();
        sc->doLayout();
    }
    return first;
}

static int starscoreKeepRepeatsOnOnePage(const INotationPtr& n, bool laidOut = false)
{
    mu::engraving::Score* score = n && n->elements() ? n->elements()->msScore() : nullptr;
    if (!score) {
        return 0;
    }
    if (n->painting()->viewMode() != ViewMode::PAGE) {
        n->painting()->setViewMode(ViewMode::PAGE);
        laidOut = false;
    }
    if (!laidOut) {
        score->doLayout();
    }
    auto pageOf = [](const mu::engraving::Measure* m) -> const mu::engraving::Page* {
        const mu::engraving::Measure* shown = m ? m->coveringMMRestOrThis() : nullptr;
        return shown && shown->system() ? shown->system()->page() : nullptr;
    };
    // how far down its usable height a page's music reaches (0 = empty, 1 = full)
    auto pageFill = [](const mu::engraving::Page* p) -> double {
        if (!p || p->systems().empty()) {
            return 0.0;
        }
        const mu::engraving::System* s = p->systems().back();
        const double usable = p->height() - p->tm() - p->bm();
        return usable > 0 ? (s->y() + s->height() - p->tm()) / usable : 1.0;
    };
    // the ranges, by first and last measure
    std::vector<std::pair<mu::engraving::Measure*, mu::engraving::Measure*> > ranges;
    mu::engraving::Measure* start = score->firstMeasure();
    for (mu::engraving::Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->repeatStart()) {
            start = m;
        }
        if (m->repeatEnd() && start) {
            ranges.emplace_back(start, m);
            start = m->nextMeasure();
        }
    }
    int kept = 0;
    for (const auto& [first, last] : ranges) {
        const mu::engraving::Page* a = pageOf(first);
        const mu::engraving::Page* b = pageOf(last);
        mu::engraving::Measure* before = first->prevMeasure();
        if (!a || !b || a == b || !before || before->pageBreak()) {
            continue;
        }
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Repeat on one page"));
        before->undoSetBreak(true, mu::engraving::LayoutBreakType::PAGE);
        n->undoStack()->commitChanges();
        score->doLayout();
        // kept only when the range now fits on one page AND the page it left stays at least half full (Top Hat's
        // first page would otherwise hold only the title and the intro)
        if (pageOf(first) == pageOf(last) && pageFill(pageOf(before)) >= 0.5) {
            ++kept;
            continue;
        }
        // longer than a page, or too much of a page left empty: as it was
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Repeat on one page"));
        before->undoSetBreak(false, mu::engraving::LayoutBreakType::PAGE);
        n->undoStack()->commitChanges();
        score->doLayout();
    }
    return kept;
}

//! A sheet as the export prints it, in ONE full layout. Inside a command and in page view: the title texts (the
//! instrument name top left, the arrangement top right) are set, the sheet is laid out once, the arrangement label is
//! levelled with the instrument name and the composer credit moved clear of it (both measure that layout; each lays
//! out again only when it moves something), and the PDF is taken from that layout. The sheet goes back to its view
//! afterwards. A Score (partSheet false) has no sheet title or label; only its credit is placed.
//!
//! The levelling and the credit rely on measuring a page-view layout of the sheet's current state: that is kept, the
//! layout just isn't repeated (an export of 74 sheets laid each one out up to six times).
//! Joel, 6 Oct 2026: a sheet that uses StarScore's minor-major 7 symbol (the triangle or diamond with a bar) explains
//! it in the left footer of the first page where it appears, in the symbol's own font: "The △ symbol indicates a
//! minor-major 7 chord." Set as score meta tags that the footer layout reads (see HeaderFooterLayout), only while the
//! sheet is printed. Returns whether a note was set (the score is then laid out again).
static bool starscoreSetMinMajNote(mu::engraving::Score* score)
{
    using namespace mu::engraving;
    // (for testing "Update all sheets": sheets made as before the footnote existed)
    if (qEnvironmentVariableIsSet("STARSCORE_TEST_NO_MINMAJ_NOTE")) {
        return false;
    }
    for (size_t pi = 0; pi < score->pages().size(); ++pi) {
        for (const System* sys : score->pages().at(pi)->systems()) {
            for (const MeasureBase* mb : sys->measures()) {
                if (!mb->isMeasure()) {
                    continue;
                }
                for (const Segment* seg = toMeasure(mb)->first(SegmentType::ChordRest); seg;
                     seg = seg->next(SegmentType::ChordRest)) {
                    for (const EngravingItem* a : seg->annotations()) {
                        if (!a->isHarmony() || !a->visible() || !a->staff() || !a->staff()->show()) {
                            continue;
                        }
                        const Harmony* h = toHarmony(a);
                        if (!h->ldata()->renderItemList.has_value()) {
                            continue;
                        }
                        for (const HarmonyRenderItem* item : h->ldata()->renderItemList.value()) {
                            const TextSegment* ts = dynamic_cast<const TextSegment*>(item);
                            if (!ts || ts->text().size() != 1) {
                                continue;
                            }
                            const char16_t c = ts->text().at(0).unicode();
                            // the barred triangles U+E001… and barred diamonds U+E021… (odd: barred), one per chord font
                            if (((c >= 0xE001 && c <= 0xE00D) || (c >= 0xE021 && c <= 0xE02D)) && (c & 1)) {
                                const String face = ts->font().family().id();
                                const String footerFace = score->style().styleSt(Sid::footerFontFace);
                                score->setMetaTag(u"starscoreFooterNote",
                                                  u"The <font face=\"" + face + u"\"/>" + String(Char(c))
                                                  + u"<font face=\"" + footerFace + u"\"/> symbol denotes a minor-major 7 chord.");
                                score->setMetaTag(u"starscoreFooterNotePage", String::number(int(pi)));
                                score->setLayoutAll();
                                score->doLayout();
                                return true;
                            }
                        }
                    }
                }
            }
        }
    }
    return false;
}

static void starscoreClearMinMajNote(mu::engraving::Score* score)
{
    // taken out entirely, so a part score saved after the export carries no trace of them
    score->metaTags().erase(u"starscoreFooterNote");
    score->metaTags().erase(u"starscoreFooterNotePage");
}

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
        // a long label running into the title is shortened ("Flexible 2H Arrangement"), then broken over two lines
        starscore::fitArrangementLabel(score);
    }
    starscore::clearComposerCredit(score);
    const bool minMajNote = starscoreSetMinMajNote(score);
    const Ret ret = starscorePdfBytes(writer, notation, pdf);
    if (minMajNote) {
        starscoreClearMinMajNote(score);
        score->setLayoutAll();
        score->doLayout();
    }
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
                                           int clefKind, bool dropBooks = true)
{
    if (dropBooks) {
        vm->setExcerpts({});
    }
    if (!part->show()) {
        vm->parts()->setPartsVisible({ { part->id(), true } }, TranslatableString::untranslatable("Show"));
    }
    mu::engraving::Instrument instrument = *part->instrument();
    instrument.setTranspose(mu::engraving::Interval(diatonic, chromatic));
    const mu::engraving::ClefType clef = clefKind == 1 ? mu::engraving::ClefType::F
                                     : clefKind == 2 ? mu::engraving::ClefType::C3
                                     : clefKind == 3 ? mu::engraving::ClefType::C4
                                     : clefKind == 4 ? mu::engraving::ClefType::G8_VB
                                     : clefKind == 5 ? mu::engraving::ClefType::F_8VA : mu::engraving::ClefType::G;
    instrument.setClefType(0, mu::engraving::ClefTypeList(clef, clef));
    // The part's main instrument sits at Part::MAIN_INSTRUMENT_TICK (-1), not at tick 0: a key at tick 0 made
    // replaceInstrument look for an instrument change there, find none, and do nothing, so every Flexible version
    // sheet came out in the chair's own concert pitch and clef (1.9.0 to 1.17.1).
    const InstrumentKey key { part->instrumentId(), part->id(), mu::engraving::Part::MAIN_INSTRUMENT_TICK };
    vm->parts()->replaceInstrument(key, instrument);
    // and the clef saved at the start (a chair written in alto clef kept it on the first system)
    vm->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("First clef"));
    starscore::setFirstClefs(part, int(clef), int(clef));
    vm->notation()->undoStack()->commitChanges();
}

//! How one instrument is written in a section Score's B♭, E♭, treble-clef or bass-clef version (Joel, 6 Oct 2026)
struct StarScoreScoreVersion {
    int diatonic = 0;
    int chromatic = 0;   // sounding = written + this
    int clef = 0;        // 0 treble, 1 bass
};

//! The instrument's notes at concert pitch: the middle one (by pitch), or its range's middle when it has no notes
static int starscoreMedianPitch(const mu::engraving::Part* part)
{
    using namespace mu::engraving;
    std::vector<int> pitches;
    const Score* score = part->score();
    for (const Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (track_idx_t t = part->startTrack(); t < part->endTrack(); ++t) {
            const EngravingItem* e = s->element(t);
            if (e && e->isChord()) {
                for (const Note* n : toChord(e)->notes()) {
                    pitches.push_back(n->pitch());
                }
            }
        }
    }
    if (pitches.empty()) {
        return (part->instrument()->minPitchA() + part->instrument()->maxPitchA()) / 2;
    }
    std::nth_element(pitches.begin(), pitches.begin() + long(pitches.size() / 2), pitches.end());
    return pitches[pitches.size() / 2];
}

//! B♭ and E♭: the transposition of that key that puts the instrument's middle note nearest the middle of the treble
//! staff (B4); treble and bass clef: the octave that puts it nearest the middle of that staff (B4, D3)
static StarScoreScoreVersion starscoreScoreVersion(const mu::engraving::Part* part, const QString& key)
{
    const int median = starscoreMedianPitch(part);
    // (diatonic, chromatic) choices
    std::vector<std::pair<int, int> > choices;
    int target = 71;
    int clef = 0;
    if (key == "Bb") {
        choices = { { 6, 10 }, { -1, -2 }, { -8, -14 }, { -15, -26 } };
    } else if (key == "Eb") {
        choices = { { 9, 15 }, { 2, 3 }, { -5, -9 }, { -12, -21 }, { -19, -33 } };
    } else if (key == "Treble") {
        choices = { { 7, 12 }, { 0, 0 }, { -7, -12 }, { -14, -24 } };
    } else {
        choices = { { 14, 24 }, { 7, 12 }, { 0, 0 }, { -7, -12 } };
        target = 50;
        clef = 1;
    }
    std::pair<int, int> best = choices.front();
    for (const auto& c : choices) {
        if (std::abs(median - c.second - target) < std::abs(median - best.second - target)) {
            best = c;
        }
    }
    return { best.first, best.second, clef };
}

//! The part's book made fresh (the one MuseScore would make for the part), named `bookName` like the part itself,
//! with the look and line breaks of `srcBook` (the chair's own part book; the default style when there is none),
//! at written pitch. MuseScore may still list the book under the part's earlier name, `oldName` (see
//! starscorePotentialBook). Null when MuseScore has no book for the part. `tmpDir` holds the style file copied over.
static INotationPtr starscoreMakeVersionBook(const IMasterNotationPtr& vm, mu::engraving::Part* part, const QString& bookName,
                                             const QString& oldName, const INotationPtr& srcBook, const QString& tmpDir,
                                             const QString& defaultStyle)
{
    vm->parts()->setInstrumentName(InstrumentKey { part->instrumentId(), part->id(), mu::engraving::Part::MAIN_INSTRUMENT_TICK }, bookName);
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
            // and where the chair's texts were put by hand ("mute", "(open)", the tempo mark), and its bar widths
            starscore::copyTextPositions(srcScore, es);
            starscore::copyMeasureWidths(srcScore, es);
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

namespace {
//! The export's throwaway copies of the song, written straight from the open score: `strippedPath` holds the song
//! without its part books, `chairsPath` (when given) the song with only the `chairs` books. What MscSaver writes
//! for a save, less the thumbnail (two full layouts of the score for a picture nothing looks at); the song's own
//! part books other than the chairs aren't written. The main score is written once, for both files: writing it
//! lays the score out with every part showing (MuseScore's rule for a score with multimeasure rests), 5 s on a big
//! song. Before, the copies came from a full save of the song, loaded, stripped and saved twice more: 44 s of a
//! Branston Pickle export. Returns false, with nothing kept, when a file couldn't be written.
bool starscoreWriteCopies(mu::engraving::MasterScore* ms, const std::vector<mu::engraving::Excerpt*>& chairs,
                          const QString& strippedPath, const QString& chairsPath)
{
    using namespace mu::engraving;
    if (!ms) {
        return false;
    }
    ByteArray styleData;
    {
        muse::io::Buffer buf(&styleData);
        buf.open(muse::io::IODevice::WriteOnly);
        ms->style().write(&buf);
    }
    rw::WriteInOutData out(ms);
    ByteArray scoreData;
    {
        muse::io::Buffer buf(&scoreData);
        buf.open(muse::io::IODevice::ReadWrite);
        rw::RWRegister::writer(ms->iocContext())->writeScore(ms, &buf, &out);
    }
    ByteArray chordListData;
    if (ms->chordList()->customChordList() && !ms->chordList()->empty()) {
        muse::io::Buffer buf(&chordListData);
        buf.open(muse::io::IODevice::WriteOnly);
        ms->chordList()->write(&buf);
    }
    std::vector<std::pair<String, ByteArray> > images;
    for (ImageStoreItem* ip : imageStore) {
        if (ip->isUsed(ms)) {
            images.emplace_back(String::fromStdString(ip->hashName()), ip->buffer());
        }
    }
    auto write = [&](const QString& path, const std::vector<Excerpt*>& excerpts) {
        muse::io::File file(path);
        MscWriter::Params params;
        params.device = &file;
        params.filePath = path;
        params.mainFileName = QFileInfo(path).completeBaseName() + ".mscx";
        params.mode = MscIoMode::Zip;
        MscWriter writer(params);
        if (!writer.open()) {
            return false;
        }
        writer.writeStyleFile(styleData);
        writer.writeScoreFile(scoreData);
        for (size_t i = 0; i < excerpts.size(); ++i) {
            Score* es = excerpts[i]->excerptScore();
            if (!es) {
                continue;
            }
            // named as a save names them (the number keeps the order; MuseScore lists a file's books in file order)
            const String name = String(u"%1_%2").arg(String::number(i), muse::io::escapeFileName(excerpts[i]->name()).toString());
            ByteArray exStyle;
            {
                muse::io::Buffer buf(&exStyle);
                buf.open(muse::io::IODevice::WriteOnly);
                es->style().write(&buf);
            }
            writer.addExcerptStyleFile(name, exStyle);
            ByteArray exData;
            {
                muse::io::Buffer buf(&exData);
                buf.open(muse::io::IODevice::ReadWrite);
                rw::RWRegister::writer(es->iocContext())->writeScore(es, &buf, &out);
            }
            writer.addExcerptFile(name, exData);
        }
        if (!chordListData.empty()) {
            writer.writeChordListFile(chordListData);
        }
        for (const auto& [name, data] : images) {
            writer.addImageFile(name, data);
        }
        writer.close();
        return !writer.hasError() && file.exists();
    };
    if (!write(strippedPath, {})) {
        QFile::remove(strippedPath);
        return false;
    }
    if (!chairsPath.isEmpty() && !write(chairsPath, chairs)) {
        QFile::remove(chairsPath);
        QFile::remove(strippedPath);
        return false;
    }
    return true;
}

//! A score not laid out after each edit while this lives (Score::update does nothing): for a throwaway copy that is
//! laid out by hand, where it is measured or printed
struct UpdatesLock {
    mu::engraving::Score* s;
    explicit UpdatesLock(mu::engraving::Score* sc)
        : s(sc) { s->lockUpdates(true); }
    ~UpdatesLock() { s->lockUpdates(false); }
    UpdatesLock(const UpdatesLock&) = delete;
    UpdatesLock& operator=(const UpdatesLock&) = delete;
};
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
    const QString variant = m_exportVariant;   // "Half-Time" / "Double-Time" (a converted copy), or "" for the song itself
    if (!onlyPaths.isEmpty() || !variant.isEmpty()) {
        std::vector<StarScoreBandFile> chosen;
        for (const StarScoreBandFile& f : full.files) {
            // (a reference PDF is copied as it is: not one of the converted sheets)
            if ((onlyPaths.isEmpty() || onlyPaths.contains(f.relativePath)) && (variant.isEmpty() || f.sourceFile.isEmpty())) {
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
    // sheets exported under an older path or name go to their place now first (every sheet of the song, ticked or
    // not; none is made again for that)
    if (!m_exportDryRun && variant.isEmpty()) {
        moveSheetsToCurrentPaths(full);
    }
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
    // book MuseScore would make, made in the copy so nothing is left behind in the open score). The song without its
    // part books is written once (starscoreWriteCopies) as the file the copies load: each load of the full file read
    // all 74 part books (and every edit in such a copy laid them all out again), and a Balkan Wedding export loaded
    // it once per version sheet (16 times) and once more for the Scores; that was most of its ten minutes. The
    // copies are loaded fresh from the stripped file (a fraction of the full load): a copy edited for one version
    // sheet isn't reused for the next.
    const QString strippedPath = tmpDir + "/copy-noparts.mscz";
    // A Flexible version sheet is printed from the chair's own part book (its hidden texts, bar widths, text positions,
    // spacers…), re-pitched for the instrument: a copy keeping only those books is saved alongside the stripped one
    const QString chairsPath = tmpDir + "/copy-chairs.mscz";
    bool chairsSaved = false;
    std::set<QString> chairPids;
    for (const StarScoreBandFile& f : plan.files) {
        if (f.isVersion && !f.partIds.isEmpty()) {
            chairPids.insert(f.partIds.front());
        }
    }
    // the single-instrument part book of a part, preferring the one named like it (as bookForPart above)
    auto bookOfPart = [](const IMasterNotationPtr& m, const QString& pid) -> IExcerptNotationPtr {
        IExcerptNotationPtr found;
        for (const IExcerptNotationPtr& e : m->excerpts()) {
            INotationPtr n = e->notation();
            engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
            if (!es || es->parts().size() != 1 || es->parts().front()->staves().empty()) {
                continue;
            }
            const engraving::Staff* linked = es->parts().front()->staves().front()->findLinkedInScore(m->masterScore());
            if (!linked || idText(linked->part()) != pid) {
                continue;
            }
            if (!found || e->name() == linked->part()->partName().toQString()) {
                found = e;
            }
        }
        return found;
    };
    bool strippedSaved = false;
    bool strippedFailed = false;
    QElapsedTimer stepClock;   // where a sheet's time goes, in the log
    stepClock.start();
    auto lapTime = [&](const QString& rel, const char* what) {
        LOGI() << "[starscore] sheet time " << rel << " " << what << " " << stepClock.restart() << " ms";
    };
    // skipLayout: the copy's main score isn't laid out on loading (for a copy whose main score is never printed or
    // measured; the Scores' copies are judged on the layout from loading on, so theirs is)
    auto loadProject = [&](const QString& path, bool skipLayout = false) -> INotationProjectPtr {
        INotationProjectPtr p = projectCreator()->newProject(iocContext());
        OpenParams params;
        params.skipLayout = skipLayout;
        if (!p->load(io::path_t(path), params) || !p->masterNotation() || !p->masterNotation()->masterScore()) {
            return nullptr;
        }
        return p;
    };
    // the stripped copy (and the chairs copy) written, once; false when that failed
    auto ensureCopies = [&]() -> bool {
        if (strippedFailed) {
            return false;
        }
        if (!strippedSaved) {
            stepClock.restart();
            std::vector<engraving::Excerpt*> chairs;
            for (const QString& pid : chairPids) {
                IExcerptNotationPtr b = bookOfPart(master, pid);
                engraving::Score* bs = b && b->notation() && b->notation()->elements() ? b->notation()->elements()->msScore() : nullptr;
                if (bs && bs->excerpt()) {
                    chairs.push_back(bs->excerpt());
                }
            }
            strippedSaved = starscoreWriteCopies(ms, chairs, strippedPath, chairs.empty() ? QString() : chairsPath);
            chairsSaved = strippedSaved && !chairs.empty();
            lapTime("copies", "write the copies");
            if (!strippedSaved) {
                strippedFailed = true;
                return false;
            }
        }
        return true;
    };
    auto loadStripped = [&]() -> INotationProjectPtr {
        if (!ensureCopies()) {
            return nullptr;
        }
        stepClock.restart();
        INotationProjectPtr p = loadProject(strippedPath);
        lapTime("copies", "load the stripped copy");
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
        // (printing lays the sheet out; the layout MuseScore does when the edit ends would be a second one. The book
        // is shown again once at the end of the export.)
        engraving::Score* bookScore = book->elements()->msScore();
        bookScore->lockUpdates(true);
        book->undoStack()->prepareChanges(TranslatableString::untranslatable("Sheet title"));
        // the 1-Horn saxophones, made from the Trumpet: no mute or open markings (Joel, 6 Oct 2026)
        static const QRegularExpression oneHornSax("(^|/)1H/.*Saxophone");
        if (oneHornSax.match(file.relativePath).hasMatch()) {
            starscore::hideMuteMarkings(book->elements()->msScore());
        }
        const Ret ret = starscorePrintSheet(writer, book, file.sheetLeft, file.sheetRight, true, pdf);
        book->undoStack()->commitChanges();
        bookScore->lockUpdates(false);
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
        const bool brass = starscore::isBrassSheet(file.sheetLeft);
        // From the chair's own part book: everything set in it carries over. (Making the copies loaded the stripped
        // copy for nothing, 2 s a sheet.)
        if (ensureCopies() && chairsSaved) {
            stepClock.restart();
            INotationProjectPtr p = loadProject(chairsPath, true);
            lapTime(file.relativePath, "load the chairs copy");
            IMasterNotationPtr vm = p ? p->masterNotation() : nullptr;
            engraving::Part* part = vm ? vm->masterScore()->partById(ID(file.partIds.value(0))) : nullptr;
            IExcerptNotationPtr book = part ? bookOfPart(vm, file.partIds.value(0)) : nullptr;
            INotationPtr n = book ? book->notation() : nullptr;
            if (n && part->instrument()) {
                // Neither score is laid out after each edit here (the copy's main score, never printed, was laid
                // out in continuous view with every part after every edit: most of a version sheet's 10 s). The book
                // is laid out by hand where MuseScore laid it out when its tab was open: after the pitch change, after
                // the instrument change, and when it's printed. The number of layouts matters: a tie's end is set
                // from the next tie's last layout, and the sheet of a book whose tab was closed when the song was
                // saved printed its ties half a point shorter.
                engraving::Score* bookScore = n->elements()->msScore();
                UpdatesLock mainLock(vm->masterScore());
                UpdatesLock bookLock(bookScore);
                vm->setExcerpts({ book });
                if (!part->show()) {
                    vm->parts()->setPartsVisible({ { part->id(), true } }, TranslatableString::untranslatable("Show"));
                }
                // written pitch first, so the instrument change transposes the book's key signatures and chord symbols
                n->undoStack()->prepareChanges(TranslatableString::untranslatable("Concert pitch"));
                n->style()->setStyleValue(StyleId::concertPitch, false);
                n->undoStack()->commitChanges();
                bookScore->setLayoutAll();
                bookScore->doLayout();
                starscoreRewritePartInstrument(vm, part, file.transposeDiatonic, file.transposeChromatic, file.clef, false);
                bookScore->setLayoutAll();
                bookScore->doLayout();
                lapTime(file.relativePath, "rewrite the instrument");
                n->undoStack()->prepareChanges(TranslatableString::untranslatable("Sheet title"));
                if (!brass) {
                    starscore::hideMuteMarkings(n->elements()->msScore());
                }
                const Ret ret = starscorePrintSheet(writer, n, file.sheetLeft, file.sheetRight, true, pdf);
                lapTime(file.relativePath, "print");
                n->undoStack()->commitChanges();
                lapTime(file.relativePath, "commit");
                return ret;
            }
        }
        // A chair without a part book of its own: the book MuseScore would make, with the look of the default style
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
        if (!brass) {
            starscore::hideMuteMarkings(n->elements()->msScore());
        }
        const Ret ret = starscorePrintSheet(writer, n, file.sheetLeft, file.sheetRight, true, pdf);
        n->undoStack()->commitChanges();
        return ret;
    };

    // A Score: the scratch copy with only the score's instruments showing
    struct ScoreCopy {
        QString folder;
        QStringList partIds;
        INotationProjectPtr p;
        std::set<int> refSystemEnds;
        QString refPartName;
        std::vector<std::tuple<double, bool, bool> > bars;   // each bar's stretch, system break, page break
        PointF numberPos;
        double spatium = 0.0;
    };
    ScoreCopy scoreCopy;
    auto writeScore = [&](const StarScoreBandFile& file, QByteArray& pdf) -> Ret {
        QElapsedTimer scoreClock;   // where a Score's time goes, in the log
        scoreClock.start();
        auto lap = [&](const char* what) {
            LOGI() << "[starscore] score time " << file.relativePath << " " << what << " " << scoreClock.restart() << " ms";
        };
        // A horn section's Score in the house order and brackets (Joel's list): on a copy of its own, as it reorders
        // the parts; any other Score as the song has it, on the shared copy
        const StarScoreScoreLayout layout = starscoreScoreLayout(ms, loadFrom(ms).sections, file.partIds);
        // A folder's six Scores share one copy (loading and preparing it took 9 s a Score): the first Score prepares it
        // and keeps how it was then; each next one puts that back (spacing, breaks, staff size, bar number position)
        const QString scoreFolder = file.relativePath.section('/', 0, -2);
        const bool reuse = layout.valid && scoreCopy.p && scoreCopy.folder == scoreFolder && scoreCopy.partIds == file.partIds;
        INotationProjectPtr p = reuse ? scoreCopy.p : layout.valid ? loadStripped() : scratchProject();
        if (!p) {
            return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "couldn't make the score copy"));
        }
        lap("copy");
        engraving::MasterScore* cs = p->masterNotation()->masterScore();
        // (Joel, 6 Oct 2026: exports took long) MuseScore lays the copy out again after every edit, and most steps
        // below then lay it out themselves to measure it: while a Score is made, the copy is laid out only where
        // something is measured (each step lays out first), about half the layouts
        UpdatesLock updatesLock(cs);
        std::set<int> refSystemEnds;   // the reference part's systems, when the Score takes its systems from a part
        QString refPartName;
        if (reuse) {
            refSystemEnds = scoreCopy.refSystemEnds;
            refPartName = scoreCopy.refPartName;
            INotationPtr rn = p->masterNotation()->notation();
            rn->undoStack()->prepareChanges(TranslatableString::untranslatable("Back to the prepared Score"));
            size_t i = 0;
            for (engraving::Measure* m = cs->firstMeasure(); m && i < scoreCopy.bars.size(); m = m->nextMeasure(), ++i) {
                const auto& [stretch, line, page] = scoreCopy.bars[i];
                if (m->userStretch() != stretch) {
                    m->undoChangeProperty(engraving::Pid::USER_STRETCH, stretch);
                }
                if (m->lineBreak() != line) {
                    m->undoSetBreak(line, engraving::LayoutBreakType::LINE);
                }
                if (m->pageBreak() != page) {
                    m->undoSetBreak(page, engraving::LayoutBreakType::PAGE);
                }
            }
            if (cs->style().styleV(engraving::Sid::measureNumberPosAbove).value<PointF>() != scoreCopy.numberPos) {
                cs->undoChangeStyleVal(engraving::Sid::measureNumberPosAbove, scoreCopy.numberPos);
            }
            if (cs->style().spatium() != scoreCopy.spatium) {
                cs->undoChangeStyleVal(engraving::Sid::spatium, scoreCopy.spatium);
            }
            rn->undoStack()->commitChanges();
        }
        if (!reuse && layout.valid) {
            starscoreApplyScoreLayout(p->masterNotation(), layout);
            lap("order and brackets");

            // Its systems (Joel, 5 Oct 2026): the arrangement's own score in the song when it was formatted by hand
            // (it has system or page breaks or system locks); otherwise the system breaks of the part on this Score
            // with the most of them (page breaks and locks aren't counted or copied), and that part's bars per system
            const Data d = loadFrom(ms);
            const engraving::Score* arrScore = nullptr;
            for (const StarScoreArrangement& a : d.arrangements) {
                bool holds = false;
                for (const StarScoreSection& s : d.sections) {
                    holds |= a.sectionIds.contains(s.id) && s.partIds.contains(file.partIds.value(0));
                }
                if (!holds || a.scoreName.isEmpty()) {
                    continue;
                }
                for (const IExcerptNotationPtr& e : master->excerpts()) {
                    if (e->name() == a.scoreName && e->notation()) {
                        arrScore = e->notation()->elements()->msScore();
                    }
                }
                if (arrScore) {
                    break;
                }
            }
            auto formatted = [](const engraving::Score* sc) {
                if (!sc->systemLocks()->allLocks().empty()) {
                    return true;
                }
                for (const engraving::Measure* m = sc->firstMeasure(); m; m = m->nextMeasure()) {
                    if (m->lineBreak() || m->pageBreak()) {
                        return true;
                    }
                }
                return false;
            };
            // breaks identical to another part score's were copied from it, not made for this Score: Top Hat's 3-Horn
            // Score carries the Lead Sheet's breaks (one intro system on page 1), so it counts as not formatted
            auto breaksOf = [](const engraving::Score* sc) {
                QStringList out;
                for (const engraving::Measure* m = sc->firstMeasure(); m; m = m->nextMeasure()) {
                    if (m->lineBreak() || m->pageBreak()) {
                        out << QString("%1%2").arg(m->tick().ticks()).arg(m->pageBreak() ? "p" : "l");
                    }
                }
                return out;
            };
            auto copiedFromAnother = [&](const engraving::Score* sc) {
                const QStringList mine = breaksOf(sc);
                if (mine.isEmpty()) {
                    return false;
                }
                for (const IExcerptNotationPtr& e : master->excerpts()) {
                    const engraving::Score* other = e->notation() ? e->notation()->elements()->msScore() : nullptr;
                    if (other && other != sc && breaksOf(other) == mine) {
                        return true;
                    }
                }
                return false;
            };
            const engraving::Score* from = nullptr;
            INotationPtr fromBook;
            starscore::LayoutCopyOptions options;
            if (arrScore && formatted(arrScore) && !copiedFromAnother(arrScore)) {
                from = arrScore;
            } else {
                int most = 0;
                for (const QString& pid : file.partIds) {
                    auto b = bookForPart.find(pid);
                    const engraving::Score* es = b != bookForPart.end() && b->second->notation()
                                                 ? b->second->notation()->elements()->msScore() : nullptr;
                    int breaks = 0;
                    for (const engraving::Measure* m = es ? es->firstMeasure() : nullptr; m; m = m->nextMeasure()) {
                        breaks += m->lineBreak() ? 1 : 0;
                    }
                    if (breaks > most) {
                        most = breaks;
                        from = es;
                        fromBook = b->second->notation();
                    }
                }
                options.pageBreaks = false;
                options.keepTogether = false;
                options.systemLocks = false;
            }
            if (from) {
                p->masterNotation()->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Score systems"));
                if (from != arrScore && !cs->systemLocks()->allLocks().empty()) {
                    engraving::EditSystemLocks::undoRemoveAllLocks(cs);   // only the part's system breaks decide
                }
                starscore::copyLayout(from, { cs }, options);
                // ... and the same bars on each system as that part (Joel, 5 Oct 2026): the Score ends a system
                // wherever the part's laid-out page does, including systems the part makes with system locks or by
                // itself (Top Hat's Trumpet from J on: four bars a system, set with locks)
                if (fromBook) {
                    engraving::Score* fs = fromBook->elements()->msScore();
                    if (fromBook->painting()->viewMode() != ViewMode::PAGE) {
                        fromBook->painting()->setViewMode(ViewMode::PAGE);
                    } else {
                        fs->doLayout();
                    }
                    std::set<int> systemEnds;
                    for (const engraving::Page* page : fs->pages()) {
                        for (const engraving::System* sys : page->systems()) {
                            const engraving::Measure* last = sys->lastMeasure();
                            if (last) {
                                systemEnds.insert(last->endTick().ticks());
                            }
                        }
                    }
                    for (engraving::Measure* m = cs->firstMeasure(); m && m->nextMeasure(); m = m->nextMeasure()) {
                        if (systemEnds.count(m->endTick().ticks()) && !m->lineBreak() && !m->pageBreak()) {
                            m->undoSetBreak(true, engraving::LayoutBreakType::LINE);
                        }
                    }
                    refSystemEnds = systemEnds;
                    refPartName = fs->name().toQString();
                }
                p->masterNotation()->notation()->undoStack()->commitChanges();
            }
            lap("systems");
        }
        if (!reuse) {
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
                        cs->doLayout();   // (judged on the layout so far, as when MuseScore lays out after each edit)
                        p->masterNotation()->parts()->setStaffVisible(st->id(), true);
                    }
                }
            }
            lap("visibility and names");
            // kept as it is now, for the folder's next Scores
            if (layout.valid) {
                scoreCopy = ScoreCopy();
                scoreCopy.folder = scoreFolder;
                scoreCopy.partIds = file.partIds;
                scoreCopy.p = p;
                scoreCopy.refSystemEnds = refSystemEnds;
                scoreCopy.refPartName = refPartName;
                for (const engraving::Measure* m = cs->firstMeasure(); m; m = m->nextMeasure()) {
                    scoreCopy.bars.emplace_back(m->userStretch(), m->lineBreak(), m->pageBreak());
                }
                scoreCopy.numberPos = cs->style().styleV(engraving::Sid::measureNumberPosAbove).value<PointF>();
                scoreCopy.spatium = cs->style().spatium();
            }
        }
        // Concert pitch, or written pitch for the transposing score; measure numbers a space higher, clear of the
        // brackets' hooks (Bumper Cars' bar 6 ran into the bracket)
        {
            INotationPtr n = p->masterNotation()->notation();
            // a Flexible Score's version: each chair's transposition and clef (the bottom chair is the last)
            if (!file.flexibleScoreKey.isEmpty()) {
                const QString& key = file.flexibleScoreKey;
                const int dia = key == "Bb" ? -1 : key == "Eb" ? -5 : 0;
                const int chrom = key == "Bb" ? -2 : key == "Eb" ? -9 : 0;
                for (int i = 0; i < file.partIds.size(); ++i) {
                    engraving::Part* part = cs->partById(ID(file.partIds.at(i)));
                    if (!part) {
                        continue;
                    }
                    const bool bottom = i == file.partIds.size() - 1;
                    const int clef = key == "Bass" ? (bottom ? 1 : 5)
                                     : key == "C" ? (bottom ? 1 : 0)
                                     : (bottom ? 4 : 0);
                    starscoreRewritePartInstrument(p->masterNotation(), part, dia, chrom, clef, false);
                }
            }
            // a section Score's B♭, E♭, treble-clef or bass-clef version: each one-staff pitched instrument rewritten
            if (!file.scoreKey.isEmpty()) {
                for (const QString& pid : file.partIds) {
                    engraving::Part* part = cs->partById(ID(pid));
                    if (!part || part->nstaves() != 1 || part->instrument()->useDrumset()
                        || !part->staves().front()->isPitchedStaff(engraving::Fraction(0, 1))) {
                        continue;
                    }
                    const StarScoreScoreVersion v = starscoreScoreVersion(part, file.scoreKey);
                    starscoreRewritePartInstrument(p->masterNotation(), part, v.diatonic, v.chromatic, v.clef, false);
                }
            }
            lap("pitch");
            // a style change goes through the laid-out systems, so the copy is laid out once after the edits above
            cs->doLayout();
            lap("layout");
            if (layout.valid) {
                const PointF pos = cs->style().styleV(engraving::Sid::measureNumberPosAbove).value<PointF>();
                n->undoStack()->prepareChanges(TranslatableString::untranslatable("Measure numbers"));
                cs->undoChangeStyleVal(engraving::Sid::measureNumberPosAbove, PointF(pos.x(), pos.y() - 1.0));
                n->undoStack()->commitChanges();
            }
            n->undoStack()->prepareChanges(TranslatableString::untranslatable("Score pitch"));
            n->style()->setStyleValue(StyleId::concertPitch, !file.transposingScore && file.flexibleScoreKey.isEmpty()
                                      && file.scoreKey.isEmpty());
            // multimeasure rests on every Score (Joel, 6 Oct 2026)
            n->style()->setStyleValue(StyleId::createMultiMeasureRests, true);
            n->undoStack()->commitChanges();
        }
        lap("prepared");
        // every system on its page: big Scores (orchestra, marching band) at a smaller staff size
        starscoreFitSystemsOnPages(p->masterNotation()->notation());
        lap("fit");
        // Repeated sections kept on one page where they fit (titled first: the label can move the composer credit)
        if (layout.valid) {
            INotationPtr n = p->masterNotation()->notation();
            n->undoStack()->prepareChanges(TranslatableString::untranslatable("Sheet title"));
            starscoreRetitleTexts(cs, file.sheetLeft, file.sheetRight);
            n->undoStack()->commitChanges();
            // Systems taken from a part: two systems become one where both fit on it whole (Joel, 5 Oct 2026). Worked
            // out once and kept in the song until the part's systems or the song's bars change.
            if (!refSystemEnds.empty()) {
                QStringList keyParts { refPartName };
                for (int t : refSystemEnds) {
                    keyParts << QString::number(t);
                }
                for (const engraving::Part* part : cs->parts()) {
                    if (file.partIds.contains(idText(part))) {
                        keyParts << idText(part);
                    }
                }
                keyParts << starscoreSystemStructureKey(cs);
                const QString key = QString::fromLatin1(QCryptographicHash::hash(keyParts.join("|").toUtf8(),
                                                                                  QCryptographicHash::Md5).toHex());
                Data stored = loadFrom(ms);
                // one set of systems for a folder's six Scores, worked out on the Concert Score (written first)
                const QString systemsKey = starscoreSectionFolderOf(file.relativePath) + "/Scores";
                const QJsonObject cached = stored.scoreSystems.value(systemsKey).toObject();
                if (cached.value("key").toString() == key) {
                    std::vector<int> ends;
                    for (const QJsonValue& v : cached.value("breaks").toArray()) {
                        ends.push_back(v.toInt());
                    }
                    starscoreSetSystemBreaks(n, ends);
                } else {
                    const std::vector<int> ends = starscoreJoinSystems(n);
                    QJsonArray arr;
                    for (int t : ends) {
                        arr.append(t);
                    }
                    stored.scoreSystems[systemsKey] = QJsonObject { { "key", key }, { "breaks", arr } };
                    storeTo(ms, stored, project);
                }
            }
        lap("joins");
            // bars run onto an extra system before a break: tighter spacing where it saves the system
            starscoreTightenWrappedSystems(n);
            lap("tighten");
            starscoreKeepRepeatsOnOnePage(n, true);   // (the tightening leaves it laid out)
        }
        lap("repeats");
        // bar numbers clear of the brackets' hooks, measured on the page as it now is (laid out by the steps above)
        starscoreClearBarNumbersOfBrackets(p->masterNotation()->notation(), layout.valid);
        // The composer credit at its house position (last line on the subtitle's baseline), measured in page
        // view: the main score's style can carry a credit moved too far (1.15.7's Apply Styles measured it in
        // continuous view and printed Bumper Cars' credit in the music). The arrangement top right, as on the parts.
        p->masterNotation()->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Composer credit"));
        lap("bar numbers");
        const Ret ret = starscorePrintSheet(writer, p->masterNotation()->notation(), file.sheetLeft, file.sheetRight,
                                            !file.sheetLeft.isEmpty() || !file.sheetRight.isEmpty(), pdf);
        lap("print");
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

    int fileNumber = 0;
    for (const StarScoreBandFile& file : plan.files) {
        QByteArray pdf;
        Ret ret;
        // where the export is: in the log (a crash can be traced to the sheet being made) and in the export window
        reportExportProgress(muse::qtrc("starscore", "Sheets"), fileNumber++, int(plan.files.size()), file.relativePath);

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

        const QString target = songDir + "/" + starscoreVariantPath(file.relativePath, variant);
        // the same pages as the file already there (a re-export with nothing changed in this sheet):
        // the existing file stays, and nothing is archived
        if (QFileInfo::exists(target) && starscoreSamePdf(pdf, target)) {
            unchanged << file.relativePath;
            continue;
        }
        // "Update all sheets" first only finds out which sheets would come out differently, writing nothing
        if (m_exportDryRun) {
            m_dryRunChanged << file.relativePath;
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
        // (a Half-Time or Double-Time sheet simply replaces the one there: it has no versions of its own)
        if (variant.isEmpty() && !supersede(file.relativePath, &archivedTo)) {
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

    // a dry run ends here: nothing archived, recorded, or written beside the sheets; so does a Half-Time or Double-Time
    // export (its sheets aren't in the sheet record, the organizer or the to-do list)
    if (m_exportDryRun || !variant.isEmpty()) {
        QDir(tmpDir).removeRecursively();
        if (masterChanged) {
            master->notation()->notationChanged().notify();
        }
        for (const INotationPtr& n : touchedBooks) {
            n->notationChanged().notify();
        }
        if (!variant.isEmpty()) {
            // (a sheet that couldn't be made is reported, the others are there)
            QString line = muse::qtrc("starscore", "%1: wrote %2 PDF(s), %3 the same as before.").arg(variant).arg(written.size())
                           .arg(unchanged.size());
            if (!problems.isEmpty()) {
                line += " " + muse::qtrc("starscore", "Not made: %1").arg(problems.join("; "));
            }
            return RetVal<QString>::make_ok(line);
        }
        if (!problems.isEmpty()) {
            return RetVal<QString>::make_ret(Ret::Code::UnknownError, problems.join("; ").toStdString());
        }
        return RetVal<QString>::make_ok(QString("%1 would change, %2 the same").arg(m_dryRunChanged.size()).arg(unchanged.size()));
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
        // A horn folder under its older name ("3H Tpt Flu Ten" for a "3H Tpt Pic Ten" whose piccolo was labelled a
        // flute before 1.17.1): its sheets are archived once the new folder's are written, and it goes when empty
        for (const auto& [oldFolder, folder] : plan.renamedFolders) {
            const QDir oldDir(songDir + "/" + oldFolder);
            const bool newThere = std::any_of(current.begin(), current.end(), [&](const QString& rel) {
                return rel.startsWith(folder + "/") && QFileInfo::exists(songDir + "/" + rel);
            });
            if (oldFolder == folder || !oldDir.exists() || !newThere) {
                continue;
            }
            for (const QString& fileName : oldDir.entryList({ "*.pdf", "*.PDF" }, QDir::Files)) {
                supersede(oldFolder + "/" + fileName);
            }
            // (anything else left in it keeps the folder, untouched)
            QDir(songDir).rmdir(oldFolder);
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
            // (1H: every sheet, as the saxophones' names changed in 1.18.11)
            const QString pattern = folder == "1H" ? plan.code + " - *.pdf" : plan.code + " - Horn *.pdf";
            for (const QString& fileName : dir.entryList({ pattern }, QDir::Files)) {
                const QString rel = folder + "/" + fileName;
                if (!current.contains(rel)) {
                    supersede(rel);
                }
            }
        }
        // sheets StarScore renamed (Last Pint's "PINT - Accordion.pdf", now "PINT - Keys.pdf"): the old one archived once
        // the new one is written
        for (const StarScoreBandFile& f : full.files) {
            if (!f.formerPath.isEmpty() && !current.contains(f.formerPath) && QFileInfo::exists(songDir + "/" + f.relativePath)
                && QFileInfo::exists(songDir + "/" + f.formerPath)) {
                supersede(f.formerPath);
            }
        }
        // Scores under their names from before 1.18.17 ("CODE - Score.pdf", "CODE - Score (Transposing).pdf", the Flexible
        // "CODE - Score (Bb).pdf"…): archived once the folder's new Scores ("Concert Score"…) are written
        {
            std::set<QString> scoreFolders;   // the section folders (above "Section Scores")
            for (const StarScoreBandFile& f : plan.files) {
                if (f.isScore && QFileInfo::exists(songDir + "/" + f.relativePath)) {
                    scoreFolders.insert(starscoreSectionFolderOf(f.relativePath));
                }
            }
            for (const QString& folder : scoreFolders) {
                const QDir dir(songDir + "/" + folder);
                // (and the 1.18.18 names, "CODE - Concert Score.pdf"…, and the 1.18.19 ones at the top of the folder,
                // "CODE - Section Score (Concert).pdf", left over when the moved one was already there)
                QStringList names { plan.code + " - Score.pdf", plan.code + " - Score (*).pdf",
                                    plan.code + " - Section Score (*).pdf" };
                for (const QString& type : { "Concert", "Transposing", "Bb", "Eb", "Treble Clef", "Bass Clef" }) {
                    names << plan.code + " - " + type + " Score.pdf";
                }
                for (const QString& fileName : dir.entryList(names, QDir::Files)) {
                    const QString rel = folder + "/" + fileName;
                    if (!current.contains(rel)) {
                        supersede(rel);
                    }
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
            if (before.isEmpty()) {
                // the same sheet in the folder's older name (the piccolo's sheet was "… - Flute.pdf" there)
                for (const auto& [oldFolder, folder] : plan.renamedFolders) {
                    // (the new Flute version is new, not the old folder's "Flute", which was the piccolo)
                    if (!f.relativePath.startsWith(folder + "/") || f.relativePath.endsWith(" - Flute.pdf")) {
                        continue;
                    }
                    QString old = oldFolder + "/" + f.relativePath.mid(folder.size() + 1);
                    if (old.endsWith(" - Piccolo.pdf")) {
                        old.replace(" - Piccolo.pdf", " - Flute.pdf");
                    }
                    before = sigs.value(old).toObject();
                    break;
                }
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
        // sheets no longer in the plan (a folder renamed, a section deleted) are forgotten: until 1.18.1 they stayed and
        // every later export was suggested as a major version ("exported before but not in this export")
        {
            std::set<QString> planned;
            for (const StarScoreBandFile& f : full.files) {
                planned.insert(f.relativePath);
            }
            for (auto it = sigs.begin(); it != sigs.end();) {
                it = planned.count(it.key()) ? std::next(it) : sigs.erase(it);
            }
        }
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
//! Any other song (new ones) starts at 0.0.0, so its first export is 1.0.0 (or 0.1.0 / 0.0.1 for a work in progress).
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

std::vector<StarScoreFlexibleSheet> StarScoreService::flexibleSheets(const QString& sectionId) const
{
    std::vector<StarScoreFlexibleSheet> out;
    const engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return out;
    }
    for (const StarScoreSection& s : load().sections) {
        static const QRegularExpression anyRe("^\\d+-horn-any$");
        if (s.id != sectionId || !anyRe.match(s.templateKey).hasMatch()) {
            continue;
        }
        const std::vector<std::pair<QString, int> > chairs = starscoreFlexibleChairs(ms, s);
        for (const auto& [pid, number] : chairs) {
            for (const StarScoreSeat& seat : starscoreFlexibleSeats(int(chairs.size()), number)) {
                StarScoreFlexibleSheet sheet;
                sheet.name = starscoreSeatSheetName(number, seat);
                sheet.chairPartId = pid;
                sheet.instrumentId = QString::fromUtf8(seat.instrumentId);
                sheet.clef = seat.clef;
                auto own = s.sheetParts.find(sheet.name);
                if (own != s.sheetParts.end() && ms->partById(ID(own->second))) {
                    sheet.partId = own->second;
                }
                out.push_back(sheet);
            }
        }
    }
    return out;
}

//! Sheets StarScore has moved or renamed since they were exported (1.18.19: the Scores' names; 1.18.22: Scores into
//! "Section Scores", Flexible horns' sheets into "Horn 1"…): the file already there is moved to its new place rather
//! than made again (no new version, nothing archived), and its sheet record entry and the folder colours' lists
//! follow it. Returns how many moved.
int StarScoreService::moveSheetsToCurrentPaths(const StarScoreBandExportPlan& plan)
{
    if (plan.bandFolder.isEmpty() || plan.songFolder.isEmpty()) {
        return 0;
    }
    const QString songDir = plan.bandFolder + "/" + plan.songFolder;
    std::vector<std::pair<QString, QString> > moved;   // old -> new, relative to the song folder
    for (const StarScoreBandFile& f : plan.files) {
        const QString to = songDir + "/" + f.relativePath;
        if (QFileInfo::exists(to)) {
            continue;   // already in place (a file left at an old path is archived after the export)
        }
        for (const QString& old : starscoreFormerPathsOf(f)) {
            const QString from = songDir + "/" + old;
            if (!QFileInfo::exists(from)) {
                continue;
            }
            QDir().mkpath(QFileInfo(to).absolutePath());
            if (QFile::rename(from, to)) {
                moved.push_back({ old, f.relativePath });
                LOGI() << "[starscore] moved " << old << " -> " << f.relativePath;
            } else {
                LOGW() << "[starscore] couldn't move " << old << " -> " << f.relativePath;
            }
            break;
        }
    }
    if (moved.empty()) {
        return 0;
    }
    // the sheet record: every mention of the old path (its entry, and the lists of sheets each folder colour needs)
    const QString recordPath = plan.bandFolder + "/6 Inbox/.organizer/sheets/" + plan.code + ".json";
    QFile in(recordPath);
    if (in.open(QIODevice::ReadOnly)) {
        QString text = QString::fromUtf8(in.readAll());
        in.close();
        for (const auto& [old, now] : moved) {
            text.replace("\"" + old + "\"", "\"" + now + "\"");
        }
        if (!QJsonDocument::fromJson(text.toUtf8()).isNull()) {
            QSaveFile out(recordPath);
            if (out.open(QIODevice::WriteOnly)) {
                out.write(text.toUtf8());
                out.commit();
            }
        }
    }
    return int(moved.size());
}

StarScoreVersionSuggestion StarScoreService::suggestVersionBump(const StarScoreBandExportPlan& plan) const
{
    StarScoreVersionSuggestion out;
    INotationProjectPtr project = exportSourceProject();
    if (!project || plan.newSong || plan.files.empty()) {
        return out;
    }
    engraving::MasterScore* ms = project->masterNotation()->masterScore();
    const Data data = loadFrom(ms);
    const QJsonObject sigs = data.exportSignatures;
    // a new song (0.0.0): its first export is its first release, 1.0.0 (0.1.0 or 0.0.1 can be picked for a work in progress)
    if (scoreVersion() == "0.0.0") {
        out.bump = 1;
        out.reasons << muse::qtrc("starscore", "first export of this song (pick Minor version or Patch for a work in progress)");
        return out;
    }
    if (sigs.isEmpty()) {
        out.reasons << muse::qtrc("starscore", "First export with change tracking: the version stays as it is.");
        return out;
    }

    // the lead sheet's parts: its sheets count toward the "song changed" rule
    QStringList leadParts;
    for (const StarScoreSection& s : data.sections) {
        if (s.templateKey == "lead-sheet") {
            leadParts << s.partIds;
        }
    }

    QStringList majorWhy, minorWhy;
    QStringList newSheets, changedSheets;
    int barsBefore = -1, barsNow = -1;
    bool marksChanged = false, formChanged = false;
    int leadChanged = 0, leadBars = 0;
    std::set<QString> seen;   // signature keys the plan still has
    auto sheetName = [&](const QString& rel) {
        QString name = rel.section('/', -1);
        if (name.startsWith(plan.code + " - ")) {
            name.remove(0, plan.code.size() + 3);
        }
        if (name.endsWith(".pdf")) {
            name.chop(4);
        }
        return rel.section('/', 0, -2) + " / " + name;
    };
    // sheets the export dialog starts unticked (Percussion, ones unticked before) aren't part of this export
    const QStringList unticked = bandExportUnticked(plan.code);
    for (const StarScoreBandFile& f : plan.files) {
        if (!f.sourceFile.isEmpty()) {
            continue;
        }
        seen.insert(f.relativePath);
        if (f.defaultUnchecked || unticked.contains(f.relativePath)) {
            continue;
        }
        const QJsonObject before = sigs.value(f.relativePath).toObject();
        if (before.isEmpty()) {
            // the Flexible version sheets of one chair share its music: one "new" per chair
            static const QRegularExpression chairRe("^(.* / Horn \\d+)");
            QString name = sheetName(f.relativePath);
            const QRegularExpressionMatch m = chairRe.match(name);
            if (f.isVersion && m.hasMatch()) {
                name = m.captured(1);
            }
            if (!newSheets.contains(name)) {
                newSheets << name;
            }
            continue;
        }
        const QJsonObject now = organizerSignature(ms, f.partIds);
        const QJsonArray a = before.value("bars").toArray(), b = now.value("bars").toArray();
        if (barsNow < 0) {
            barsNow = b.size();
        }
        if (barsBefore < 0 && a.size() != b.size()) {
            barsBefore = a.size();
        }
        if (before.value("marks").toObject() != now.value("marks").toObject()) {
            marksChanged = true;
        }
        if (before.contains("form") && before.value("form") != now.value("form")) {
            formChanged = true;
        }
        int diff = 0;
        const int n = std::max(a.size(), b.size());
        for (int i = 0; i < n; ++i) {
            if (i >= a.size() || i >= b.size() || a[i] != b[i]) {
                ++diff;
            }
        }
        if (diff > 0) {
            changedSheets << sheetName(f.relativePath);
            if (!f.isScore && std::all_of(f.partIds.begin(), f.partIds.end(), [&](const QString& pid) { return leadParts.contains(pid); })) {
                leadChanged = std::max(leadChanged, diff);
                leadBars = std::max(leadBars, n);
            }
        }
    }
    // A sheet exported before and not planned now counts only while its file is still in the song folder: one already
    // archived (its folder renamed, as "3H Tpt Flu Ten" became "3H Tpt Pic Ten") went in an earlier version
    QStringList gone;
    const QString songDir = plan.bandFolder + "/" + plan.songFolder;
    for (auto it = sigs.begin(); it != sigs.end(); ++it) {
        if (!seen.count(it.key()) && QFileInfo::exists(songDir + "/" + it.key())) {
            gone << sheetName(it.key());
        }
    }

    if (barsBefore >= 0) {
        majorWhy << (barsNow > barsBefore
                     ? muse::qtrc("starscore", "the song is %n bar(s) longer (%1, was %2)", nullptr, barsNow - barsBefore).arg(barsNow).arg(barsBefore)
                     : muse::qtrc("starscore", "the song is %n bar(s) shorter (%1, was %2)", nullptr, barsBefore - barsNow).arg(barsNow).arg(barsBefore));
    }
    if (marksChanged) {
        majorWhy << muse::qtrc("starscore", "the rehearsal marks changed");
    }
    if (formChanged) {
        majorWhy << muse::qtrc("starscore", "key or time signatures changed");
    }
    if (leadBars > 0 && leadChanged * 4 >= leadBars) {
        majorWhy << muse::qtrc("starscore", "the lead sheet changed in %1 of its %2 bars").arg(leadChanged).arg(leadBars);
    }
    if (!gone.isEmpty()) {
        majorWhy << muse::qtrc("starscore", "exported before but not in this export: %1").arg(gone.mid(0, 3).join(", ")
                                                                                             + (gone.size() > 3 ? "…" : ""));
    }
    if (!changedSheets.isEmpty()) {
        minorWhy << muse::qtrc("starscore", "notes, chords, dynamics or slurs changed in %n sheet(s): %1", nullptr, changedSheets.size())
            .arg(changedSheets.mid(0, 4).join(", ") + (changedSheets.size() > 4 ? "…" : ""));
    }
    if (!newSheets.isEmpty()) {
        minorWhy << muse::qtrc("starscore", "new: %1").arg(newSheets.mid(0, 4).join(", ") + (newSheets.size() > 4 ? "…" : ""));
    }

    if (!majorWhy.isEmpty()) {
        out.bump = 1;
        out.reasons = majorWhy + minorWhy;
    } else if (!minorWhy.isEmpty()) {
        out.bump = 2;
        out.reasons = minorWhy;
    } else {
        out.bump = 3;
        out.reasons << muse::qtrc("starscore", "no change to the music since the last export: layout, text and style fixes only "
                                               "(untick to keep the version)");
    }
    return out;
}

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
    return existingSong ? QString("4.0.0") : QString("0.0.0");
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

    // Each edit's end lays out every open score (the main score and each open part score): a new version number
    // on Branston Pickle's 53 scores, with their tabs open, meant thousands of layouts, 9 min 20 s before every
    // export (which sets the version first). Each score is laid out once, on its own, and only when its footer
    // changed: 35 s.
    auto update = [&](INotationPtr n) {
        if (!n) {
            return;
        }
        engraving::Score* score = n->elements()->msScore();
        score->lockUpdates(true);
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("Version"));
        const bool changed = starscore::applyVersionFooter(score, version);
        n->undoStack()->commitChanges();
        score->lockUpdates(false);
        if (changed) {
            score->setLayoutAll();
            score->doLayout();
        }
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

// ---------------------------------------------------------------------------
//  Half-Time and Double-Time sheets (Joel, 7 Oct 2026: "export half-time version?" and "export double-time version?"
//  for each song, exported alongside the standard sheets)
//
//  The song as it is now is saved to a temporary copy (under its own file name, so the export finds the same song
//  folder and code), the copy is loaded out of sight, rewritten at half or twice its note lengths (convertTimeOf),
//  and exported like the song itself, each sheet into a "Half-Time" or "Double-Time" subfolder of its part folder.
//  Those sheets have no versions, sheet record or changelog of their own: a sheet there is simply replaced. The open
//  song is never changed.
// ---------------------------------------------------------------------------

RetVal<QString> StarScoreService::exportTimeVariants(const QStringList& onlyPaths, bool halfTime, bool doubleTime)
{
    INotationProjectPtr project = exportSourceProject();
    if (!project || (!halfTime && !doubleTime)) {
        return RetVal<QString>::make_ok(QString());
    }
    const QString fileName = QFileInfo(project->path().toQString()).fileName();
    QStringList lines;
    QStringList problems;
    for (const bool toDouble : { false, true }) {
        if ((toDouble && !doubleTime) || (!toDouble && !halfTime)) {
            continue;
        }
        const QString variant = toDouble ? QString("Double-Time") : QString("Half-Time");
        reportExportProgress(variant, 0, 1, muse::qtrc("starscore", "Converting the song"));
        const QString dir = QDir::tempPath() + "/StarScoreTime-" + QUuid::createUuid().toString(QUuid::Id128);
        QDir().mkpath(dir);
        const QString copyPath = dir + "/" + fileName;
        Ret ret = project->save(io::path_t(copyPath), SaveMode::SaveCopy, false);
        INotationProjectPtr copy;
        if (ret) {
            copy = projectCreator()->newProject(iocContext());
            ret = copy->load(io::path_t(copyPath));
        }
        if (!ret || !copy || !copy->masterNotation()) {
            problems << muse::qtrc("starscore", "%1: the song couldn't be copied (%2).").arg(variant, QString::fromStdString(ret.toString()));
            QDir(dir).removeRecursively();
            continue;
        }
        const RetVal<QString> converted = convertTimeOf(copy, toDouble, true);
        LOGI() << "[starscore] " << variant << ": " << converted.val;
        if (!converted.ret) {
            problems << muse::qtrc("starscore", "%1: the song couldn't be converted (%2).").arg(variant, QString::fromStdString(converted.ret.toString()));
            QDir(dir).removeRecursively();
            continue;
        }
        m_exportSource = copy;
        m_exportVariant = variant;
        const RetVal<QString> done = exportToBandFolder(onlyPaths);
        m_exportSource.reset();
        m_exportVariant.clear();
        copy.reset();
        QDir(dir).removeRecursively();
        if (done.ret) {
            lines << done.val;
        } else {
            problems << muse::qtrc("starscore", "%1: %2").arg(variant, QString::fromStdString(done.ret.toString()));
        }
    }
    endExportProgress();
    if (!problems.isEmpty()) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, (lines + problems).join("\n").toStdString());
    }
    return RetVal<QString>::make_ok(lines.join("\n"));
}
