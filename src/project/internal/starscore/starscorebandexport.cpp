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

#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationpainting.h"
#include "notation/inotationparts.h"
#include "notation/inotationstyle.h"
#include "notation/inotation.h"

#include "starscoreengraving.h"

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
    std::map<QString, QString> folderToCode;
    {
        QFile f(plan.bandFolder + "/6 Inbox/.organizer/codes.json");
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            for (auto it = o.begin(); it != o.end(); ++it) {
                folderToCode[it.key()] = it.value().toString();
            }
        }
    }

    const QString fileBase = QFileInfo(project->path().toQString()).completeBaseName();
    QString title = project->metaInfo().title.trimmed();
    QString codeFromName;
    const QRegularExpressionMatch codeMatch = QRegularExpression("^([A-Z]{3,4})\\s*-\\s*(.*)$").match(fileBase);
    if (codeMatch.hasMatch()) {
        codeFromName = codeMatch.captured(1);
        if (title.isEmpty()) {
            title = codeMatch.captured(2).trimmed();
        }
    }
    if (title.isEmpty()) {
        title = fileBase;
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
            if (plainName(folder) == title.toLower()) {
                plan.songFolder = folder;
                plan.code = code;
                break;
            }
        }
    }
    if (plan.songFolder.isEmpty()) {
        return RetVal<StarScoreBandExportPlan>::make_ret(
            Ret::Code::UnknownError,
            muse::qtrc("starscore", "Couldn't find “%1” in Sheets and Demos. Name the file “CODE - %1.starscore” "
                                    "with the song's code from 6 Inbox/.organizer/codes.json, or add the song there first.")
            .arg(title).toStdString());
    }

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
                QString name = starscoreRhythmName(p->instrumentId().toQString(), p->partName().toQString());
                if (counts[name] > 1) {
                    name += " (" + p->partName().toQString() + ")";
                }
                addFile("1 Rhythm", name, { pid }, false);
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
