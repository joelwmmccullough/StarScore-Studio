/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Solo Transcriptions (1.24.0)
 *
 * File › New › Solo Transcription (Joel, 3 Nov 2026): a transcribed solo in its own .starscore, kept in
 * "Projects and Sheets/8 Solo Transcriptions/<Player> - <Song>/". The main score holds the one solo part (in concert
 * pitch); twelve part scores of that part show it in each key, the original first. What's written in the score shows
 * in all twelve (linked notes); fingerings stay in the part score they're written in (Score::undoAddElement). Each
 * key is transposed up or down, whichever keeps the notes nearest the staff and inside the piano's 88 keys.
 *
 * MuseScore copies a linked note's spelling, key signatures' written key and chord symbols from wherever an edit is
 * made, as if all linked staves shared one transposition. Each part score here has its own (its instrument's
 * transposition is the key's shift), so after every change, and before every save, refreshSoloTranscription()
 * spells each part score's notes, key signatures and chord symbols for its own key again, picks each key's octave,
 * and copies the main score's breaks, system locks, bar widths and style into the twelve.
 */
#include "starscoreservice.h"
#include "starscoreengraving.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QDate>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>
#include <QFile>
#include <QUrl>

#include "engraving/dom/chord.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/editing/transpose.h"
#include "engraving/style/style.h"
#include "engraving/style/defaultstyle.h"
#include "engraving/dom/systemlock.h"
#include "engraving/dom/clef.h"
#include "engraving/dom/accidental.h"
#include "engraving/dom/notedot.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/fingering.h"

#include "log.h"
#include "translation.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;

namespace {
//! The metaTag that marks a Solo Transcription (read by the engraving code too: fingerings stay local)
const char16_t* SOLO_TAG = u"starscoreSoloTranscription";

//! The major key a key signature lands on a number of semitones up (-6 … 5 fifths: G♭ rather than F♯)
int soloKeyAfter(int fifths, int semitones)
{
    if (semitones % 12 == 0) {
        return fifths;
    }
    int f = ((fifths + 7 * semitones) % 12 + 12) % 12;
    return f > 5 ? f - 12 : f;
}

QString soloKeyName(int fifths)
{
    static const char* NAMES[15] = { "C♭", "G♭", "D♭", "A♭", "E♭", "B♭", "F", "C", "G", "D", "A", "E", "B", "F♯", "C♯" };
    return QString::fromUtf8(NAMES[std::clamp(fifths, -7, 7) + 7]);
}

//! Letter steps from one key's tonic up to another's (0 … 6)
int soloLetterSteps(int fromFifths, int toFifths)
{
    const int a = mu::engraving::tpc2step(mu::engraving::Tpc::TPC_C + fromFifths);
    const int b = mu::engraving::tpc2step(mu::engraving::Tpc::TPC_C + toFifths);
    return ((b - a) % 7 + 7) % 7;
}

//! The instrument transposition that shows concert pitch `shift` semitones higher (lower when negative), spelled
//! in the target key: written = concert + shift, so the instrument sounds `shift` below what's written. The letter
//! steps are the ones nearest the shift's size (C♯ up 11 to C: 7 steps; C♭ up 1 to C: none).
mu::engraving::Interval soloIntervalTo(int originalFifths, int targetFifths, int shift)
{
    const int s = soloLetterSteps(originalFifths, targetFifths);
    const double size = shift * 7.0 / 12.0;
    int steps = s;
    for (int c : { s - 7, s + 7, s - 14, s + 14 }) {
        if (std::abs(c - size) < std::abs(steps - size)) {
            steps = c;
        }
    }
    return mu::engraving::Interval(-steps, -shift);
}

mu::engraving::Interval soloInterval(int originalFifths, int keyIndex, int shift)
{
    return soloIntervalTo(originalFifths, soloKeyAfter(originalFifths, keyIndex), shift);
}

//! The key `semitones` above a key, as a key signature of at most 6 flats or 5 sharps (G♭ rather than F♯, D♭ rather
//! than C♯): a tenor sax's written key for concert B is D♭, not C♯
int soloKeyAbove(int fifths, int semitones)
{
    int f = ((fifths + 7 * semitones) % 12 + 12) % 12;
    return f > 5 ? f - 12 : f;
}

//! The pitch in the middle of a staff with this clef (written)
int soloClefMiddle(mu::engraving::ClefType c)
{
    using mu::engraving::ClefType;
    switch (c) {
    case ClefType::G: case ClefType::G_1: return 71;
    case ClefType::G8_VB: case ClefType::G8_VB_O: case ClefType::G8_VB_P: return 59;
    case ClefType::G8_VA: return 83;
    case ClefType::G15_MB: return 47;
    case ClefType::G15_MA: return 95;
    case ClefType::F: return 50;
    case ClefType::F8_VB: return 38;
    case ClefType::F_8VA: return 62;
    case ClefType::F15_MB: return 26;
    case ClefType::F_15MA: return 74;
    case ClefType::C1: return 67;
    case ClefType::C2: return 64;
    case ClefType::C3: return 60;
    case ClefType::C4: return 57;
    case ClefType::C5: return 53;
    default: return 64;
    }
}

struct SoloPitch {
    int pitch = 60;          // concert
    int concertMiddle = 64;  // the middle of the staff in the concert clef
    int writtenMiddle = 64;  // ... and in the instrument's own (transposing) clef
};

template<typename F>
void forEachSoloNote(mu::engraving::Score* score, F f)
{
    using namespace mu::engraving;
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (track_idx_t t = 0; t < score->ntracks(); ++t) {
            EngravingItem* e = s->element(t);
            if (!e || !e->isChord()) {
                continue;
            }
            Chord* chord = toChord(e);
            for (Note* n : chord->notes()) {
                f(n);
            }
            for (Chord* grace : chord->graceNotes()) {
                for (Note* n : grace->notes()) {
                    f(n);
                }
            }
        }
    }
}

//! How far the notes sit off their staves at a shift: semitones beyond the staff, summed, with any note outside the
//! piano's 88 keys (A0–C8) costing far more. `written`: on the sheet written for the instrument (`offset` semitones
//! above concert pitch, in its own clef) rather than in concert pitch.
double soloShiftCost(const std::vector<SoloPitch>& pitches, int shift, bool written, int offset)
{
    double cost = 0.0;
    for (const SoloPitch& p : pitches) {
        const int sounding = p.pitch + shift;
        if (sounding < 21 || sounding > 108) {
            cost += 1000.0;
        }
        const int shown = written ? sounding + offset : sounding;
        cost += std::max(0, std::abs(shown - (written ? p.writtenMiddle : p.concertMiddle)) - 7);
    }
    return cost;
}

//! Up or down for a key `keyIndex` semitones above the original: the shift (keyIndex or keyIndex - 12) that keeps
//! the notes nearest their staves; with no notes yet, the nearer one. A shift that's nearly as good as the one in
//! use is kept, so a key doesn't jump octaves back and forth while the solo is written.
int soloPickShift(const std::vector<SoloPitch>& pitches, int keyIndex, int current, bool written = false, int offset = 0)
{
    if (keyIndex == 0) {
        return 0;
    }
    const int up = keyIndex;
    const int down = keyIndex - 12;
    if (pitches.empty()) {
        return (current == up || current == down) ? current : (up <= 6 ? up : down);
    }
    const double costUp = soloShiftCost(pitches, up, written, offset);
    const double costDown = soloShiftCost(pitches, down, written, offset);
    int best = costUp < costDown ? up : down;
    if (costUp == costDown) {
        // a tie: toward the middle of the piano
        double mean = 0.0;
        for (const SoloPitch& p : pitches) {
            mean += p.pitch;
        }
        mean /= double(pitches.size());
        best = std::abs(mean + up - 64.5) <= std::abs(mean + down - 64.5) ? up : down;
    }
    if ((current == up || current == down) && current != best) {
        const double now = current == up ? costUp : costDown;
        const double other = current == up ? costDown : costUp;
        if (now <= other * 1.15 + 2.0) {
            return current;
        }
    }
    return best;
}
}

QString StarScoreService::soloTranscriptionsFolder() const
{
    const QString projects = projectsFolder();
    return projects.isEmpty() ? QString() : projects + "/8 Solo Transcriptions";
}

static bool soloTagged(const mu::engraving::MasterScore* ms)
{
    return ms && ms->metaTags().count(String(SOLO_TAG)) > 0;
}

bool StarScoreService::isSoloTranscription() const
{
    return soloTagged(masterScore());
}

QJsonObject StarScoreService::soloTranscriptionInfo() const
{
    if (!isSoloTranscription()) {
        return QJsonObject();
    }
    return load().solo;
}

std::vector<std::tuple<QString, QString, QString> > StarScoreService::soloTiableSongs() const
{
    std::vector<std::tuple<QString, QString, QString> > out;
    const QString projects = projectsFolder();
    if (projects.isEmpty()) {
        return out;
    }
    static const QRegularExpression coded("^([A-Z0-9]{4}) - (.+)$");
    for (const QString& path : auditLibraryFiles(projects)) {
        const QRegularExpressionMatch m = coded.match(QFileInfo(path).completeBaseName());
        if (m.hasMatch()) {
            out.emplace_back(m.captured(1), m.captured(2), path);
        }
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return std::get<1>(a).compare(std::get<1>(b), Qt::CaseInsensitive) < 0;
    });
    return out;
}

QStringList StarScoreService::soloTranscriptionsTiedTo(const QString& songCode) const
{
    // each solo folder's .starscore is named "<CODE> · <Player> - <Song>.starscore" when it's tied to a song
    QStringList out;
    const QString folder = soloTranscriptionsFolder();
    if (folder.isEmpty() || songCode.isEmpty() || !QFileInfo(folder).isDir()) {
        return out;
    }
    QDirIterator it(folder, { songCode + " · *.starscore" }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (!path.contains("/Version History/")) {
            out << path;
        }
    }
    std::sort(out.begin(), out.end(), [](const QString& a, const QString& b) {
        return QFileInfo(a).lastModified() > QFileInfo(b).lastModified();
    });
    return out;
}

QString StarScoreService::soloTranscriptionPath(const StarScoreSoloTranscriptionOptions& options) const
{
    // "8 Solo Transcriptions/<Player> - <Song>/[<CODE> · ]<Player> - <Song>.starscore"
    const QString folder = soloTranscriptionsFolder();
    if (folder.isEmpty()) {
        return QString();
    }
    auto safe = [](QString s) {
        static const QRegularExpression unsafe("[/:\\\\]");
        return s.replace(unsafe, "-").trimmed();
    };
    const QString base = safe(options.player.trimmed() + " - " + options.song.trimmed());
    const QString code = options.tiedSongCode.trimmed();
    return folder + "/" + base + "/" + (code.isEmpty() ? QString() : code + " · ") + base + ".starscore";
}

//! What's wrong with these options for a new Solo Transcription ("" when nothing): its song and player missing, no
//! Projects and Sheets folder, or a file already where it would go
static QString soloOptionsProblem(const StarScoreSoloTranscriptionOptions& options, const QString& folder, const QString& path)
{
    if (options.song.trimmed().isEmpty() || options.player.trimmed().isEmpty()) {
        return muse::qtrc("starscore", "A solo transcription needs its song and its player.");
    }
    if (folder.isEmpty()) {
        return muse::qtrc("starscore", "The Projects and Sheets folder wasn't found (choose it on the Dashboard first).");
    }
    if (QFileInfo::exists(path)) {
        return muse::qtrc("starscore", "%1 is already there. Open it from there, or give this one another name.")
               .arg(QDir(folder).relativeFilePath(path));
    }
    return QString();
}

Ret StarScoreService::newSoloTranscription(const StarScoreSoloTranscriptionOptions& options)
{
    const QString path = soloTranscriptionPath(options);
    const QString problem = soloOptionsProblem(options, soloTranscriptionsFolder(), path);
    if (!problem.isEmpty()) {
        return make_ret(Ret::Code::UnknownError, problem.toStdString());
    }
    const QString instrumentId = options.instrumentId.isEmpty() ? QString("piano") : options.instrumentId;
    const InstrumentTemplate& tpl = instrumentsRepository()->instrumentTemplate(String::fromQString(instrumentId));
    if (tpl.id.isEmpty()) {
        return make_ret(Ret::Code::UnknownError, muse::qtrc("starscore", "Unknown instrument: %1").arg(instrumentId).toStdString());
    }

    ProjectCreateOptions projectOptions;
    projectOptions.title = options.song.trimmed();
    projectOptions.subtitle = soloSubtitle(options);
    projectOptions.composer = options.band.trimmed();

    ScoreCreateOptions& score = projectOptions.scoreOptions;
    score.withTempo = options.tempoBpm > 0;
    score.tempo.valueBpm = options.tempoBpm;
    {
        int noteValue = 4;
        bool dotted = false;
        starscore::tempoBeatFor(options.timeSigNumerator, options.timeSigDenominator, noteValue, dotted);
        score.tempo.duration = noteValue == 1 ? engraving::DurationType::V_WHOLE
                               : noteValue == 2 ? engraving::DurationType::V_HALF
                               : noteValue == 8 ? engraving::DurationType::V_EIGHTH
                               : noteValue == 16 ? engraving::DurationType::V_16TH : engraving::DurationType::V_QUARTER;
        score.tempo.withDot = dotted;
    }
    score.globalTimesig = engraving::Fraction(options.timeSigNumerator, options.timeSigDenominator);
    score.key = static_cast<engraving::Key>(std::clamp(options.keyFifths, -7, 7));
    score.totalMeasures = std::max(1, options.measures);
    PartInstrument pi;
    pi.instrumentTemplate = tpl;
    score.parts << pi;

    INotationProjectPtr project = projectCreator()->newProject(iocContext());
    Ret ret = project->createNew(projectOptions);
    if (!ret) {
        return ret;
    }
    // File › New (a window of its own): the window's score. From a song's StarScore bar: made and saved on its own,
    // then opened like any file (the song stays open as it is)
    const bool here = !globalContext()->currentProject() || options.replaceOpenScore;
    if (here) {
        globalContext()->setCurrentProject(project);
    }
    StarScoreSoloTranscriptionOptions withInstrument = options;
    withInstrument.instrumentId = instrumentId;
    ret = setUpSoloTranscription(project, withInstrument);
    if (!ret) {
        return ret;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    const bool saved = here ? projectFilesController()->saveProject(io::path_t(path))
                       : bool(project->save(io::path_t(path), SaveMode::Save));
    if (!saved) {
        return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "The solo transcription was made but couldn't be saved."));
    }
    LOGI() << "[starscore] new solo transcription: " << path.toStdString();
    if (!here) {
        dispatcher()->dispatch("file-open", muse::actions::ActionData::make_arg1<QUrl>(QUrl::fromLocalFile(path)));
    }
    return make_ok();
}

QString StarScoreService::soloSubtitle(const StarScoreSoloTranscriptionOptions& options)
{
    // "Michael Brecker's solo · Heavy Metal Be-Bop"
    QString sub = muse::qtrc("starscore", "%1's solo").arg(options.player.trimmed());
    if (!options.recording.trimmed().isEmpty()) {
        sub += " · " + options.recording.trimmed();
    }
    return sub;
}

Ret StarScoreService::setUpSoloTranscription(const INotationProjectPtr& project, const StarScoreSoloTranscriptionOptions& options)
{
    IMasterNotationPtr master = project ? project->masterNotation() : nullptr;
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms || ms->parts().empty()) {
        return make_ret(Ret::Code::InternalError);
    }
    engraving::Part* part = ms->parts().front();

    // marked first: from here on a fingering stays where it's written
    ms->setMetaTag(String(SOLO_TAG), u"1");
    // the main score in concert pitch: what the twelve keys are worked out from (chord symbols changed with it)
    if (!ms->style().styleB(engraving::Sid::concertPitch)) {
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Concert pitch"));
        master->notation()->style()->setStyleValue(StyleId::concertPitch, true);
        master->notation()->undoStack()->commitChanges();
    }

    // the key: the original's key signature at the start
    int fifths = int(ms->staff(0) ? ms->staff(0)->concertKey(engraving::Fraction(0, 1)) : engraving::Key::C);
    fifths = std::clamp(fifths, -7, 7);

    // the twelve keys, from the original up by semitones
    Data data = loadFrom(ms);
    QJsonArray keys;
    ExcerptNotationList excerpts = master->excerpts();
    const QString instrumentName = part->partName().toQString();
    for (int k = 0; k < 12; ++k) {
        const int keyFifths = soloKeyAfter(fifths, k);
        QString name = k == 0 ? muse::qtrc("starscore", "%1 (original)").arg(soloKeyName(keyFifths))
                       : muse::qtrc("starscore", "in %1").arg(soloKeyName(keyFifths));
        for (int i = 2; std::any_of(excerpts.begin(), excerpts.end(), [&](const IExcerptNotationPtr& e) { return e->name() == name; }); ++i) {
            name = QString("%1 (%2)").arg(name.section(" (", 0, 0)).arg(i);
        }
        IExcerptNotationPtr e = master->createEmptyExcerpt(name);
        e->setMasterParts({ part });
        excerpts.push_back(e);
        keys.append(QJsonObject { { "index", k }, { "name", name }, { "fifths", keyFifths },
                                  { "shift", k == 0 ? 0 : (k <= 6 ? k : k - 12) } });
    }
    master->setExcerpts(excerpts);
    // the twelve keys' tabs open, the original first
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        for (const QJsonValue& v : keys) {
            if (e && e->notation() && e->name() == v.toObject().value("name").toString()) {
                master->setExcerptIsOpen(e->notation(), true);
            }
        }
    }

    data.solo = QJsonObject {
        { "song", options.song.trimmed() }, { "player", options.player.trimmed() }, { "band", options.band.trimmed() },
        { "recording", options.recording.trimmed() }, { "link", options.link.trimmed() }, { "instrument", options.instrumentId },
        { "instrumentName", instrumentName }, { "tiedSongCode", options.tiedSongCode.trimmed() }, { "fifths", fifths },
        { "keys", keys },
    };
    storeTo(ms, data, project);

    m_soloLayoutSig.clear();
    refreshSoloTranscription(master, true);
    return make_ok();
}

Ret StarScoreService::convertToSoloTranscription(const StarScoreSoloTranscriptionOptions& options)
{
    // The open score (an old solo kept inside a song, or any score of one instrument) copied into its own Solo
    // Transcription file, which then opens: the score itself stays as it was
    INotationProjectPtr project = globalContext()->currentProject();
    engraving::MasterScore* ms = masterScore();
    if (!project || !ms) {
        return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "Open the solo first."));
    }
    if (isSoloTranscription()) {
        return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "This is a solo transcription already."));
    }
    // the soloist: an old solo's "solo" section (its other instruments are the band's copies), else the one instrument
    QString soloistId;
    const Data openData = loadFrom(ms);
    for (const StarScoreSection& sec : openData.sections) {
        if (sec.templateKey == "solo" && !sec.partIds.isEmpty()) {
            soloistId = sec.partIds.front();
        }
    }
    if (soloistId.isEmpty() && ms->parts().size() == 1) {
        soloistId = idText(ms->parts().front());
    }
    if (soloistId.isEmpty()) {
        return make_ret(Ret::Code::UnknownError,
                        muse::qtrc("starscore", "A solo transcription has one instrument, and this score has %1 with no solo section to "
                                                "say which is the solo.").arg(ms->parts().size()).toStdString());
    }
    const QString path = soloTranscriptionPath(options);
    const QString problem = soloOptionsProblem(options, soloTranscriptionsFolder(), path);
    if (!problem.isEmpty()) {
        return make_ret(Ret::Code::UnknownError, problem.toStdString());
    }
    // a copy (a .starscore is a MuseScore file), set up on its own without opening it, then saved where it goes
    const QString tmp = QDir::tempPath() + "/StarScoreSolo-" + QUuid::createUuid().toString(QUuid::Id128) + ".mscz";
    struct RemoveTmp {
        QString path;
        ~RemoveTmp() { QFile::remove(path); }
    } removeTmp { tmp };
    Ret ret = project->save(io::path_t(tmp), SaveMode::SaveCopy);
    if (!ret) {
        return ret;
    }
    StarScoreSoloTranscriptionOptions withInstrument = options;
    INotationProjectPtr copy = projectCreator()->newProject(iocContext());
    OpenParams params;
    params.skipPlaybackModel = true;
    ret = copy->load(io::path_t(tmp), params);
    if (!ret || !copy->masterNotation() || !copy->masterNotation()->masterScore()) {
        return ret ? make_ret(Ret::Code::InternalError) : ret;
    }
    engraving::MasterScore* cms = copy->masterNotation()->masterScore();
    // only the soloist (the band's copies and any part scores out)
    QStringList others;
    engraving::Part* soloist = nullptr;
    for (engraving::Part* p : cms->parts()) {
        if (idText(p) == soloistId) {
            soloist = p;
        } else {
            others << idText(p);
        }
    }
    if (!soloist) {
        return make_ret(Ret::Code::InternalError);
    }
    if (withInstrument.instrumentId.isEmpty()) {
        withInstrument.instrumentId = soloist->instrument()->id().toQString();
    }
    copy->masterNotation()->setExcerpts({});
    if (!others.isEmpty()) {
        removePartsKeepingSystemObjects(copy->masterNotation(), others);
    }
    // (an old solo's own StarScore data isn't a solo transcription's)
    cms->metaTags().erase(u"starscore");
    m_loadCache.erase(cms);
    ret = setUpSoloTranscription(copy, withInstrument);
    if (!ret) {
        return ret;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    ret = copy->save(io::path_t(path), SaveMode::SaveCopy);
    if (!ret) {
        QFile::remove(path);
        return ret;
    }
    LOGI() << "[starscore] solo transcription made from " << project->path().toQString().toStdString() << ": " << path.toStdString();
    if (!options.replaceOpenScore) {
        dispatcher()->dispatch("file-open", muse::actions::ActionData::make_arg1<QUrl>(QUrl::fromLocalFile(path)));
    }
    return make_ok();
}

//! The twelve part scores of the open Solo Transcription, by key index (0 = original): found by name, else (a tab
//! renamed) by place among the part scores that have the solo's part only
std::vector<StarScoreService::SoloKeyScore> StarScoreService::soloKeyScores(const IMasterNotationPtr& master) const
{
    std::vector<SoloKeyScore> out;
    if (!master || !soloTagged(master->masterScore())) {
        return out;
    }
    const QJsonArray keys = loadFrom(master->masterScore()).solo.value("keys").toArray();
    std::vector<IExcerptNotationPtr> all;
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        if (e && e->notation() && e->notation()->elements() && e->notation()->elements()->msScore()) {
            all.push_back(e);
        }
    }
    std::set<const void*> taken;
    auto add = [&](const QJsonObject& k, const IExcerptNotationPtr& e) {
        SoloKeyScore s;
        s.index = k.value("index").toInt();
        s.name = e->name();
        s.notation = e->notation();
        s.score = e->notation()->elements()->msScore();
        out.push_back(s);
        taken.insert(e.get());
    };
    std::vector<QJsonObject> missing;
    for (const QJsonValue& v : keys) {
        const QJsonObject k = v.toObject();
        bool found = false;
        for (const IExcerptNotationPtr& e : all) {
            if (!taken.count(e.get()) && e->name() == k.value("name").toString()) {
                add(k, e);
                found = true;
                break;
            }
        }
        if (!found) {
            missing.push_back(k);
        }
    }
    if (!missing.empty() && all.size() == size_t(keys.size())) {
        size_t m = 0;
        for (const IExcerptNotationPtr& e : all) {
            if (!taken.count(e.get()) && m < missing.size()) {
                add(missing[m++], e);
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const SoloKeyScore& a, const SoloKeyScore& b) { return a.index < b.index; });
    return out;
}

namespace {
//! The solo's notes (concert pitch) with the middles of their staves, from the main score
std::vector<SoloPitch> soloPitches(mu::engraving::MasterScore* ms)
{
    using namespace mu::engraving;
    std::vector<SoloPitch> pitches;
    forEachSoloNote(ms, [&](Note* n) {
        if (!n->staff() || !n->chord()) {
            return;
        }
        const ClefTypeList clefs = n->staff()->clefType(n->chord()->tick());
        SoloPitch p;
        p.pitch = n->pitch();
        p.concertMiddle = soloClefMiddle(clefs.concertClef);
        p.writtenMiddle = soloClefMiddle(clefs.transposingClef);
        pitches.push_back(p);
    });
    return pitches;
}

//! The main score's breaks, keep-togethers, bar widths, system locks and style, as one string: copied into the
//! twelve keys only when it changes, so a break put into one key's part score by hand stays until the main score's
//! formatting changes
QString soloLayoutSignature(mu::engraving::MasterScore* ms)
{
    using namespace mu::engraving;
    QString sig;
    for (Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        sig += (m->lineBreak() ? QString("L") : QString()) + (m->pageBreak() ? QString("P") : QString())
               + (m->noBreak() ? QString("N") : QString()) + QString::number(m->userStretch()) + ",";
    }
    for (const SystemLock* l : ms->systemLocks()->allLocks()) {
        if (l && l->startMB() && l->endMB()) {
            sig += QString("|%1-%2").arg(l->startMB()->tick().ticks()).arg(l->endMB()->tick().ticks());
        }
    }
    for (int i = 0; i < int(Sid::STYLES); ++i) {
        const Sid sid = static_cast<Sid>(i);
        const PropertyValue& v = ms->style().styleV(sid);
        if (sid != Sid::concertPitch && !(v == DefaultStyle::defaultStyle().styleV(sid))) {
            sig += QString(";%1=").arg(i) + v.toQVariant().toString();
        }
    }
    return sig;
}

//! Note colours by the pitch written on this sheet (the 12-note colours). Undoable: an export rolls them back.
//! dark: for the dark-mode sheets (page #15120E): the same hues, the dark ones (navy E, green D, brown G♯, red A,
//! purple B) lightened until they read on the dark page; F and A♯ a little lighter still, to stay apart from B and E
int soloColorNotes(mu::engraving::Score* score, bool dark)
{
    using namespace mu::engraving;
    static const char* COLORS[12] = {
        "#C7C102", "#FF5DA1", "#028800", "#2DD3C0", "#001CA4", "#CC75F8",
        "#92D303", "#F47700", "#915700", "#AD000E", "#829EFF", "#770096"
    };
    static const char* DARK_COLORS[12] = {
        "#E6DF2E", "#FF6FAE", "#36C234", "#3FE0CC", "#5072FF", "#E4B2FF",
        "#A8E621", "#FF8A1F", "#CC8E33", "#F2434F", "#A9BEFF", "#AE45D6"
    };
    int changed = 0;
    forEachSoloNote(score, [&](Note* n) {
        const int pc = ((n->epitch() % 12) + 12) % 12;
        const Color color = Color::fromString(dark ? DARK_COLORS[pc] : COLORS[pc]);
        auto apply = [&](EngravingItem* item) {
            if (item && item->getProperty(Pid::COLOR).value<Color>() != color) {
                item->undoChangeProperty(Pid::COLOR, color, PropertyFlags::NOSTYLE);
                return true;
            }
            return false;
        };
        bool any = apply(n);
        any |= apply(n->accidental());
        for (NoteDot* dot : n->dots()) {
            any |= apply(dot);
        }
        changed += any ? 1 : 0;
    });
    return changed;
}
}

void StarScoreService::respellSoloKey(engraving::Score* es, const engraving::Interval& iv, bool writtenClefs)
{
    using namespace mu::engraving;
    engraving::MasterScore* ms = es ? es->masterScore() : nullptr;
    if (!es || !ms) {
        return;
    }
    // the key's transposition on the part score's own instrument(s) (not an undo step: worked out from the main
    // score every time)
    for (Part* p : es->parts()) {
        for (const auto& entry : p->instruments()) {
            if (entry.second && !(entry.second->transpose() == iv)) {
                entry.second->setTranspose(iv);
            }
        }
    }
    if (es->style().styleB(Sid::concertPitch)) {
        es->style().set(Sid::concertPitch, false);
    }
    Interval written = iv;
    written.flip();
    // the clefs: the concert clef on a concert-pitch sheet (a tenor sax's 8vb treble), the instrument's own on one
    // written for it. The part score shows each clef's "transposing" side (its concert pitch is off).
    for (staff_idx_t si = 0; si < es->nstaves(); ++si) {
        Staff* st = es->staff(si);
        Staff* mst = si < ms->nstaves() ? ms->staff(si) : nullptr;
        if (!st || !mst) {
            continue;
        }
        const ClefTypeList base = mst->defaultClefType();
        const ClefTypeList want(base.concertClef, writtenClefs ? base.transposingClef : base.concertClef);
        if (!(st->defaultClefType() == want)) {
            st->setDefaultClefType(want);
        }
    }
    for (Segment* s = es->firstSegment(SegmentType::HeaderClef | SegmentType::Clef); s;
         s = s->next1(SegmentType::HeaderClef | SegmentType::Clef)) {
        for (staff_idx_t si = 0; si < es->nstaves(); ++si) {
            EngravingItem* e = s->element(si * VOICES);
            if (!e || !e->isClef() || e->generated()) {
                continue;
            }
            Clef* c = toClef(e);
            ClefTypeList from = c->clefTypeList();
            for (EngravingObject* o : c->linkList()) {
                if (o != c && o->score() == ms) {
                    from = static_cast<Clef*>(o)->clefTypeList();
                }
            }
            const ClefTypeList want(from.concertClef, writtenClefs ? from.transposingClef : from.concertClef);
            if (!(c->clefTypeList() == want)) {
                c->setClefType(want);
                if (Staff* st = es->staff(si)) {
                    st->setClef(c);
                }
            }
        }
    }
    // the notes spelled for this key (tpc2: what's written), as Note::setTpcFromPitch does
    forEachSoloNote(es, [&](Note* n) {
        const int tpc2 = iv.isZero() ? n->tpc1() : Transpose::transposeTpc(n->tpc1(), written, false);
        if (n->tpc2() != tpc2) {
            n->setTpc2(tpc2);
        }
    });
    // key signatures in this key
    for (Segment* s = es->firstSegment(SegmentType::KeySig); s; s = s->next1(SegmentType::KeySig)) {
        for (staff_idx_t si = 0; si < es->nstaves(); ++si) {
            EngravingItem* e = s->element(si * VOICES);
            if (!e || !e->isKeySig()) {
                continue;
            }
            KeySig* ks = toKeySig(e);
            KeySigEvent ke = ks->keySigEvent();
            if (ke.isAtonal() || ke.custom()) {
                continue;
            }
            const Key want = iv.isZero() ? ke.concertKey() : Transpose::transposeKey(ke.concertKey(), written, PreferSharpFlat::NONE);
            if (ke.key() != want) {
                ke.setKey(want);
                ks->setKeySigEvent(ke);
                if (Staff* st = es->staff(si)) {
                    st->setKey(s->tick(), ke);
                }
            }
        }
    }
    // chord symbols in this key, from the main score's (concert, or written for its own instrument)
    const bool masterConcert = ms->style().styleB(Sid::concertPitch);
    for (Segment* s = es->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : s->annotations()) {
            if (!a || !a->isHarmony()) {
                continue;
            }
            Harmony* h = toHarmony(a);
            const Harmony* src = nullptr;
            for (EngravingObject* o : h->linkList()) {
                if (o != h && o->score() == ms) {
                    src = static_cast<const Harmony*>(o);
                }
            }
            if (!src || src->chords().size() != h->chords().size()) {
                continue;
            }
            Interval toConcert;
            if (!masterConcert && src->staff()) {
                toConcert = src->staff()->transpose(src->tick());   // written -> concert
            }
            bool changed = false;
            for (size_t i = 0; i < h->chords().size(); ++i) {
                const HarmonyInfo* from = src->chords().at(i);
                HarmonyInfo* to = h->chords().at(i);
                if (!from || !to) {
                    continue;
                }
                auto spell = [&](int tpc) {
                    if (!tpcIsValid(tpc)) {
                        return tpc;
                    }
                    const int concert = toConcert.isZero() ? tpc : Transpose::transposeTpc(tpc, toConcert, false);
                    return iv.isZero() ? concert : Transpose::transposeTpc(concert, written, false);
                };
                const int root = spell(from->rootTpc());
                const int bass = spell(from->bassTpc());
                if (to->rootTpc() != root || to->bassTpc() != bass) {
                    to->setRootTpc(root);
                    to->setBassTpc(bass);
                    changed = true;
                }
            }
            if (changed) {
                h->realizedHarmony().setDirty(true);
                h->setXmlText(h->harmonyName());
                h->triggerLayout();
            }
        }
    }
    es->setLayoutAll();
}

void StarScoreService::scheduleSoloRefresh()
{
    if (m_soloRefreshQueued || !isSoloTranscription()) {
        return;
    }
    m_soloRefreshQueued = true;
    const std::function<bool()> stillCurrent = currentSongCheck();
    QTimer::singleShot(250, &m_timerGuard, [this, stillCurrent]() {
        m_soloRefreshQueued = false;
        const bool undoRedo = m_soloUndoRedo;
        m_soloUndoRedo = false;
        if (stillCurrent && !stillCurrent()) {
            return;
        }
        refreshSoloTranscription(globalContext()->currentMasterNotation(), !undoRedo);
    });
}

void StarScoreService::refreshSoloTranscription(const IMasterNotationPtr& master, bool syncLayout)
{
    using namespace mu::engraving;
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms || !soloTagged(ms) || m_soloRefreshing) {
        return;
    }
    m_soloRefreshing = true;
    struct Done {
        bool& flag;
        ~Done() { flag = false; }
    } done { m_soloRefreshing };

    Data data = loadFrom(ms);
    const int fifths = data.solo.value("fifths").toInt();
    QJsonArray keys = data.solo.value("keys").toArray();
    const std::vector<SoloPitch> pitches = soloPitches(ms);

    bool keysChanged = false;
    const std::vector<SoloKeyScore> scores = soloKeyScores(master);
    for (const SoloKeyScore& k : scores) {
        int at = -1;
        for (int i = 0; i < keys.size(); ++i) {
            if (keys.at(i).toObject().value("index").toInt() == k.index) {
                at = i;
            }
        }
        if (at < 0) {
            continue;
        }
        QJsonObject entry = keys.at(at).toObject();
        const int current = entry.value("shift").toInt();
        const int shift = soloPickShift(pitches, k.index, current);
        if (shift != current) {
            entry["shift"] = shift;
            keys[at] = entry;
            keysChanged = true;
        }
        respellSoloKey(k.score, soloInterval(fifths, k.index, shift), false);
    }
    if (keysChanged) {
        data.solo["keys"] = keys;
        storeTo(ms, data, projectOf(ms));
    }

    // the main score's formatting in all twelve, in one undo step with the edit that changed it (not after an undo
    // or redo: that would clear what can be redone), and only when it changed
    const QString sig = soloLayoutSignature(ms);
    if (syncLayout && !scores.empty() && sig != m_soloLayoutSig) {
        std::vector<engraving::Score*> targets;
        for (const SoloKeyScore& k : scores) {
            targets.push_back(k.score);
        }
        INotationUndoStackPtr stack = master->notation()->undoStack();
        const size_t before = stack->currentStateIndex();
        stack->prepareChanges(TranslatableString::untranslatable("Formatting in the twelve keys"));
        starscore::LayoutCopyOptions options;
        options.replaceExisting = true;
        starscore::copyLayout(ms, targets, options);
        for (engraving::Score* es : targets) {
            for (Measure* m = ms->firstMeasure(), * em = es->firstMeasure(); m && em; m = m->nextMeasure(), em = em->nextMeasure()) {
                if (m->userStretch() != em->userStretch()) {
                    em->undoChangeProperty(Pid::USER_STRETCH, m->userStretch());
                }
            }
            for (int i = 0; i < int(Sid::STYLES); ++i) {
                const Sid sid = static_cast<Sid>(i);
                if (sid != Sid::concertPitch && !(ms->style().styleV(sid) == es->style().styleV(sid))) {
                    es->undoChangeStyleVal(sid, ms->style().styleV(sid));
                }
            }
        }
        stack->commitChanges();
        if (before != muse::nidx && before >= 1 && stack->currentStateIndex() == before + 1) {
            stack->mergeCommands(before - 1);
        }
    }
    if (m_soloLayoutSig.isEmpty() || syncLayout) {
        m_soloLayoutSig = sig;
    }
    for (const SoloKeyScore& k : scores) {
        if (k.notation) {
            k.notation->notationChanged().notify();
        }
    }
}

void StarScoreService::onSoloTranscriptionOpened()
{
    m_soloLayoutSig.clear();
    m_soloUndoRedo = false;
    if (!isSoloTranscription()) {
        return;
    }
    // (opened: the keys spelled again, the formatting left as it was saved)
    m_soloLayoutSig = soloLayoutSignature(masterScore());
    refreshSoloTranscription(globalContext()->currentMasterNotation(), false);
}

mu::engraving::Interval StarScoreService::soloInstrumentInterval() const
{
    engraving::MasterScore* ms = masterScore();
    if (!ms || ms->parts().empty()) {
        return engraving::Interval();
    }
    const engraving::Instrument* inst = ms->parts().front()->instrument(engraving::Fraction(0, 1));
    return inst ? inst->transpose() : engraving::Interval();
}

QString StarScoreService::prepareSoloKeyForSheet(const SoloKeyScore& k, bool written)
{
    using namespace mu::engraving;
    engraving::MasterScore* ms = masterScore();
    if (!ms || !k.score) {
        return QString();
    }
    const Data data = load();
    const int fifths = data.solo.value("fifths").toInt();
    const int keyFifths = soloKeyAfter(fifths, k.index);
    int shift = 0;
    for (const QJsonValue& v : data.solo.value("keys").toArray()) {
        if (v.toObject().value("index").toInt() == k.index) {
            shift = v.toObject().value("shift").toInt();
        }
    }
    Interval iv = soloInterval(fifths, k.index, shift);
    QString label = k.index == 0 ? muse::qtrc("starscore", "%1 (original key)").arg(soloKeyName(keyFifths))
                    : muse::qtrc("starscore", "in %1").arg(soloKeyName(keyFifths));
    if (written) {
        // for the instrument: its own transposition on top, the octave picked again for its own clef
        // (the written key as a key signature of at most 6 flats or 5 sharps, the letters counted from the original)
        const Interval own = soloInstrumentInterval();
        const int writtenShift = soloPickShift(soloPitches(ms), k.index, shift, true, -own.chromatic);
        const int writtenFifths = soloKeyAbove(keyFifths, -own.chromatic);
        iv = soloIntervalTo(fifths, writtenFifths, writtenShift - own.chromatic);
        label = k.index == 0
                ? muse::qtrc("starscore", "in %1 (concert %2, original key)").arg(soloKeyName(writtenFifths), soloKeyName(keyFifths))
                : muse::qtrc("starscore", "in %1 (concert %2)").arg(soloKeyName(writtenFifths), soloKeyName(keyFifths));
    }
    respellSoloKey(k.score, iv, written);
    return label;
}

int StarScoreService::colorSoloSheet(engraving::Score* score, bool dark)
{
    return soloColorNotes(score, dark);
}

// ---------------------------------------------------------------------------------------------------------------------
//  Learning the solos (1.25.0, Joel 4 Nov 2026): Home lists every solo transcription whose sheets have been exported,
//  each key marked learned or memorized. Kept beside the solos, so it travels with them (Google Drive):
//  "8 Solo Transcriptions/.solo-progress.json" = { "version": 1, "solos": { "<file relative to the folder>": {
//  "player", "song", "instrument", "exported": "yyyy-MM-dd", "keys": [ { "name", "state" } × 12 ] } } }
// ---------------------------------------------------------------------------------------------------------------------

static QString soloProgressPath(const QString& folder)
{
    return folder.isEmpty() ? QString() : folder + "/.solo-progress.json";
}

static QJsonObject soloReadProgress(const QString& folder)
{
    QFile f(soloProgressPath(folder));
    if (!f.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(f.readAll()).object();
}

static bool soloWriteProgress(const QString& folder, QJsonObject root)
{
    root["version"] = 1;
    QSaveFile f(soloProgressPath(folder));
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return f.commit();
}

//! "E♭ (original)" -> "E♭", "in D♭" -> "D♭"
static QString soloShortKey(QString name)
{
    name = name.trimmed();
    if (name.startsWith("in ")) {
        name = name.mid(3);
    }
    const int paren = name.indexOf(" (");
    return paren > 0 ? name.left(paren) : name;
}

//! Solos exported before the list existed (1.24.0): a solo folder with concert sheets and no entry gets one, its keys
//! read from the sheets' names ("… - 04 Db (concert).pdf"). Returns whether any was added.
static bool soloAddUnlisted(const QString& folder, QJsonObject& root)
{
    QJsonObject solos = root.value("solos").toObject();
    bool added = false;
    static const QRegularExpression sheetRe("^(.+) - (.+) - (\\d\\d) (\\S+) \\(concert\\)\\.pdf$");
    QDirIterator it(folder, { "*.starscore" }, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.contains("/Version History/")) {
            continue;
        }
        const QString id = QDir(folder).relativeFilePath(path);
        if (solos.contains(id)) {
            continue;
        }
        const QDir concert(QFileInfo(path).absolutePath() + "/Sheets/Concert");
        std::map<int, QString> keys;
        QString player, song;
        for (const QString& f : concert.entryList({ "*(concert).pdf" }, QDir::Files)) {
            const QRegularExpressionMatch m = sheetRe.match(f);
            if (m.hasMatch()) {
                player = m.captured(1);
                song = m.captured(2);
                QString key = m.captured(4);
                key.replace("b", QString::fromUtf8("♭")).replace("#", QString::fromUtf8("♯"));
                keys[m.captured(3).toInt() - 1] = key;
            }
        }
        if (keys.size() < 12) {
            continue;
        }
        QJsonArray list;
        for (const auto& [index, key] : keys) {
            list.append(QJsonObject { { "index", index }, { "name", index == 0 ? key + " (original)" : "in " + key }, { "state", "" } });
        }
        solos[id] = QJsonObject { { "player", player }, { "song", song }, { "instrument", "" },
                                  { "exported", QFileInfo(concert.absolutePath()).lastModified().date().toString(Qt::ISODate) },
                                  { "keys", list } };
        added = true;
    }
    if (added) {
        root["solos"] = solos;
    }
    return added;
}

void StarScoreService::recordSoloExport()
{
    INotationProjectPtr project = globalContext()->currentProject();
    const QString folder = soloTranscriptionsFolder();
    if (!project || folder.isEmpty() || !isSoloTranscription()) {
        return;
    }
    const QString file = QDir::cleanPath(project->path().toQString());
    if (!file.startsWith(QDir::cleanPath(folder) + "/")) {
        return;   // (a solo kept somewhere else isn't on the list)
    }
    const QString id = QDir(folder).relativeFilePath(file);
    const QJsonObject info = load().solo;
    QJsonObject root = soloReadProgress(folder);
    QJsonObject solos = root.value("solos").toObject();
    QJsonObject entry = solos.value(id).toObject();
    const QJsonArray oldKeys = entry.value("keys").toArray();
    QJsonArray keys;
    const QJsonArray infoKeys = info.value("keys").toArray();
    for (int i = 0; i < infoKeys.size(); ++i) {
        const QJsonObject k = infoKeys.at(i).toObject();
        const int index = k.value("index").toInt(i);
        QString state;
        for (const QJsonValue& v : oldKeys) {
            if (v.toObject().value("index").toInt(-1) == index) {
                state = v.toObject().value("state").toString();
            }
        }
        keys.append(QJsonObject { { "index", index }, { "name", k.value("name").toString() }, { "state", state } });
    }
    entry["player"] = info.value("player").toString();
    entry["song"] = info.value("song").toString();
    entry["instrument"] = info.value("instrumentName").toString();
    entry["tiedSongCode"] = info.value("tiedSongCode").toString();
    entry["exported"] = QDate::currentDate().toString(Qt::ISODate);
    entry["keys"] = keys;
    solos[id] = entry;
    root["solos"] = solos;
    if (soloWriteProgress(folder, root)) {
        m_soloLearningChanged.notify();
    } else {
        LOGW() << "[starscore] couldn't write " << soloProgressPath(folder).toStdString();
    }
}

QJsonArray StarScoreService::soloLearningList() const
{
    QJsonArray out;
    const QString folder = soloTranscriptionsFolder();
    if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
        return out;
    }
    QJsonObject root = soloReadProgress(folder);
    if (soloAddUnlisted(folder, root)) {
        soloWriteProgress(folder, root);
    }
    const QJsonObject solos = root.value("solos").toObject();
    std::vector<QJsonObject> list;
    for (auto it = solos.begin(); it != solos.end(); ++it) {
        const QString path = folder + "/" + it.key();
        if (!QFileInfo::exists(path)) {
            continue;   // (moved or deleted)
        }
        QJsonObject e = it.value().toObject();
        QJsonArray keys;
        for (const QJsonValue& v : e.value("keys").toArray()) {
            QJsonObject k = v.toObject();
            k["short"] = soloShortKey(k.value("name").toString());
            keys.append(k);
        }
        e["keys"] = keys;
        e["id"] = it.key();
        e["path"] = path;
        e["audio"] = soloAudioFiles(path);
        list.push_back(e);
    }
    std::sort(list.begin(), list.end(), [](const QJsonObject& a, const QJsonObject& b) {
        const int c = a.value("player").toString().compare(b.value("player").toString(), Qt::CaseInsensitive);
        return c != 0 ? c < 0 : a.value("song").toString().compare(b.value("song").toString(), Qt::CaseInsensitive) < 0;
    });
    for (const QJsonObject& e : list) {
        out.append(e);
    }
    return out;
}

Ret StarScoreService::setSoloKeyState(const QString& id, int keyIndex, const QString& state)
{
    const QString folder = soloTranscriptionsFolder();
    if (folder.isEmpty()) {
        return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "The Projects and Sheets folder wasn't found."));
    }
    if (!state.isEmpty() && state != "learned" && state != "memorized") {
        return make_ret(Ret::Code::UnknownError);
    }
    QJsonObject root = soloReadProgress(folder);
    soloAddUnlisted(folder, root);
    QJsonObject solos = root.value("solos").toObject();
    if (!solos.contains(id)) {
        return make_ret(Ret::Code::UnknownError, muse::trc("starscore", "That solo isn't on the list any more."));
    }
    QJsonObject entry = solos.value(id).toObject();
    QJsonArray keys = entry.value("keys").toArray();
    bool found = false;
    for (int i = 0; i < keys.size(); ++i) {
        QJsonObject k = keys.at(i).toObject();
        if (k.value("index").toInt(i) == keyIndex) {
            k["state"] = state;
            if (!state.isEmpty()) {
                k["date"] = QDate::currentDate().toString(Qt::ISODate);
            } else {
                k.remove("date");
            }
            keys[i] = k;
            found = true;
        }
    }
    if (!found) {
        return make_ret(Ret::Code::UnknownError);
    }
    entry["keys"] = keys;
    solos[id] = entry;
    root["solos"] = solos;
    if (!soloWriteProgress(folder, root)) {
        return make_ret(Ret::Code::UnknownError,
                        muse::qtrc("starscore", "Couldn't write %1.").arg(soloProgressPath(folder)).toStdString());
    }
    m_soloLearningChanged.notify();
    return make_ok();
}

muse::async::Notification StarScoreService::soloLearningChanged() const
{
    return m_soloLearningChanged;
}

// ---------------------------------------------------------------------------------------------------------------------
//  A solo's recordings (1.25.0, Joel 4 Nov 2026): the original recording, a stem-separated copy that makes the solo
//  clearer, … copied into "<solo folder>/Audio/" so they go with the solo (Google Drive); opened in the Mac's own
//  player from the StarScore bar or Home
// ---------------------------------------------------------------------------------------------------------------------

static const QStringList SOLO_AUDIO_TYPES { "*.mp3", "*.m4a", "*.aac", "*.wav", "*.aif", "*.aiff", "*.flac", "*.ogg", "*.opus",
                                            "*.mp4", "*.mov", "*.m4v" };

QJsonArray StarScoreService::soloAudioFiles(const QString& soloPath) const
{
    QJsonArray out;
    if (soloPath.isEmpty()) {
        return out;
    }
    const QDir audio(QFileInfo(soloPath).absolutePath() + "/Audio");
    for (const QFileInfo& f : audio.entryInfoList(SOLO_AUDIO_TYPES, QDir::Files, QDir::Name | QDir::IgnoreCase)) {
        out.append(QJsonObject { { "name", f.completeBaseName() }, { "path", f.absoluteFilePath() } });
    }
    return out;
}

RetVal<QString> StarScoreService::addSoloAudio(const QString& soloPath)
{
    if (soloPath.isEmpty() || !QFileInfo::exists(soloPath)) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "Save the solo transcription first."));
    }
    const io::path_t picked = interactive()->selectOpeningFileSync(
        muse::trc("starscore", "Choose a recording of the solo"), io::path_t(QDir::homePath() + "/Downloads"),
        { muse::trc("starscore", "Audio and video files") + " (" + SOLO_AUDIO_TYPES.join(' ').toStdString() + ")" });
    if (picked.empty()) {
        return RetVal<QString>::make_ok(QString());
    }
    const QFileInfo source(picked.toQString());

    constexpr int Original = static_cast<int>(IInteractive::Button::CustomButton) + 1;
    constexpr int Stems = static_cast<int>(IInteractive::Button::CustomButton) + 2;
    constexpr int OwnName = static_cast<int>(IInteractive::Button::CustomButton) + 3;
    constexpr int Cancel = static_cast<int>(IInteractive::Button::CustomButton) + 4;
    const IInteractive::Result answer = interactive()->questionSync(
        muse::trc("starscore", "What is this recording?"),
        muse::qtrc("starscore", "“%1” is copied into the solo's Audio folder under the name you pick.").arg(source.fileName()).toStdString(),
        { IInteractive::ButtonData(Original, muse::trc("starscore", "The original recording")),
          IInteractive::ButtonData(Stems, muse::trc("starscore", "Stem-separated")),
          IInteractive::ButtonData(OwnName, muse::trc("starscore", "Keep its file name")),
          IInteractive::ButtonData(Cancel, muse::trc("starscore", "Cancel")) }, Original);
    QString base;
    switch (answer.button()) {
    case Original: base = muse::qtrc("starscore", "Original recording");
        break;
    case Stems: base = muse::qtrc("starscore", "Stem-separated");
        break;
    case OwnName: base = source.completeBaseName();
        break;
    default:
        return RetVal<QString>::make_ok(QString());
    }
    return copySoloAudio(soloPath, source.absoluteFilePath(), base);
}

//! The file copied into the solo's Audio folder as "<base>.<ext>" ("<base> 2.<ext>" when that's taken)
static RetVal<QString> soloCopyAudio(const QString& soloPath, const QString& sourcePath, QString base)
{
    const QFileInfo source(sourcePath);
    static const QRegularExpression unsafe("[/:\\\\]");
    base.replace(unsafe, "-");
    const QString dir = QFileInfo(soloPath).absolutePath() + "/Audio";
    QDir().mkpath(dir);
    const QString ext = source.suffix().isEmpty() ? QString() : "." + source.suffix().toLower();
    QString target = dir + "/" + base + ext;
    for (int i = 2; QFileInfo::exists(target); ++i) {
        target = QString("%1/%2 %3%4").arg(dir, base).arg(i).arg(ext);
    }
    if (!QFile::copy(source.absoluteFilePath(), target)) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError,
                                         muse::qtrc("starscore", "Couldn't copy it to %1.").arg(target).toStdString());
    }
    LOGI() << "[starscore] solo recording added: " << target.toStdString();
    return RetVal<QString>::make_ok(target);
}

RetVal<QString> StarScoreService::copySoloAudio(const QString& soloPath, const QString& sourcePath, const QString& base)
{
    const RetVal<QString> r = soloCopyAudio(soloPath, sourcePath, base);
    if (r.ret) {
        m_soloLearningChanged.notify();
        scheduleChanged();
    }
    return r;
}

Ret StarScoreService::removeSoloAudio(const QString& audioPath)
{
    const QFileInfo f(audioPath);
    if (!f.exists() || f.dir().dirName() != "Audio") {
        return make_ret(Ret::Code::UnknownError);
    }
    const IInteractive::Result answer = interactive()->questionSync(
        muse::trc("starscore", "Remove this recording from the solo?"),
        muse::qtrc("starscore", "“%1” goes into the solo's Version History folder (it isn't deleted).").arg(f.fileName()).toStdString(),
        { IInteractive::Button::Cancel, IInteractive::Button::Ok }, IInteractive::Button::Ok);
    if (answer.standardButton() != IInteractive::Button::Ok) {
        return make_ok();
    }
    QDir soloDir = f.dir();
    soloDir.cdUp();
    const QString dir = soloDir.absolutePath() + "/Version History/" + QDate::currentDate().toString(Qt::ISODate) + " Removed audio";
    QDir().mkpath(dir);
    QString target = dir + "/" + f.fileName();
    for (int i = 2; QFileInfo::exists(target); ++i) {
        target = QString("%1/%2 %3.%4").arg(dir, f.completeBaseName()).arg(i).arg(f.suffix());
    }
    if (!QFile::rename(f.absoluteFilePath(), target)) {
        return make_ret(Ret::Code::UnknownError, muse::qtrc("starscore", "Couldn't move it to %1.").arg(dir).toStdString());
    }
    m_soloLearningChanged.notify();
    scheduleChanged();
    return make_ok();
}

QString StarScoreService::soloFileKeyName(int keyIndex, bool written) const
{
    // the key in a file name, without ♭ and ♯ ("Db", "F#")
    const Data data = load();
    const int fifths = data.solo.value("fifths").toInt();
    int keyFifths = soloKeyAfter(fifths, keyIndex);
    if (written) {
        keyFifths = soloKeyAbove(keyFifths, -soloInstrumentInterval().chromatic);
    }
    return soloKeyName(keyFifths).replace(QString::fromUtf8("♭"), "b").replace(QString::fromUtf8("♯"), "#");
}

// ---------------------------------------------------------------------------------------------------------------------
//  Autotest steps (STARSCORE_AUTOTEST_STEPS):
//    solonew:<player>|<song>|<instrument id>|<fifths>[|<tied code>]   a new Solo Transcription, made this window's
//        score, in STARSCORE_AUTOTEST_PROJECTS (else <autotest dir>/projects)
//    solonotes:<pitch> <pitch> …   quarter notes from bar 1 of the main score (one undo step; the keys refresh after it)
//    solochord:<text>              a chord symbol on the first note
//    solofinger:<key index>|<text> a fingering on the first note of that key's part score
//    solobreak:<bar>               a system break after that bar of the main score
//    solocheck                     each key: its name, shift, transposition, key signature, the first notes as written
//                                  (pitch, spelling), its first chord symbol and fingerings, and its breaks
//    solosave                      saves the solo transcription
//    soloload                      the saved file read back on its own: each key's first notes as written
//    soloexport                    the twelve keys' sheets (and a list of what was written)
//    soloshow:<name>               an old solo kept in the song shown
//    sololist, solomark:<key>|<learned|memorized|->   Home's learning list (and a key marked on its first solo)
//    soloaudioadd:<file>|<name>    a recording copied into the open solo's Audio folder
//    solountag                     the open solo transcription made a plain one-instrument score (for soloconvert)
//    soloconvert:<player>|<song>   the open score made into a Solo Transcription file
// ---------------------------------------------------------------------------------------------------------------------

static void soloTestLog(const QString& line)
{
    QFile f(qEnvironmentVariable("STARSCORE_AUTOTEST") + "/log.txt");
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write((line + "\n").toUtf8());
    }
}

static QString soloTestNotes(mu::engraving::Score* score, int max = 8)
{
    using namespace mu::engraving;
    QStringList out;
    forEachSoloNote(score, [&](Note* n) {
        if (out.size() < max) {
            out << QString("%1%2(%3)").arg(tpc2name(n->tpc(), NoteSpellingType::STANDARD, NoteCaseType::AUTO).toQString())
                   .arg(n->epitch() / 12 - 1).arg(n->epitch());
        }
    });
    return out.join(" ");
}

bool StarScoreService::autotestSoloStep(const QString& step)
{
    using namespace mu::engraving;
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    INotationProjectPtr project = globalContext()->currentProject();
    if (step.startsWith("solonew:")) {
        const QStringList a = step.mid(8).split('|');
        QString projects = qEnvironmentVariable("STARSCORE_AUTOTEST_PROJECTS");
        if (projects.isEmpty()) {
            projects = qEnvironmentVariable("STARSCORE_AUTOTEST") + "/projects";
        }
        QDir().mkpath(projects);
        setProjectsFolder(projects);
        StarScoreSoloTranscriptionOptions o;
        o.player = a.value(0);
        o.song = a.value(1);
        o.instrumentId = a.value(2);
        o.keyFifths = a.value(3).toInt();
        o.tiedSongCode = a.value(4);
        o.band = "Test Band";
        o.recording = "Test Album";
        o.link = "https://example.com/solo";
        o.measures = 8;
        o.replaceOpenScore = true;
        const Ret ret = newSoloTranscription(o);
        soloTestLog(QString("  %1 -> %2").arg(ret ? QString("made") : QString::fromStdString(ret.toString()),
                                             soloTranscriptionPath(o)));
        return true;
    }
    if (!master || !ms) {
        return false;
    }
    if (step.startsWith("solonotes:")) {
        INotationUndoStackPtr stack = master->notation()->undoStack();
        stack->prepareChanges(TranslatableString::untranslatable("Test notes"));
        int i = 0;
        for (const QString& p : step.mid(10).split(' ', Qt::SkipEmptyParts)) {
            Segment* seg = ms->tick2segment(Fraction(i, 4), true, SegmentType::ChordRest);
            if (!seg) {
                break;
            }
            NoteVal nval(p.toInt());
            ms->setNoteRest(seg, 0, nval, Fraction(1, 4));
            ++i;
        }
        stack->commitChanges();
        master->notation()->notationChanged().notify();
        soloTestLog(QString("  %1 note(s)").arg(i));
        return true;
    }
    if (step.startsWith("solochord:")) {
        Segment* seg = ms->tick2segment(Fraction(0, 1), true, SegmentType::ChordRest);
        if (!seg) {
            return true;
        }
        INotationUndoStackPtr stack = master->notation()->undoStack();
        stack->prepareChanges(TranslatableString::untranslatable("Test chord symbol"));
        Harmony* h = Factory::createHarmony(seg);
        h->setTrack(0);
        h->setParent(seg);
        h->setHarmony(String::fromQString(step.mid(10)));
        ms->undoAddElement(h);
        stack->commitChanges();
        soloTestLog("  chord symbol " + h->harmonyName().toQString());
        return true;
    }
    if (step.startsWith("solofinger:")) {
        const int index = step.mid(11).section('|', 0, 0).toInt();
        for (const SoloKeyScore& k : soloKeyScores(master)) {
            if (k.index != index) {
                continue;
            }
            Note* first = nullptr;
            forEachSoloNote(k.score, [&](Note* n) {
                if (!first) {
                    first = n;
                }
            });
            if (!first) {
                soloTestLog("  no note");
                return true;
            }
            INotationUndoStackPtr stack = master->notation()->undoStack();
            stack->prepareChanges(TranslatableString::untranslatable("Test fingering"));
            Fingering* f = Factory::createFingering(first);
            f->setTrack(first->track());
            f->setParent(first);
            f->setXmlText(String::fromQString(step.section('|', 1)));
            k.score->undoAddElement(f);
            stack->commitChanges();
            soloTestLog("  fingering in " + k.name);
        }
        return true;
    }
    if (step.startsWith("solobreak:")) {
        const int bar = step.mid(10).toInt();
        Measure* m = ms->firstMeasure();
        for (int i = 1; m && i < bar; ++i) {
            m = m->nextMeasure();
        }
        if (m) {
            INotationUndoStackPtr stack = master->notation()->undoStack();
            stack->prepareChanges(TranslatableString::untranslatable("Test break"));
            m->undoSetLineBreak(true);
            stack->commitChanges();
        }
        soloTestLog(QString("  break after bar %1").arg(bar));
        return true;
    }
    if (step == "solocheck") {
        // the refresh a moment after an edit, done now
        refreshSoloTranscription(master, true);
        auto fingerings = [](Score* s) {
            int n = 0;
            forEachSoloNote(s, [&](Note* note) {
                for (EngravingItem* e : note->el()) {
                    n += e && e->isFingering() ? 1 : 0;
                }
            });
            return n;
        };
        auto breaks = [](Score* s) {
            QStringList b;
            int i = 1;
            for (Measure* m = s->firstMeasure(); m; m = m->nextMeasure(), ++i) {
                if (m->lineBreak()) {
                    b << QString::number(i);
                }
            }
            return b.join(",");
        };
        auto firstChord = [](Score* s) {
            for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                for (EngravingItem* a : seg->annotations()) {
                    if (a && a->isHarmony()) {
                        return toHarmony(a)->harmonyName().toQString();
                    }
                }
            }
            return QString("-");
        };
        auto keyOf = [](Score* s) {
            for (Segment* seg = s->firstSegment(SegmentType::KeySig); seg; seg = seg->next1(SegmentType::KeySig)) {
                if (EngravingItem* e = seg->element(0); e && e->isKeySig()) {
                    return int(toKeySig(e)->key());
                }
            }
            return 99;
        };
        auto clefOf = [](Score* s) {
            return s->staff(0) ? int(s->staff(0)->clef(Fraction(0, 1))) : -1;
        };
        soloTestLog(QString("  main: %1 | chord %2 | fingerings %3 | breaks %4 | key %5 | clef %6 | concert %7 | undo steps %8")
                    .arg(soloTestNotes(ms)).arg(firstChord(ms)).arg(fingerings(ms)).arg(breaks(ms)).arg(keyOf(ms)).arg(clefOf(ms))
                    .arg(int(ms->style().styleB(Sid::concertPitch))).arg(master->notation()->undoStack()->currentStateIndex()));
        const QJsonArray keys = load().solo.value("keys").toArray();
        for (const SoloKeyScore& k : soloKeyScores(master)) {
            int shift = 0;
            for (const QJsonValue& v : keys) {
                if (v.toObject().value("index").toInt() == k.index) {
                    shift = v.toObject().value("shift").toInt();
                }
            }
            const Interval iv = k.score->parts().empty() ? Interval() : k.score->parts().front()->instrument(Fraction(0, 1))->transpose();
            soloTestLog(QString("  %1 [%2]: shift %3, transposition %4/%5 | key %6 | clef %7 | %8 | chord %9 | fingerings %10 | breaks %11")
                        .arg(k.index).arg(k.name).arg(shift).arg(iv.diatonic).arg(iv.chromatic).arg(keyOf(k.score)).arg(clefOf(k.score))
                        .arg(soloTestNotes(k.score)).arg(firstChord(k.score)).arg(fingerings(k.score)).arg(breaks(k.score)));
        }
        return true;
    }
    if (step == "solosave") {
        const bool ok = projectFilesController()->saveProject();
        soloTestLog(QString("  saved: %1 (%2)").arg(int(ok)).arg(project ? project->path().toQString() : QString()));
        return true;
    }
    if (step == "soloload") {
        const QString file = project ? project->path().toQString() : QString();
        const QString tmp = QDir::tempPath() + "/StarScoreSoloTest-" + QUuid::createUuid().toString(QUuid::Id128) + ".mscz";
        QFile::copy(file, tmp);
        INotationProjectPtr copy = projectCreator()->newProject(iocContext());
        OpenParams params;
        params.skipPlaybackModel = true;
        const Ret ret = copy->load(io::path_t(tmp), params);
        QFile::remove(tmp);
        if (!ret || !copy->masterNotation()) {
            soloTestLog("  couldn't read it back: " + QString::fromStdString(ret.toString()));
            return true;
        }
        soloTestLog("  read back, main: " + soloTestNotes(copy->masterNotation()->masterScore()));
        for (const IExcerptNotationPtr& e : copy->masterNotation()->excerpts()) {
            if (e->notation() && e->notation()->elements()) {
                Score* s = e->notation()->elements()->msScore();
                const Interval iv = s->parts().empty() ? Interval() : s->parts().front()->instrument(Fraction(0, 1))->transpose();
                soloTestLog(QString("  read back %1: transposition %2/%3 | %4").arg(e->name()).arg(iv.diatonic).arg(iv.chromatic)
                            .arg(soloTestNotes(s)));
            }
        }
        return true;
    }
    if (step == "soloexport") {
        const RetVal<QString> r = exportSoloSheets();
        soloTestLog("  " + (r.ret ? r.val : QString::fromStdString(r.ret.text())));
        if (project) {
            QDirIterator it(QFileInfo(project->path().toQString()).absolutePath() + "/Sheets", { "*.pdf" }, QDir::Files,
                            QDirIterator::Subdirectories);
            QStringList files;
            while (it.hasNext()) {
                const QString f = it.next();
                files << QString("%1 (%2 bytes)").arg(f.section("/Sheets/", 1)).arg(QFileInfo(f).size());
            }
            files.sort();
            for (const QString& f : files) {
                soloTestLog("    " + f);
            }
        }
        return true;
    }
    if (step.startsWith("soloshow:")) {
        // soloshow:<name>: an old solo kept in the song shown (as clicking its chip)
        for (const StarScoreSolo& solo : solos()) {
            if (solo.name == step.mid(9)) {
                const Ret ret = showSolo(solo.id);
                soloTestLog("  " + (ret ? QString("shown") : QString::fromStdString(ret.toString())));
                return true;
            }
        }
        soloTestLog("  no such solo");
        return true;
    }
    if (step == "sololist" || step.startsWith("solomark:")) {
        // solomark:<key index>|<learned|memorized|-> on the first listed solo, then the list; sololist: the list
        if (step.startsWith("solomark:")) {
            const QJsonArray list = soloLearningList();
            const QString state = step.mid(9).section('|', 1);
            const Ret ret = list.isEmpty() ? make_ret(Ret::Code::UnknownError)
                            : setSoloKeyState(list.at(0).toObject().value("id").toString(), step.mid(9).section('|', 0, 0).toInt(),
                                              state == "-" ? QString() : state);
            soloTestLog("  mark: " + QString(ret ? "ok" : QString::fromStdString(ret.toString())));
        }
        for (const QJsonValue& v : soloLearningList()) {
            const QJsonObject e = v.toObject();
            QStringList keys;
            for (const QJsonValue& k : e.value("keys").toArray()) {
                const QString st = k.toObject().value("state").toString();
                keys << k.toObject().value("short").toString() + (st.isEmpty() ? QString() : "=" + st);
            }
            QStringList audio;
            for (const QJsonValue& a : e.value("audio").toArray()) {
                audio << a.toObject().value("name").toString();
            }
            soloTestLog(QString("  %1 | %2 – %3 (%4) exported %5 | %6 | audio: %7").arg(e.value("id").toString(), e.value("player").toString(),
                                                                                    e.value("song").toString(), e.value("instrument").toString(),
                                                                                    e.value("exported").toString(), keys.join(" "),
                                                                                    audio.join(", ")));
        }
        return true;
    }
    if (step.startsWith("soloaudioadd:")) {
        // soloaudioadd:<file>|<name>: a recording copied into the open solo's Audio folder
        const RetVal<QString> r = copySoloAudio(project ? project->path().toQString() : QString(), step.mid(13).section('|', 0, 0),
                                                step.mid(13).section('|', 1));
        soloTestLog("  " + (r.ret ? r.val : QString::fromStdString(r.ret.toString())));
        for (const QJsonValue& v : soloAudioFiles(project ? project->path().toQString() : QString())) {
            soloTestLog("    audio: " + v.toObject().value("name").toString());
        }
        return true;
    }
    if (step == "solountag") {
        // solountag: the open Solo Transcription made a plain score of one instrument again (no tag, data or part
        // scores), to try soloconvert on
        master->setExcerpts({});
        ms->metaTags().erase(String(SOLO_TAG));
        ms->metaTags().erase(u"starscore");
        m_loadCache.erase(ms);
        soloTestLog(QString("  plain score: %1 part(s), %2 part score(s), solo %3").arg(ms->parts().size())
                    .arg(master->excerpts().size()).arg(int(isSoloTranscription())));
        return true;
    }
    if (step.startsWith("soloconvert:")) {
        if (const QString projects = qEnvironmentVariable("STARSCORE_AUTOTEST_PROJECTS",
                                                          qEnvironmentVariable("STARSCORE_AUTOTEST") + "/projects"); !projects.isEmpty()) {
            QDir().mkpath(projects);
            setProjectsFolder(projects);
        }
        const QStringList a = step.mid(12).split('|');
        StarScoreSoloTranscriptionOptions o;
        o.player = a.value(0);
        o.song = a.value(1);
        o.replaceOpenScore = true;   // (not opened)
        const Ret ret = convertToSoloTranscription(o);
        soloTestLog("  " + (ret ? QString("made: ") + soloTranscriptionPath(o) : QString::fromStdString(ret.text())));
        return true;
    }
    return false;
}
