/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Practice panel (see practicepanelmodel.h)
 */
#include "practicepanelmodel.h"

#include <algorithm>
#include <cmath>

#include <QSettings>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/starscoreplayback.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationselection.h"

using namespace mu::project;

PracticePanelModel::PracticePanelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void PracticePanelModel::load()
{
    if (playbackController()) {
        playbackController()->isPlayingChanged().onNotify(this, [this]() { emit changed(); });
        playbackController()->actionCheckedChanged().onReceive(this, [this](const muse::actions::ActionCode&) { emit changed(); });
    }
    if (globalContext()) {
        globalContext()->currentProjectChanged().onNotify(this, [this]() {
            jamStop();
            restoreSettings();
            loadForm();
            emit changed();
        });
    }
    restoreSettings();
    loadForm();
    emit changed();
}

bool PracticePanelModel::hasScore() const
{
    return globalContext() && globalContext()->currentNotation() != nullptr;
}

bool PracticePanelModel::playing() const
{
    return playbackController() && playbackController()->isPlaying();
}

int PracticePanelModel::speed() const
{
    return playbackController() ? int(std::lround(playbackController()->tempoMultiplier() * 100.0)) : 100;
}

void PracticePanelModel::setSpeed(int percent)
{
    if (!playbackController()) {
        return;
    }
    percent = std::clamp(percent, 10, 200);
    playbackController()->setTempoMultiplier(percent / 100.0);
    emit changed();
}

bool PracticePanelModel::loop() const
{
    return playbackController() && playbackController()->actionChecked("loop");
}

bool PracticePanelModel::countIn() const
{
    return playbackController() && playbackController()->actionChecked("countin");
}

void PracticePanelModel::togglePlay()
{
    dispatcher()->dispatch("play");
}

void PracticePanelModel::rewind()
{
    dispatcher()->dispatch("rewind");
}

void PracticePanelModel::toggleLoop()
{
    // (with bars selected, playback loops those bars)
    dispatcher()->dispatch("loop");
}

void PracticePanelModel::toggleCountIn()
{
    dispatcher()->dispatch("countin");
}

void PracticePanelModel::setColors(const QString& c)
{
    if (c != m_colors) {
        m_colors = c;
        saveSettings();
        emit changed();
    }
}

void PracticePanelModel::setSecondsShown(double s)
{
    s = std::clamp(s, 1.0, 20.0);
    if (!qFuzzyCompare(s, m_secondsShown)) {
        m_secondsShown = s;
        saveSettings();
        emit changed();
    }
}

//! The song's practice settings, kept per song (by its file) in the app's settings; the falling notes' look for all
QString PracticePanelModel::songKey() const
{
    const auto project = globalContext() ? globalContext()->currentProject() : nullptr;
    const QString path = project ? project->path().toQString() : QString();
    return "StarScore/practice/song/" + QString(path).replace('/', '|');
}

void PracticePanelModel::restoreSettings()
{
    QSettings s;
    m_colors = s.value("StarScore/practice/colors", "hands").toString();
    m_secondsShown = s.value("StarScore/practice/secondsShown", 4.0).toDouble();
    m_mode = s.value("StarScore/practice/mode", "watch").toString();
    s.beginGroup(songKey());
    m_groove = s.value("groove").toString();
    m_jamBpm = s.value("bpm", 0).toInt();
    m_choruses = s.value("choruses", 3).toInt();
    m_tempoStep = s.value("tempoStep", 0).toInt();
    m_countInBars = s.value("countIn", 1).toInt();
    m_transpose = s.value("transpose", 0).toInt();
    m_selectedBarsOnly = s.value("selectedBarsOnly", false).toBool();
    m_bass = s.value("bass", true).toBool();
    m_drums = s.value("drums", true).toBool();
    m_piano = s.value("piano", true).toBool();
    s.endGroup();
}

void PracticePanelModel::saveSettings()
{
    QSettings s;
    s.setValue("StarScore/practice/colors", m_colors);
    s.setValue("StarScore/practice/secondsShown", m_secondsShown);
    s.setValue("StarScore/practice/mode", m_mode);
    s.beginGroup(songKey());
    s.setValue("groove", m_groove);
    s.setValue("bpm", m_jamBpm);
    s.setValue("choruses", m_choruses);
    s.setValue("tempoStep", m_tempoStep);
    s.setValue("countIn", m_countInBars);
    s.setValue("transpose", m_transpose);
    s.setValue("selectedBarsOnly", m_selectedBarsOnly);
    s.setValue("bass", m_bass);
    s.setValue("drums", m_drums);
    s.setValue("piano", m_piano);
    s.endGroup();
}

// ---------------------------------------------------------------------------------------------------------------------
//  Jam
// ---------------------------------------------------------------------------------------------------------------------

void PracticePanelModel::setMode(const QString& m)
{
    if (m != m_mode) {
        if (m != "jam") {
            jamStop();
        } else if (playing()) {
            dispatcher()->dispatch("stop");
        }
        m_mode = m;
        saveSettings();
        emit changed();
    }
}

QVariantList PracticePanelModel::grooves() const
{
    QVariantList out;
    for (const QString& g : starscore::jamGrooves()) {
        out << QVariantMap { { "text", starscore::jamGrooveName(g) }, { "value", g } };
    }
    return out;
}

#define PRACTICE_SETTER(fn, member, type) \
    void PracticePanelModel::fn(type v) { if (member != v) { member = v; saveSettings(); emit changed(); } }

void PracticePanelModel::setGroove(const QString& g)
{
    if (m_groove != g) {
        m_groove = g;
        saveSettings();
        emit changed();
    }
}

PRACTICE_SETTER(setTempoStep, m_tempoStep, int)
PRACTICE_SETTER(setTranspose, m_transpose, int)
PRACTICE_SETTER(setSelectedBarsOnly, m_selectedBarsOnly, bool)
PRACTICE_SETTER(setBassOn, m_bass, bool)
PRACTICE_SETTER(setDrumsOn, m_drums, bool)
PRACTICE_SETTER(setPianoOn, m_piano, bool)

void PracticePanelModel::setJamBpm(int v)
{
    v = std::clamp(v, 30, 400);
    if (m_jamBpm != v) {
        m_jamBpm = v;
        saveSettings();
        emit changed();
    }
}

void PracticePanelModel::setChoruses(int v)
{
    v = std::clamp(v, 1, 99);
    if (m_choruses != v) {
        m_choruses = v;
        saveSettings();
        emit changed();
    }
}

void PracticePanelModel::setCountInBars(int v)
{
    v = std::clamp(v, 0, 4);
    if (m_countInBars != v) {
        m_countInBars = v;
        saveSettings();
        emit changed();
    }
}

void PracticePanelModel::loadForm()
{
    const auto master = globalContext() ? globalContext()->currentMasterNotation() : nullptr;
    m_form = master ? starscore::jamForm(master->masterScore(), starScore() && starScore()->isSoloTranscription())
             : starscore::JamForm();
    if (m_form.bars.empty()) {
        m_jamInfo = m_form.why.isEmpty() ? muse::qtrc("starscore", "Open a song with chord symbols to jam over it.") : m_form.why;
    } else {
        const int beats = m_form.bars.front().beats;
        const QString auto_ = starscore::jamGrooveName(starscore::jamGrooveFor(m_form.style, beats));
        m_jamInfo = m_form.soloForm
                    ? muse::qtrc("starscore", "The solo's form: %1 bars (played %2 times in the solo). %3 bpm. Style: %4")
                    .arg(m_form.bars.size()).arg(m_form.soloChoruses).arg(m_form.bpm).arg(m_form.style.isEmpty() ? auto_ : m_form.style)
                    : muse::qtrc("starscore", "%1 bars, from bar %2. %3 bpm. Style: %4").arg(m_form.bars.size()).arg(m_form.firstBar + 1)
                    .arg(m_form.bpm).arg(m_form.style.isEmpty() ? auto_ : m_form.style);
    }
    if (m_groove.isEmpty() && !m_form.bars.empty()) {
        m_groove = starscore::jamGrooveFor(m_form.style, m_form.bars.front().beats);
    }
    if (m_jamBpm <= 0) {
        m_jamBpm = m_form.bpm > 0 ? m_form.bpm : 120;
    }
}

starscore::JamPlayer* PracticePanelModel::player()
{
    if (!m_player) {
        m_player = std::make_unique<starscore::JamPlayer>(iocContext());
        connect(m_player.get(), &starscore::JamPlayer::playingChanged, this, [this]() { emit jamChanged(); });
        connect(m_player.get(), &starscore::JamPlayer::positionChanged, this, [this]() { updateJamPosition(); });
    }
    return m_player.get();
}

bool PracticePanelModel::jamPlaying() const
{
    return m_player && m_player->isPlaying();
}

void PracticePanelModel::jamPlay()
{
    if (jamPlaying()) {
        jamStop();
        return;
    }
    if (playing()) {
        dispatcher()->dispatch("stop");   // (one band at a time)
    }
    loadForm();
    if (m_form.bars.empty()) {
        emit changed();
        return;
    }
    starscore::JamSettings js;
    js.groove = m_groove.isEmpty() ? starscore::jamGrooveFor(m_form.style, m_form.bars.front().beats) : m_groove;
    js.bpm = m_jamBpm > 0 ? m_jamBpm : m_form.bpm;
    js.choruses = m_choruses;
    js.tempoStep = m_tempoStep;
    js.countInBars = m_countInBars;
    js.bass = m_bass;
    js.drums = m_drums;
    js.piano = m_piano;
    js.seed = m_seed++;
    // a solo's key part score: its key (as its playback), then the panel's own transposition
    const auto master = globalContext()->currentMasterNotation();
    js.transpose = m_transpose + (master ? mu::engraving::starscore::playbackTranspose(master->masterScore()).chromatic : 0);
    // the selected bars only (a song)
    if (m_selectedBarsOnly && !m_form.soloForm) {
        const auto notation = globalContext()->currentNotation();
        const auto sel = notation && notation->interaction() ? notation->interaction()->selection() : nullptr;
        if (sel && sel->isRange() && sel->startMeasureBase() && sel->endMeasureBase()) {
            const auto* a = sel->startMeasureBase()->findMeasure();
            const auto* b = sel->endMeasureBase()->findMeasure();
            if (a && b) {
                js.fromBar = std::max(0, a->index() - m_form.firstBar);
                js.toBar = std::max(js.fromBar, b->index() - m_form.firstBar);
            }
        }
    }
    player()->load(starscore::makeJam(m_form, js), true);
    emit jamChanged();
}

void PracticePanelModel::jamStop()
{
    if (m_player) {
        m_player->stop();
    }
    m_jamWhere.clear();
    m_jamChord.clear();
    m_jamNext.clear();
    emit jamChanged();
}

void PracticePanelModel::updateJamPosition()
{
    if (!m_player) {
        return;
    }
    const starscore::JamBarTime* bar = m_player->barAt(m_player->position());
    if (!bar || !m_player->isPlaying()) {
        return;
    }
    const auto& bars = m_player->jam().bars;
    const int choruses = bars.empty() ? 0 : bars.back().chorus;
    if (bar->chorus == 0) {
        m_jamWhere = muse::qtrc("starscore", "Count-in");
    } else {
        m_jamWhere = muse::qtrc("starscore", "Chorus %1 of %2 · bar %3").arg(bar->chorus).arg(choruses)
                     .arg(m_form.soloForm ? bar->bar + 1 : bar->bar + m_form.firstBar + 1);
    }
    // the chord sounding: the last one at or before now in this bar (or a bar before), and the next change
    const double now = m_player->position();
    const double spq = 60.0 / std::max(1, m_jamBpm);   // (close enough for naming the chord; the bars carry the real times)
    // (in the key it plays in: a solo's key part score's, and the panel's own transposition)
    const int t = m_transpose + (globalContext()->currentMasterNotation()
                                 ? mu::engraving::starscore::playbackTranspose(globalContext()->currentMasterNotation()->masterScore()).chromatic : 0);
    auto nameOf = [t](const starscore::JamChord& c) {
        static const char* NAMES[12] = { "C", "D♭", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B" };
        if (t % 12 == 0) {
            return c.name;
        }
        QString n = QString::fromUtf8(NAMES[(((c.root + t) % 12) + 12) % 12]) + c.quality;
        if (c.bass >= 0) {
            n += "/" + QString::fromUtf8(NAMES[(((c.bass + t) % 12) + 12) % 12]);
        }
        return n;
    };
    QString current, next;
    if (bar->bar >= 0 && bar->bar < int(m_form.bars.size())) {
        const double inBar = (now - bar->start) / spq;
        for (int b = bar->bar; b >= 0 && current.isEmpty(); --b) {
            for (const auto& c : m_form.bars[size_t(b)].chords) {
                if (b < bar->bar || c.at <= inBar + 1e-6) {
                    current = nameOf(c);
                }
            }
        }
        for (int b = bar->bar; b < int(m_form.bars.size()) && next.isEmpty(); ++b) {
            for (const auto& c : m_form.bars[size_t(b)].chords) {
                if ((b > bar->bar || c.at > inBar + 1e-6) && nameOf(c) != current && next.isEmpty()) {
                    next = nameOf(c);
                }
            }
        }
    }
    {
        m_jamChord = current;
        m_jamNext = next;
        emit jamChanged();
    }
}
