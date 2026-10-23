/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the practice runner (see practicerunnermodel.h)
 */
#include "practicerunnermodel.h"

#include <algorithm>
#include <cmath>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>

#include "project/internal/starscore/starscorepracticexml.h"

#include "log.h"

using namespace mu::project;

static QString runnerImageDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/StarScore Practice/runner";
    QDir().mkpath(dir);
    return dir;
}

PracticeRunnerModel::PracticeRunnerModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    QSettings settings;
    m_textScale = std::clamp(settings.value("StarScore/practiceRunner/textScale", 1.0).toDouble(), 0.7, 2.0);
    m_scoreScale = std::clamp(settings.value("StarScore/practiceRunner/scoreScale", 1.0).toDouble(), 0.6, 2.0);
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this]() { tick(); });
}

PracticeRunnerModel::~PracticeRunnerModel()
{
    for (const QString& f : m_images) {
        QFile::remove(QUrl(f).toLocalFile());
    }
}

// ---------------------------------------------------------------- properties
double PracticeRunnerModel::textScale() const
{
    return m_textScale;
}

void PracticeRunnerModel::setTextScale(double s)
{
    s = std::clamp(std::round(s * 10.0) / 10.0, 0.7, 2.0);
    if (std::abs(s - m_textScale) < 0.001) {
        return;
    }
    m_textScale = s;
    QSettings().setValue("StarScore/practiceRunner/textScale", s);
    emit scaleChanged();
}

double PracticeRunnerModel::scoreScale() const
{
    return m_scoreScale;
}

void PracticeRunnerModel::setScoreScale(double s)
{
    s = std::clamp(std::round(s * 10.0) / 10.0, 0.6, 2.0);
    if (std::abs(s - m_scoreScale) < 0.001) {
        return;
    }
    m_scoreScale = s;
    QSettings().setValue("StarScore/practiceRunner/scoreScale", s);
    emit scaleChanged();
    renderImages();   // the bars reflow at the new size
}

int PracticeRunnerModel::systemWidth() const
{
    return m_systemWidth;
}

QString PracticeRunnerModel::planName() const
{
    return m_plan.name;
}

QString PracticeRunnerModel::dayTitle() const
{
    if (m_dayIndex < 0 || m_dayIndex >= int(m_plan.days.size())) {
        return QString();
    }
    const practice::Day& d = m_plan.days[m_dayIndex];
    QString t = d.date.isValid() ? d.date.toString("dddd d MMMM") : QString("Session");
    if (m_plan.kind == "plan") {
        t = QString("Day %1 · %2").arg(m_dayIndex + 1).arg(t);
    }
    if (!d.label.isEmpty()) {
        t += " · " + d.label;
    }
    return t;
}

int PracticeRunnerModel::blockCount() const
{
    return m_dayIndex >= 0 && m_dayIndex < int(m_plan.days.size()) ? int(m_plan.days[m_dayIndex].blocks.size()) : 0;
}

int PracticeRunnerModel::blockIndex() const
{
    return m_blockIndex;
}

int PracticeRunnerModel::doneCount() const
{
    int n = 0;
    if (m_dayIndex >= 0 && m_dayIndex < int(m_plan.days.size())) {
        for (const practice::Block& b : m_plan.days[m_dayIndex].blocks) {
            n += b.done ? 1 : 0;
        }
    }
    return n;
}

QString PracticeRunnerModel::message() const
{
    return m_message;
}

const practice::Library* PracticeRunnerModel::currentLibrary() const
{
    if (m_dayIndex < 0 || m_dayIndex >= int(m_plan.days.size())) {
        return nullptr;
    }
    const auto& blocks = m_plan.days[m_dayIndex].blocks;
    if (m_blockIndex < 0 || m_blockIndex >= int(blocks.size())) {
        return nullptr;
    }
    auto it = m_libs.find(blocks[m_blockIndex].code);
    return it == m_libs.end() ? nullptr : &it->second;
}

const practice::Item* PracticeRunnerModel::currentItem() const
{
    const practice::Library* lib = currentLibrary();
    if (!lib) {
        return nullptr;
    }
    return lib->item(m_plan.days[m_dayIndex].blocks[m_blockIndex].itemId);
}

QVariantList PracticeRunnerModel::blocks() const
{
    QVariantList out;
    if (m_dayIndex < 0 || m_dayIndex >= int(m_plan.days.size())) {
        return out;
    }
    const auto& blocks = m_plan.days[m_dayIndex].blocks;
    for (int i = 0; i < int(blocks.size()); ++i) {
        const practice::Block& b = blocks[i];
        auto lib = m_libs.find(b.code);
        const practice::Item* it = lib == m_libs.end() ? nullptr : lib->second.item(b.itemId);
        out << QVariantMap { { "index", i }, { "song", lib == m_libs.end() ? b.code : lib->second.title },
                             { "title", it ? practice::prettyText(it->title) : b.itemId }, { "minutes", b.minutes }, { "done", b.done } };
    }
    return out;
}

QVariantMap PracticeRunnerModel::block() const
{
    const practice::Library* lib = currentLibrary();
    if (!lib && blockCount() == 0) {
        return {};
    }
    const practice::Block& b = m_plan.days[m_dayIndex].blocks[m_blockIndex];
    const practice::Item* it = lib ? lib->item(b.itemId) : nullptr;
    practice::Progress progress;
    progress.load();
    const practice::ItemProgress p = progress.get(b.code, b.itemId);
    QVariantMap m { { "code", b.code }, { "song", lib ? lib->title : b.code }, { "minutes", b.minutes }, { "tempo", b.tempo },
                    { "done", b.done }, { "kind", b.kind }, { "tempoReached", b.tempoReached }, { "stage", p.stage },
                    { "stageName", practice::stageName(p.stage) }, { "sessions", p.sessions }, { "bestTempo", p.bestTempo },
                    { "lastNote", p.note }, { "missing", it == nullptr } };
    if (!it) {
        m["title"] = b.itemId;
        return m;
    }
    m["title"] = practice::prettyText(it->title);
    m["category"] = it->category;
    m["categoryName"] = practice::categoryName(it->category);
    const practice::Solo* solo = lib->solo(it->solo);
    m["solo"] = solo ? practice::prettyText(solo->name) : QString();
    m["soloInfo"] = solo ? QStringList { solo->bars.isEmpty() ? QString() : "Bars " + solo->bars, solo->form, solo->changes }
                    .filter(QRegularExpression(".")).join(" · ") : QString();
    m["soloInfo"] = practice::prettyText(m["soloInfo"].toString());
    m["sections"] = it->sections.join(", ");
    m["level"] = it->level;
    m["goal"] = practice::prettyText(it->goal);
    QStringList steps;
    for (const QString& st : it->steps) {
        steps << practice::prettyText(st);
    }
    m["steps"] = steps;
    m["tempoStart"] = it->tempoStart;
    m["tempoGoal"] = it->tempoGoal;
    m["moveOnWhen"] = practice::prettyText(it->moveOnWhen);
    m["moveOnWhenHtml"] = practice::prettyText(it->moveOnWhen).toHtmlEscaped();
    m["listen"] = practice::prettyText(it->listen);
    m["refs"] = practice::prettyText(it->refs.join("; "));
    m["hasExercise"] = it->hasXml();
    m["hasBacking"] = it->hasXml() && it->xml().contains("<part id=\"P2\"");
    m["hasIreal"] = !it->irealProg.isEmpty();
    m["groove"] = it->groove;
    m["kindName"] = b.kind == "warmup" ? QString("Warm-up") : b.kind == "runthrough" ? QString("Run-through") : QString();
    return m;
}

QStringList PracticeRunnerModel::images() const
{
    return m_images;
}

bool PracticeRunnerModel::rendering() const
{
    return m_rendering;
}

int PracticeRunnerModel::secondsLeft() const
{
    return m_secondsLeft;
}

bool PracticeRunnerModel::running() const
{
    return m_timer.isActive();
}

// ---------------------------------------------------------------- loading
void PracticeRunnerModel::load(const QString& planId, int dayIndex, int blockIndex)
{
    m_plan = practice::loadPlan(planId);
    m_libs.clear();
    m_paths.clear();
    if (m_plan.id.isEmpty()) {
        m_message = QStringLiteral("This plan couldn't be read.");
        emit changed();
        return;
    }
    const QString root = practice::libraryRoot(starScore()->auditLibraryFolder());
    for (const practice::SongEntry& e : practice::scanLibraries(root, practice::practiceFolder() + "/libraries.json")) {
        m_paths[e.code] = e.path;
    }
    for (const practice::PlanSong& ps : m_plan.songs) {
        auto p = m_paths.find(ps.code);
        if (p != m_paths.end()) {
            m_libs[ps.code] = practice::cachedLibrary(p->second);
        }
    }
    m_dayIndex = std::clamp(dayIndex, 0, std::max(0, int(m_plan.days.size()) - 1));
    go(blockIndex);
}

void PracticeRunnerModel::go(int index)
{
    const int n = blockCount();
    if (n == 0) {
        m_blockIndex = 0;
        emit changed();
        return;
    }
    m_blockIndex = std::clamp(index, 0, n - 1);
    m_message.clear();
    if (m_handsFree.isEmpty()) {
        m_timer.stop();
    }
    setupParts();
    emit changed();
    emit partChanged();
    emit timerChanged();
    renderImages();
}

void PracticeRunnerModel::setupParts()
{
    m_parts.clear();
    m_partIndex = 0;
    m_passTempos.clear();
    m_passStarts.clear();
    m_playingAlong = false;
    if (blockCount() == 0) {
        m_partSecondsLeft = m_secondsLeft = 0;
        return;
    }
    const practice::Block& b = m_plan.days[m_dayIndex].blocks[m_blockIndex];
    const practice::Item* it = currentItem();
    const int total = std::max(60, b.minutes * 60);
    if (it && !it->parts.empty()) {
        // the item's parts, their minutes scaled to the block's (a block can be shorter than the item)
        int sum = 0;
        for (const practice::Part& p : it->parts) {
            sum += p.minutes;
        }
        int given = 0;
        for (size_t i = 0; i < it->parts.size(); ++i) {
            const practice::Part& p = it->parts[i];
            RunPart rp;
            rp.label = practice::prettyText(p.label);
            const double exact = double(total) * p.minutes / std::max(1, sum);
            const bool roomy = total >= 30 * int(it->parts.size());
            rp.seconds = i + 1 == it->parts.size() ? total - given
                         : roomy ? int(std::lround(exact / 15.0)) * 15   // (to 15 seconds)
                         : int(std::lround(exact));
            rp.seconds = std::max(roomy ? 15 : 5, rp.seconds);
            given += rp.seconds;
            rp.from = p.barFrom;
            rp.to = p.barTo;
            rp.notation = it->hasXml() && p.barFrom > 0;
            m_parts.push_back(rp);
        }
    } else {
        RunPart rp;
        rp.label = it ? practice::prettyText(it->title) : b.itemId;
        rp.seconds = total;
        rp.notation = it && it->hasXml();
        m_parts.push_back(rp);
    }
    m_partSecondsLeft = m_parts.front().seconds;
    m_secondsLeft = total;
}

// ---------------------------------------------------------------- parts
QVariantList PracticeRunnerModel::parts() const
{
    QVariantList out;
    for (int i = 0; i < int(m_parts.size()); ++i) {
        const RunPart& p = m_parts[i];
        out << QVariantMap { { "index", i }, { "label", p.label }, { "seconds", p.seconds },
                             { "time", QString("%1:%2").arg(p.seconds / 60).arg(p.seconds % 60, 2, 10, QChar('0')) },
                             { "hasNotation", p.notation }, { "current", i == m_partIndex }, { "finished", p.finished } };
    }
    return out;
}

int PracticeRunnerModel::partIndex() const
{
    return m_partIndex;
}

int PracticeRunnerModel::partSecondsLeft() const
{
    return m_partSecondsLeft;
}

QVariantList PracticeRunnerModel::passTempos() const
{
    QVariantList out;
    for (int t : m_passTempos) {
        out << t;
    }
    return out;
}

int PracticeRunnerModel::currentPassTempo() const
{
    if (!m_playingAlong || m_passTempos.isEmpty() || m_partIndex >= int(m_parts.size())) {
        return 0;
    }
    const int elapsed = m_parts[m_partIndex].seconds - m_partSecondsLeft;
    int t = m_passTempos.front();
    for (int i = 0; i < m_passTempos.size() && i < m_passStarts.size(); ++i) {
        if (elapsed >= m_passStarts[i]) {
            t = m_passTempos[i];
        }
    }
    return t;
}

bool PracticeRunnerModel::playingAlong() const
{
    return m_playingAlong;
}

QString PracticeRunnerModel::handsFree() const
{
    return m_handsFree;
}

int PracticeRunnerModel::breakSecondsLeft() const
{
    return m_breakSecondsLeft;
}

int PracticeRunnerModel::dayMinutesLeft() const
{
    if (blockCount() == 0) {
        return 0;
    }
    int sec = m_secondsLeft;
    const auto& blocks = m_plan.days[m_dayIndex].blocks;
    for (int i = m_blockIndex + 1; i < int(blocks.size()); ++i) {
        sec += blocks[i].done ? 0 : blocks[i].minutes * 60;
    }
    return (sec + 59) / 60;
}

QString PracticeRunnerModel::nextUp() const
{
    const int n = nextOpenBlock(m_blockIndex);
    if (n < 0) {
        return QString();
    }
    const practice::Block& b = m_plan.days[m_dayIndex].blocks[n];
    auto lib = m_libs.find(b.code);
    const practice::Item* it = lib == m_libs.end() ? nullptr : lib->second.item(b.itemId);
    return QString("%1 · %2").arg(lib == m_libs.end() ? b.code : lib->second.title, it ? practice::prettyText(it->title) : b.itemId);
}

QVariantList PracticeRunnerModel::toRate() const
{
    QVariantList out;
    for (int i = 0; i < int(m_toRate.size()); ++i) {
        const Pending& p = m_toRate[i];
        out << QVariantMap { { "index", i }, { "song", p.song }, { "title", p.title }, { "stage", p.stage } };
    }
    return out;
}

int PracticeRunnerModel::nextOpenBlock(int after) const
{
    if (blockCount() == 0) {
        return -1;
    }
    const auto& blocks = m_plan.days[m_dayIndex].blocks;
    for (int i = after + 1; i < int(blocks.size()); ++i) {
        if (!blocks[i].done) {
            return i;
        }
    }
    return -1;
}

void PracticeRunnerModel::goPart(int index)
{
    if (index < 0 || index >= int(m_parts.size())) {
        return;
    }
    const bool wasPlaying = m_playingAlong;
    m_partIndex = index;
    m_partSecondsLeft = m_parts[index].seconds;
    m_passTempos.clear();
    m_passStarts.clear();
    m_playingAlong = false;
    if (wasPlaying) {
        dispatcher()->dispatch("stop");
    }
    emit partChanged();
    emit timerChanged();
    renderImages();
}

void PracticeRunnerModel::playExample(int index)
{
    const practice::Item* it = currentItem();
    const practice::Library* lib = currentLibrary();
    if (!it || !it->hasXml() || !lib || index < 0 || index >= int(m_parts.size())) {
        return;
    }
    const RunPart& p = m_parts[index];
    const QString xml = practice::sliceExercise(it->xml(), p.from, p.to);
    const int tempo = m_plan.days[m_dayIndex].blocks[m_blockIndex].tempo;
    m_playingAlong = false;
    emit partChanged();
    starScore()->openPracticeExercise(xml, QString("%1 - %2 - example").arg(lib->title, p.label), tempo, true);
}

void PracticeRunnerModel::playAlong(int index)
{
    const practice::Item* it = currentItem();
    const practice::Library* lib = currentLibrary();
    if (!it || !it->hasXml() || !lib || index < 0 || index >= int(m_parts.size())) {
        return;
    }
    if (index != m_partIndex) {
        goPart(index);
    }
    const RunPart& p = m_parts[index];
    const practice::Block& b = m_plan.days[m_dayIndex].blocks[m_blockIndex];
    const practice::PlayAlong pa = practice::playAlong(it->xml(), p.from, p.to, b.tempo > 0 ? b.tempo : it->tempoStart,
                                                       it->tempoGoal, m_partSecondsLeft + 5);
    if (pa.xml.isEmpty()) {
        m_message = QStringLiteral("This one has no backing track: play it with the timer.");
        emit changed();
        if (!m_timer.isActive()) {
            m_timer.start();
            emit timerChanged();
        }
        return;
    }
    m_passTempos = pa.passTempos;
    m_passStarts = pa.passStarts;
    // (the passes count from now: the part's clock restarts with the music)
    m_partSecondsLeft = m_parts[index].seconds;
    m_playingAlong = true;
    emit partChanged();
    // (the automatic tests can leave the score open: the tests' steps run on the open song)
    static const bool noPlayback = qEnvironmentVariableIsSet("STARSCORE_AUTOTEST_NO_PLAYBACK");
    if (!noPlayback) {
        starScore()->openPracticeExercise(pa.xml, QString("%1 - %2 - play along").arg(lib->title, p.label), 0, true);
    } else {
        LOGI() << "[starscore] play along: " << p.label << ", " << pa.seconds << " s, passes " << pa.passTempos.size();
        QFile dump(QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/StarScore Practice/playalong-test.musicxml");
        if (dump.open(QIODevice::WriteOnly)) {
            dump.write(pa.xml.toUtf8());
        }
    }
    if (!m_timer.isActive()) {
        m_timer.start();
    }
    emit timerChanged();
}

void PracticeRunnerModel::stopPlaying()
{
    dispatcher()->dispatch("stop");
    m_playingAlong = false;
    emit partChanged();
}

void PracticeRunnerModel::next()
{
    go(m_blockIndex + 1);
}

void PracticeRunnerModel::previous()
{
    go(m_blockIndex - 1);
}

void PracticeRunnerModel::renderImages()
{
    for (const QString& f : m_images) {
        QFile::remove(QUrl(f).toLocalFile());
    }
    m_images.clear();
    const practice::Item* it = currentItem();
    if (!it || !it->hasXml()) {
        m_rendering = false;
        emit imagesChanged();
        return;
    }
    m_rendering = true;
    emit imagesChanged();
    const int serial = ++m_renderSerial;
    // the current part's bars only (all of them when the item is one part)
    QString xml = it->xml();
    if (m_partIndex < int(m_parts.size()) && m_parts[m_partIndex].from > 0) {
        xml = practice::sliceExercise(xml, m_parts[m_partIndex].from, m_parts[m_partIndex].to);
    }
    QTimer::singleShot(20, this, [this, serial, xml]() {
        if (serial != m_renderSerial) {
            return;   // moved on meanwhile
        }
        int systemWidth = 0;
        const QList<QImage> pages = starScore()->renderPracticeExercise(xml, 160, m_scoreScale, &systemWidth);
        QStringList files;
        for (int i = 0; i < pages.size(); ++i) {
            const QString f = QString("%1/exercise-%2-%3-%4.png").arg(runnerImageDir()).arg(quintptr(this), 0, 16).arg(serial).arg(i);
            if (pages[i].save(f, "PNG")) {
                files << QUrl::fromLocalFile(f).toString();
            }
        }
        m_images = files;
        m_systemWidth = systemWidth;
        m_rendering = false;
        emit imagesChanged();
    });
}

// ---------------------------------------------------------------- timer
void PracticeRunnerModel::tick()
{
    if (m_handsFree == "break") {
        if (--m_breakSecondsLeft <= 0) {
            m_breakSecondsLeft = 0;
            m_handsFree = "practice";
            emit handsFreeChanged();
            startBlockHandsFree();
        }
        emit timerChanged();
        return;
    }
    if (m_partSecondsLeft > 0) {
        --m_partSecondsLeft;
    }
    if (m_secondsLeft > 0) {
        --m_secondsLeft;
    }
    if (m_partSecondsLeft <= 0) {
        partFinished();
    }
    emit timerChanged();
}

void PracticeRunnerModel::partFinished()
{
    if (m_partIndex < int(m_parts.size())) {
        m_parts[m_partIndex].finished = true;
    }
    const bool last = m_partIndex + 1 >= int(m_parts.size());
    if (!last) {
        // the next part: its own clock, and in hands free its own play-along
        const bool along = m_playingAlong || !m_handsFree.isEmpty();
        goPart(m_partIndex + 1);
        if (along && m_parts[m_partIndex].notation) {
            playAlong(m_partIndex);
        } else if (along) {
            dispatcher()->dispatch("stop");   // (a part with nothing to play along to)
        }
        return;
    }
    // the block's time is up
    m_secondsLeft = 0;
    if (!m_handsFree.isEmpty()) {
        blockFinishedHandsFree();
        return;
    }
    m_timer.stop();
    if (m_playingAlong) {
        stopPlaying();
    }
    emit timeUp();
    emit partChanged();
}

void PracticeRunnerModel::startTimer()
{
    if (m_partSecondsLeft <= 0) {
        setupParts();
        emit partChanged();
    }
    m_timer.start();
    emit timerChanged();
}

void PracticeRunnerModel::pauseTimer()
{
    m_timer.stop();
    emit timerChanged();
}

void PracticeRunnerModel::resetTimer()
{
    m_timer.stop();
    setupParts();
    emit partChanged();
    emit timerChanged();
    renderImages();
}

// ---------------------------------------------------------------- hands free
void PracticeRunnerModel::startHandsFree()
{
    if (blockCount() == 0) {
        return;
    }
    if (m_plan.days[m_dayIndex].blocks[m_blockIndex].done) {
        const int n = nextOpenBlock(m_blockIndex);
        if (n < 0) {
            m_message = QStringLiteral("Everything today is done.");
            emit changed();
            return;
        }
        m_blockIndex = n;
    }
    m_handsFree = "practice";
    m_minutesSinceBreak = 0;
    emit handsFreeChanged();
    startBlockHandsFree();
}

void PracticeRunnerModel::startBlockHandsFree()
{
    go(m_blockIndex);
    m_timer.start();
    if (!m_parts.empty() && m_parts.front().notation) {
        playAlong(0);
    }
    emit handsFreeChanged();
    emit timerChanged();
}

void PracticeRunnerModel::blockFinishedHandsFree()
{
    const practice::Block& b = m_plan.days[m_dayIndex].blocks[m_blockIndex];
    const practice::Item* it = currentItem();
    const practice::Library* lib = currentLibrary();
    Pending p;
    p.code = b.code;
    p.itemId = b.itemId;
    p.minutes = b.minutes;
    p.tempo = m_passTempos.isEmpty() ? b.tempo : m_passTempos.back();
    p.song = lib ? lib->title : b.code;
    p.title = it ? practice::prettyText(it->title) : b.itemId;
    {
        practice::Progress progress;
        progress.load();
        p.stage = progress.get(b.code, b.itemId).stage;
    }
    m_toRate.push_back(p);
    // done in the plan now; kept in the progress when rated (or kept unrated)
    practice::Block done = b;
    done.done = true;
    done.tempoReached = p.tempo;
    m_plan.days[m_dayIndex].blocks[m_blockIndex] = done;
    savePlan(done);
    m_minutesSinceBreak += b.minutes;

    const int n = nextOpenBlock(m_blockIndex);
    if (n < 0) {
        dispatcher()->dispatch("stop");
        m_playingAlong = false;
        m_timer.stop();
        m_handsFree = "done";
        m_message = QStringLiteral("That's the day done.");
        emit changed();
        emit partChanged();
        emit handsFreeChanged();
        return;
    }
    m_blockIndex = n;
    if (m_plan.breakEvery > 0 && m_minutesSinceBreak >= m_plan.breakEvery) {
        dispatcher()->dispatch("stop");
        m_playingAlong = false;
        m_minutesSinceBreak = 0;
        m_handsFree = "break";
        m_breakSecondsLeft = m_plan.breakMinutes * 60;
        go(n);   // (the next block on screen, its music not started)
        m_timer.start();
        emit handsFreeChanged();
        return;
    }
    startBlockHandsFree();
}

void PracticeRunnerModel::pauseHandsFree()
{
    if (m_handsFree.isEmpty() || m_handsFree == "done") {
        return;
    }
    m_pausedPhase = m_handsFree;
    m_handsFree = "paused";
    m_timer.stop();
    if (m_playingAlong) {
        dispatcher()->dispatch("play");   // (toggles: pauses where it is)
    }
    emit handsFreeChanged();
    emit timerChanged();
}

void PracticeRunnerModel::resumeHandsFree()
{
    if (m_handsFree != "paused") {
        return;
    }
    m_handsFree = m_pausedPhase.isEmpty() ? QStringLiteral("practice") : m_pausedPhase;
    m_timer.start();
    if (m_playingAlong && m_handsFree == "practice") {
        dispatcher()->dispatch("play");   // (on from where it paused)
    }
    emit handsFreeChanged();
    emit timerChanged();
}

void PracticeRunnerModel::stopHandsFree()
{
    m_handsFree.clear();
    m_timer.stop();
    if (m_playingAlong) {
        stopPlaying();
    }
    emit handsFreeChanged();
    emit timerChanged();
}

void PracticeRunnerModel::skipBreak()
{
    if (m_handsFree == "break" || (m_handsFree == "paused" && m_pausedPhase == "break")) {
        m_breakSecondsLeft = 0;
        m_handsFree = "practice";
        emit handsFreeChanged();
        startBlockHandsFree();
    }
}

void PracticeRunnerModel::skipPart()
{
    m_partSecondsLeft = 0;
    partFinished();
    emit timerChanged();
}

void PracticeRunnerModel::rate(int index, int stage)
{
    if (index < 0 || index >= int(m_toRate.size())) {
        return;
    }
    const Pending p = m_toRate[index];
    m_toRate.erase(m_toRate.begin() + index);
    int blockIndex = -1;
    const auto& blocks = m_plan.days[m_dayIndex].blocks;
    for (int i = 0; i < int(blocks.size()); ++i) {
        if (blocks[i].code == p.code && blocks[i].itemId == p.itemId) {
            blockIndex = i;
        }
    }
    if (blockIndex >= 0) {
        recordDone(blockIndex, p.minutes, p.tempo, std::clamp(stage, 1, 4), QString());
    }
    emit handsFreeChanged();
}

void PracticeRunnerModel::keepUnrated()
{
    while (!m_toRate.empty()) {
        const Pending p = m_toRate.front();
        m_toRate.erase(m_toRate.begin());
        const auto& blocks = m_plan.days[m_dayIndex].blocks;
        for (int i = 0; i < int(blocks.size()); ++i) {
            if (blocks[i].code == p.code && blocks[i].itemId == p.itemId) {
                recordDone(i, p.minutes, p.tempo, -1, QString());
                break;
            }
        }
    }
    emit handsFreeChanged();
}

void PracticeRunnerModel::addMinutes(int minutes)
{
    practice::Plan latest = practice::loadPlan(m_plan.id);
    if (latest.id.isEmpty() || blockCount() == 0) {
        return;
    }
    const QDate date = m_plan.days[m_dayIndex].date;
    int dayIndex = m_dayIndex;
    for (int i = 0; i < int(latest.days.size()); ++i) {
        if (latest.days[i].date == date) {
            dayIndex = i;
        }
    }
    const int before = dayIndex < int(latest.days.size()) ? int(latest.days[dayIndex].blocks.size()) : 0;
    practice::Progress progress;
    progress.load();
    if (practice::addToDay(latest, dayIndex, minutes, m_libs, progress) == 0) {
        m_message = QStringLiteral("Nothing more to add for these songs today.");
        emit changed();
        return;
    }
    practice::savePlan(latest);
    m_plan = latest;
    m_dayIndex = dayIndex;
    m_message.clear();
    if (m_handsFree == "done") {
        m_handsFree = "practice";
        m_blockIndex = before;
        emit handsFreeChanged();
        startBlockHandsFree();
        return;
    }
    go(before);
}

// ---------------------------------------------------------------- StarScore and iReal
void PracticeRunnerModel::openExercise(int tempo, bool play)
{
    const practice::Item* it = currentItem();
    const practice::Library* lib = currentLibrary();
    if (!it || !it->hasXml() || !lib) {
        return;
    }
    const QString title = QString("%1 - %2").arg(lib->title, it->title);
    starScore()->openPracticeExercise(it->xml(), title, tempo, play);
}

void PracticeRunnerModel::openSong()
{
    const practice::Library* lib = currentLibrary();
    if (!lib) {
        return;
    }
    auto p = m_paths.find(lib->code);
    if (p == m_paths.end()) {
        return;
    }
    dispatcher()->dispatch("starscore-practice-open",
                           muse::actions::ActionData::make_arg2<QUrl, bool>(QUrl::fromLocalFile(p->second), false));
}

void PracticeRunnerModel::togglePlay()
{
    dispatcher()->dispatch("play");
}

void PracticeRunnerModel::toggleMetronome()
{
    dispatcher()->dispatch("metronome");
}

void PracticeRunnerModel::openIreal()
{
    const practice::Item* it = currentItem();
    const practice::Library* lib = currentLibrary();
    if (!it || !lib || it->irealProg.isEmpty()) {
        return;
    }
    practice::Plan one = m_plan;
    // a one-day, one-block plan: the same link the iReal page would have
    practice::Day d = m_plan.days[m_dayIndex];
    d.blocks = { m_plan.days[m_dayIndex].blocks[m_blockIndex] };
    one.days = { d };
    const QString html = practice::irealHtml(one, m_libs, 0);
    const QString file = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + "/StarScore Practice/ireal.html";
    QFile f(file);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(html.toUtf8());
        f.close();
        QDesktopServices::openUrl(QUrl::fromLocalFile(file));
    }
}

// ---------------------------------------------------------------- progress
void PracticeRunnerModel::savePlan(const practice::Block& changed)
{
    // the plan may have been planned again (or marked in another window) meanwhile: put only this block's marks into
    // the plan as it is on disk
    practice::Plan latest = practice::loadPlan(m_plan.id);
    if (latest.id.isEmpty()) {
        return;   // deleted meanwhile: not brought back
    }
    const QDate day = m_plan.days[m_dayIndex].date;
    bool found = false;
    for (practice::Day& d : latest.days) {
        if (d.date != day) {
            continue;
        }
        for (practice::Block& b : d.blocks) {
            if (b.code == changed.code && b.itemId == changed.itemId) {
                b.done = changed.done;
                b.tempoReached = changed.tempoReached;
                b.stage = changed.stage;
                found = true;
            }
        }
        if (!found && changed.done) {
            d.blocks.push_back(changed);
        }
    }
    practice::savePlan(latest);
    // carry on with the plan as saved, on the same block
    int dayIndex = m_dayIndex;
    for (int i = 0; i < int(latest.days.size()); ++i) {
        if (latest.days[i].date == day) {
            dayIndex = i;
        }
    }
    m_plan = latest;
    m_dayIndex = std::clamp(dayIndex, 0, std::max(0, int(m_plan.days.size()) - 1));
    const int n = blockCount();
    for (int i = 0; i < n; ++i) {
        const practice::Block& b = m_plan.days[m_dayIndex].blocks[i];
        if (b.code == changed.code && b.itemId == changed.itemId) {
            m_blockIndex = i;
        }
    }
    m_blockIndex = std::clamp(m_blockIndex, 0, std::max(0, n - 1));
}

void PracticeRunnerModel::recordDone(int blockIndex, int minutes, int tempoReached, int stage, const QString& note)
{
    if (blockIndex < 0 || blockIndex >= blockCount()) {
        return;
    }
    practice::Block& b = m_plan.days[m_dayIndex].blocks[blockIndex];
    b.done = true;
    b.tempoReached = tempoReached;
    if (stage >= 0) {
        b.stage = std::clamp(stage, 0, 4);
    }
    auto lib = m_libs.find(b.code);
    const practice::Item* it = lib == m_libs.end() ? nullptr : lib->second.item(b.itemId);
    practice::Progress progress;
    progress.load();
    progress.record(b.code, b.itemId, minutes, tempoReached, stage >= 0 ? b.stage : -1, note, QDate::currentDate(),
                    it ? it->level : 0, it ? it->skills : QStringList());
    progress.save();
    const int keep = m_blockIndex;
    savePlan(practice::Block(b));
    if (blockIndex != keep) {
        m_blockIndex = std::clamp(keep, 0, std::max(0, blockCount() - 1));   // (savePlan moved to the rated block)
    }
}

void PracticeRunnerModel::markDone(int tempoReached, int stage, const QString& note)
{
    if (blockCount() == 0) {
        return;
    }
    const practice::Block& b = m_plan.days[m_dayIndex].blocks[m_blockIndex];
    const int practised = std::max(1, (b.minutes * 60 - m_secondsLeft + 30) / 60);
    const int minutes = m_timer.isActive() || m_secondsLeft < b.minutes * 60 ? practised : b.minutes;
    m_timer.stop();
    if (m_playingAlong) {
        stopPlaying();
    }
    recordDone(m_blockIndex, minutes, tempoReached, stage, note);
    // (savePlan found our block again), then the next one not done
    const int at = m_blockIndex;
    const auto& blocks = m_plan.days[m_dayIndex].blocks;
    for (int i = at + 1; i < int(blocks.size()); ++i) {
        if (!blocks[i].done) {
            go(i);
            return;
        }
    }
    m_blockIndex = at;
    m_message = doneCount() >= blockCount() ? QStringLiteral("That's the day done.") : QString();
    emit changed();
    emit timerChanged();
}

void PracticeRunnerModel::markNotDone()
{
    if (blockCount() == 0) {
        return;
    }
    practice::Block& b = m_plan.days[m_dayIndex].blocks[m_blockIndex];
    b.done = false;
    b.stage = -1;
    b.tempoReached = 0;
    savePlan(practice::Block(b));
    emit changed();
}
