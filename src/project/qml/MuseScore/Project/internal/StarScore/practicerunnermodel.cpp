/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the practice runner (see practicerunnermodel.h)
 */
#include "practicerunnermodel.h"

#include <algorithm>

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

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
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        if (m_secondsLeft > 0) {
            --m_secondsLeft;
        }
        if (m_secondsLeft <= 0) {
            m_timer.stop();
            emit timeUp();
        }
        emit timerChanged();
    });
}

// ---------------------------------------------------------------- properties
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
    m["listen"] = practice::prettyText(it->listen);
    m["refs"] = practice::prettyText(it->refs.join("; "));
    m["hasExercise"] = !it->xml.isEmpty();
    m["hasBacking"] = it->xml.contains("<part id=\"P2\"");
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
    m_timer.stop();
    m_secondsLeft = m_plan.days[m_dayIndex].blocks[m_blockIndex].minutes * 60;
    emit changed();
    emit timerChanged();
    renderImages();
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
    if (!it || it->xml.isEmpty()) {
        m_rendering = false;
        emit imagesChanged();
        return;
    }
    m_rendering = true;
    emit imagesChanged();
    const int serial = ++m_renderSerial;
    const QString xml = it->xml;
    QTimer::singleShot(20, this, [this, serial, xml]() {
        if (serial != m_renderSerial) {
            return;   // moved on meanwhile
        }
        const QList<QImage> pages = starScore()->renderPracticeExercise(xml, 160);
        QStringList files;
        for (int i = 0; i < pages.size(); ++i) {
            const QString f = QString("%1/exercise-%2-%3.png").arg(runnerImageDir()).arg(serial).arg(i);
            if (pages[i].save(f, "PNG")) {
                files << QUrl::fromLocalFile(f).toString();
            }
        }
        m_images = files;
        m_rendering = false;
        emit imagesChanged();
    });
}

// ---------------------------------------------------------------- timer
void PracticeRunnerModel::startTimer()
{
    if (m_secondsLeft <= 0 && blockCount() > 0) {
        m_secondsLeft = m_plan.days[m_dayIndex].blocks[m_blockIndex].minutes * 60;
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
    m_secondsLeft = blockCount() > 0 ? m_plan.days[m_dayIndex].blocks[m_blockIndex].minutes * 60 : 0;
    emit timerChanged();
}

// ---------------------------------------------------------------- StarScore and iReal
void PracticeRunnerModel::openExercise(int tempo, bool play)
{
    const practice::Item* it = currentItem();
    const practice::Library* lib = currentLibrary();
    if (!it || it->xml.isEmpty() || !lib) {
        return;
    }
    const QString title = QString("%1 - %2").arg(lib->title, it->title);
    starScore()->openPracticeExercise(it->xml, title, tempo, play);
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
void PracticeRunnerModel::savePlan()
{
    // the plan may have been planned again from the Practice page meanwhile: merge this day's done marks into it
    practice::Plan latest = practice::loadPlan(m_plan.id);
    if (!latest.id.isEmpty() && m_dayIndex < int(m_plan.days.size())) {
        for (practice::Day& d : latest.days) {
            if (d.date != m_plan.days[m_dayIndex].date) {
                continue;
            }
            for (const practice::Block& mine : m_plan.days[m_dayIndex].blocks) {
                bool found = false;
                for (practice::Block& b : d.blocks) {
                    if (b.code == mine.code && b.itemId == mine.itemId) {
                        b.done = mine.done;
                        b.tempoReached = mine.tempoReached;
                        b.stage = mine.stage;
                        found = true;
                    }
                }
                if (!found && mine.done) {
                    d.blocks.push_back(mine);
                }
            }
            m_plan.days[m_dayIndex].blocks = d.blocks;
        }
        // keep our index on the same block
        practice::savePlan(latest);
        m_plan = latest;
        return;
    }
    practice::savePlan(m_plan);
}

void PracticeRunnerModel::markDone(int tempoReached, int stage, const QString& note)
{
    if (blockCount() == 0) {
        return;
    }
    practice::Block& b = m_plan.days[m_dayIndex].blocks[m_blockIndex];
    const QString code = b.code;
    const QString itemId = b.itemId;
    const int practised = std::max(1, (b.minutes * 60 - m_secondsLeft + 30) / 60);
    b.done = true;
    b.tempoReached = tempoReached;
    b.stage = std::clamp(stage, 0, 4);
    practice::Progress progress;
    progress.load();
    progress.record(code, itemId, m_timer.isActive() || m_secondsLeft < b.minutes * 60 ? practised : b.minutes, tempoReached,
                    b.stage, note);
    progress.save();
    m_timer.stop();
    savePlan();
    // find our block again (the plan may have changed), then go to the next one not done
    int at = m_blockIndex;
    const auto& blocks = m_plan.days[m_dayIndex].blocks;
    for (int i = 0; i < int(blocks.size()); ++i) {
        if (blocks[i].code == code && blocks[i].itemId == itemId) {
            at = i;
        }
    }
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
    savePlan();
    emit changed();
}
