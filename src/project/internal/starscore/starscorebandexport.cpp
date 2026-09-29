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

#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationpainting.h"
#include "notation/inotationparts.h"
#include "notation/inotationstyle.h"
#include "notation/inotation.h"

#include "starscoreengraving.h"
#include "starscorehouse.h"

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
static QString starscoreTitleFrameText(const engraving::MasterScore* ms)
{
    const engraving::MeasureBase* first = ms ? ms->first() : nullptr;
    if (!first || !first->isVBox()) {
        return QString();
    }
    for (engraving::EngravingItem* e : first->el()) {
        if (e && e->isText() && engraving::toText(e)->textStyleType() == engraving::TextStyleType::TITLE) {
            return engraving::toText(e)->plainText().toQString().simplified();
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
                if (sec.status == StarScoreStatus::Finished && !sec.skipSheets.isEmpty()) {
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
                    if (!kind.isEmpty() && sec.skipSheets.contains(kind)) {
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

            struct Version {
                const char* suffix;
                int dia;
                int chrom;
                int clef;
            };
            static const std::vector<Version> HIGH = {
                { " in Bb", -1, -2, 0 }, { " in Eb", -5, -9, 0 }, { " in C", 0, 0, 0 },
            };
            static const std::vector<Version> MIDDLE = {
                { " in Bb", -1, -2, 0 }, { " in Bb (Tenor Sax)", -8, -14, 0 }, { " in Eb", -5, -9, 0 }, { " in C", 0, 0, 0 },
                { " (Alto Clef)", 0, 0, 2 },
            };
            static const std::vector<Version> LOW = {
                { " in Bb", -8, -14, 0 }, { " in Eb", -12, -21, 0 }, { " in C", -7, -12, 0 }, { " (Bass Clef)", 0, 0, 1 },
            };

            auto addVersion = [&](const QString& pid, const QString& name, const Version& v) {
                StarScoreBandFile f;
                f.relativePath = folder + "/" + code + " - " + starscoreSafeFileName(name) + ".pdf";
                f.partIds = { pid };
                f.isVersion = true;
                f.transposeDiatonic = v.dia;
                f.transposeChromatic = v.chrom;
                f.clef = v.clef;
                f.header = arr + name;
                plan.files.push_back(f);
            };

            for (const auto& [pid, number] : chairs) {
                const std::vector<Version>& versions = (number == 1 && horns > 1) ? HIGH
                                                       : (number >= horns && horns > 1) ? LOW : MIDDLE;
                const QString chairName = QString("Horn %1").arg(number);
                for (const Version& v : versions) {
                    addVersion(pid, chairName + v.suffix, v);
                }
                if (number == 1 && !flutePid.isEmpty()) {
                    addVersion(flutePid, "Horn 1 for C Flute", Version { "", 0, 0, 0 });
                }
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
            for (const auto& h : horns) {
                counts[h.second]++;
            }
            QStringList codes;
            for (const auto& [name, abbr] : STARSCORE_HORN_ORDER) {
                if (counts.count(name)) {
                    codes << (counts[name] > 1 ? QString::number(counts[name]) : QString()) + abbr;
                }
            }
            const QString folder = QString("%1H %2").arg(horns.size()).arg(codes.join(' '));
            addFile(folder, "Score", sec.partIds, true);

            std::map<QString, int> seen;
            for (const auto& [pid, horn] : horns) {
                QString name = horn;
                if (counts[horn] > 1) {
                    name += QString(" %1").arg(++seen[horn]);
                }
                addFile(folder, name, { pid }, false);
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

    return RetVal<StarScoreBandExportPlan>::make_ok(plan);
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

        if (file.isVersion) {
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
            ret = writePdf(it->second->notation(), tmpPdf);
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
