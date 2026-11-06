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
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/starscoreplayback.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationselection.h"
#include "notation/inotationelements.h"
#include "project/internal/starscore/starscorevideo.h"

using namespace mu::project;

PracticePanelModel::PracticePanelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void PracticePanelModel::load()
{
    // (1.29.0, Joel: a speed set here goes back to normal when Practice is closed) playback's speed and Listen's
    QTimer::singleShot(0, this, [this]() {
        muse::dock::IDockWindow* window = dockWindowProvider() ? dockWindowProvider()->window() : nullptr;
        if (!window) {
            return;
        }
        window->docksOpenStatusChanged().onReceive(this, [this, window](const QStringList& names) {
            if (!names.contains("starscorePracticePanel") || window->isDockOpen("starscorePracticePanel")) {
                return;
            }
            if (playbackController() && !qFuzzyCompare(playbackController()->tempoMultiplier(), 1.0)) {
                playbackController()->setTempoMultiplier(1.0);
            }
            if (m_listen && !qFuzzyCompare(m_listen->speed(), 1.0)) {
                m_listen->setSpeed(1.0);
                emit listenChanged();
                emit listenPositionChanged();
            }
            emit changed();
        });
    });
    if (playbackController()) {
        playbackController()->isPlayingChanged().onNotify(this, [this]() { emit changed(); });
        playbackController()->actionCheckedChanged().onReceive(this, [this](const muse::actions::ActionCode&) { emit changed(); });
    }
    if (globalContext()) {
        globalContext()->currentProjectChanged().onNotify(this, [this]() {
            jamStop();
            if (m_listen) {
                m_listen->clear();
                m_listenSong.clear();
                starscore::video::close();
            }
            if (m_mode == "listen") {
                loadListen();
            }
            restoreSettings();
            loadForm();
            emit changed();
        });
    }
    restoreSettings();
    loadForm();
    if (m_mode == "listen") {
        loadListen();
    }
    if (starScore()) {
        // (recordings added to or taken out of the song: Listen loads them again, when not playing)
        starScore()->changed().onNotify(this, [this]() {
            if (m_mode != "listen" || (m_listen && m_listen->isPlaying())) {
                return;
            }
            QStringList now;
            for (const StarScoreMedia& m : starScore()->media()) {
                if (m.kind == "full" || m.kind == "piano" || m.kind == "nopiano" || m.kind == "audio") {
                    now << m.id;
                }
            }
            QStringList loaded;
            if (m_listen) {
                for (const auto& st : m_listen->stems()) {
                    loaded << st.mediaId;
                }
            }
            if (now != loaded) {
                m_listenSong.clear();
                loadListen();
            }
        });
    }
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

void PracticePanelModel::takeRequestedMode()
{
    QSettings settings;
    const QString requested = settings.value("StarScore/practice/requestMode").toString();
    if (!requested.isEmpty()) {
        settings.remove("StarScore/practice/requestMode");
        if (requested == "watch" || requested == "jam" || requested == "listen") {
            setMode(requested);
        }
    }
}

void PracticePanelModel::setMode(const QString& m)
{
    if (m != m_mode) {
        if (m != "jam") {
            jamStop();
        } else if (playing()) {
            dispatcher()->dispatch("stop");
        }
        if (m != "listen" && m_listen) {
            m_listen->pause();
            starscore::video::close();
        }
        if (m == "listen") {
            if (playing()) {
                dispatcher()->dispatch("stop");
            }
            jamStop();
            loadListen();
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

// ---------------------------------------------------------------------------------------------------------------------
//  Listen (1.27.0)
// ---------------------------------------------------------------------------------------------------------------------

namespace {
bool isStemKind(const QString& kind)
{
    return kind == "full" || kind == "piano" || kind == "nopiano" || kind == "audio";
}

QString clock(double s)
{
    const int t = int(std::max(0.0, s));
    return QString("%1:%2").arg(t / 60).arg(t % 60, 2, 10, QChar('0'));
}
}

starscore::ListenPlayer* PracticePanelModel::listener()
{
    if (!m_listen) {
        m_listen = std::make_unique<starscore::ListenPlayer>(iocContext());
        connect(m_listen.get(), &starscore::ListenPlayer::readyChanged, this, [this]() {
            const QStringList errors = m_listen->errors();
            if (!errors.isEmpty()) {
                m_listenInfo = muse::qtrc("starscore", "Couldn't play: %1").arg(errors.join("; "));
            }
            emit listenChanged();
            emit listenPositionChanged();
        });
        connect(m_listen.get(), &starscore::ListenPlayer::playingChanged, this, [this]() {
            if (m_listen->isPlaying()) {
                m_videoTimer.start();
            }
            emit listenChanged();
        });
        connect(m_listen.get(), &starscore::ListenPlayer::positionChanged, this, [this]() { onListenPosition(); });
        m_videoTimer.setInterval(100);
        connect(&m_videoTimer, &QTimer::timeout, this, [this]() {
            if (!m_listen) {
                return;
            }
            starscore::video::follow(m_listen->position(), m_listen->speed(), m_listen->isPlaying());
            if (!m_listen->isPlaying()) {
                m_videoTimer.stop();
            }
        });
    }
    return m_listen.get();
}

void PracticePanelModel::loadListen()
{
    if (!starScore() || !globalContext() || !globalContext()->currentProject()) {
        return;
    }
    const QString song = globalContext()->currentProject()->path().toQString();
    if (m_listen && m_listenSong == song && m_listen->isReady()) {
        return;
    }
    std::vector<starscore::ListenStem> stems;
    for (const StarScoreMedia& m : starScore()->media()) {
        if (isStemKind(m.kind)) {
            stems.push_back({ m.id, m.name, m.kind, starScore()->mediaPath(m.id) });
        }
    }
    // the full mix first: heard at the start
    std::stable_sort(stems.begin(), stems.end(), [](const auto& a, const auto& b) {
        auto rank = [](const QString& k) { return k == "full" ? 0 : k == "piano" ? 1 : k == "nopiano" ? 2 : 3; };
        return rank(a.kind) < rank(b.kind);
    });
    m_listenSong = song;
    m_listenInfo.clear();
    if (stems.empty()) {
        m_listenInfo = muse::qtrc("starscore", "No recordings in this score yet. Add the full mix and the stems with “+ Audio, "
                                                "video or Transcribe! file” on the StarScore bar.");
        if (m_listen) {
            m_listen->clear();
        }
        emit listenChanged();
        return;
    }
    listener()->load(stems);
    loadBarMap();
    emit listenChanged();
}

void PracticePanelModel::loadBarMap()
{
    // the score's bars as played, in its own seconds (the bar map counts bars as the recording plays them)
    m_playedBarSeconds.clear();
    if (const auto master = globalContext()->currentMasterNotation()) {
        mu::engraving::MasterScore* ms = master->masterScore();
        for (const mu::engraving::RepeatSegment* rs : ms->repeatList()) {
            const int offset = rs->utick - rs->tick;
            for (mu::engraving::Measure* m = ms->tick2measure(mu::engraving::Fraction::fromTicks(rs->tick));
                 m && m->tick().ticks() < rs->tick + rs->len(); m = m->nextMeasure()) {
                m_playedBarSeconds.push_back(ms->utick2utime(m->tick().ticks() + offset));
            }
        }
        if (!m_playedBarSeconds.empty()) {
            m_playedBarSeconds.push_back(ms->utick2utime(ms->repeatList().empty() ? 0 : ms->repeatList().back()->utick
                                                         + ms->repeatList().back()->len()));
        }
    }
    m_barMap = starscore::BarMap();
    m_barMapInfo.clear();
    // tapped bars first (Joel's own), then the first Transcribe! file's measure markers
    QSettings s;
    s.beginGroup(songKey());
    const QStringList taps = s.value("listenTaps").toStringList();
    const int firstBar = s.value("listenFirstBar", 0).toInt();
    s.endGroup();
    if (taps.size() >= 2) {
        for (const QString& t : taps) {
            m_barMap.barTimes.push_back(t.toDouble());
        }
        m_barMap.firstBar = firstBar;
        m_barMapInfo = muse::qtrc("starscore", "Bars: %1 tapped").arg(taps.size());
    } else if (starScore()) {
        for (const StarScoreMedia& m : starScore()->media()) {
            if (m.kind != "transcribe") {
                continue;
            }
            starscore::BarMap map = starscore::transcribeBarMap(starScore()->mediaPath(m.id));
            if (!map.empty()) {
                m_barMap = map;
                m_barMapInfo = muse::qtrc("starscore", "Bars: %1 from %2").arg(map.barTimes.size()).arg(m.name);
                break;
            }
        }
    }
    if (m_barMap.empty()) {
        m_barMapInfo = muse::qtrc("starscore", "No bar map yet: tap the bars in once");
    }
}

QVariantList PracticePanelModel::stems() const
{
    QVariantList out;
    if (!m_listen) {
        return out;
    }
    for (const auto& st : m_listen->stems()) {
        out << QVariantMap { { "id", st.mediaId }, { "name", st.name }, { "kind", st.kind }, { "heard", m_listen->heard().contains(st.mediaId) } };
    }
    return out;
}

bool PracticePanelModel::listenReady() const
{
    return m_listen && m_listen->isReady();
}

bool PracticePanelModel::listenPlaying() const
{
    return m_listen && m_listen->isPlaying();
}

QString PracticePanelModel::listenTime() const
{
    return m_listen ? clock(m_listen->position()) + " / " + clock(m_listen->length()) : QString();
}

QString PracticePanelModel::listenBar() const
{
    if (!m_listen || m_barMap.empty()) {
        return QString();
    }
    const double t = m_listen->position();
    const int bar = m_barMap.barAt(t);
    if (bar < 0) {
        return muse::qtrc("starscore", "Before bar 1");
    }
    QString section;
    for (size_t i = 0; i < m_barMap.sectionTimes.size(); ++i) {
        if (m_barMap.sectionTimes[i] <= t + 1e-6) {
            section = m_barMap.sectionNames.value(int(i));
        }
    }
    return muse::qtrc("starscore", "Bar %1").arg(bar + 1) + (section.isEmpty() ? QString() : " · " + section);
}

double PracticePanelModel::listenFraction() const
{
    return m_listen && m_listen->length() > 0 ? m_listen->position() / m_listen->length() : 0.0;
}

int PracticePanelModel::listenSpeed() const
{
    return m_listen ? int(std::lround(m_listen->speed() * 100)) : 100;
}

void PracticePanelModel::setListenSpeed(int percent)
{
    listener()->setSpeed(std::clamp(percent, 10, 200) / 100.0);
    emit listenChanged();
    emit listenPositionChanged();
}

double PracticePanelModel::listenPitch() const
{
    return m_listen ? m_listen->pitch() : 0.0;
}

void PracticePanelModel::setListenPitch(double st)
{
    listener()->setPitch(st);
    emit listenChanged();
}

bool PracticePanelModel::listenLoop() const
{
    return m_listen && m_listen->hasLoop();
}

void PracticePanelModel::setFollow(bool f)
{
    m_follow = f;
    emit listenChanged();
}

void PracticePanelModel::listenPlayPause()
{
    loadListen();
    if (!listenReady()) {
        return;
    }
    if (m_listen->isPlaying()) {
        m_listen->pause();
    } else {
        if (playing()) {
            dispatcher()->dispatch("stop");
        }
        jamStop();
        m_listen->play();
    }
}

void PracticePanelModel::listenRewind()
{
    if (m_listen) {
        m_listen->seek(m_listen->hasLoop() ? 0 : 0);
        onListenPosition();
    }
}

void PracticePanelModel::listenSeekFraction(double f)
{
    if (m_listen && m_listen->length() > 0) {
        m_listen->seek(std::clamp(f, 0.0, 1.0) * m_listen->length());
        onListenPosition();
    }
}

void PracticePanelModel::listenSkip(int bars)
{
    if (!m_listen) {
        return;
    }
    const double t = m_listen->position();
    if (m_barMap.empty()) {
        m_listen->seek(t + 5.0 * bars);
    } else {
        const int bar = std::max(m_barMap.firstBar, m_barMap.barAt(t) + bars);
        m_listen->seek(m_barMap.timeOfBar(bar));
    }
    onListenPosition();
}

void PracticePanelModel::chooseStem(const QString& id, bool add)
{
    if (!m_listen) {
        return;
    }
    QStringList heard = m_listen->heard();
    if (add) {
        if (heard.contains(id)) {
            if (heard.size() > 1) {
                heard.removeAll(id);
            }
        } else {
            heard << id;
        }
    } else {
        heard = QStringList { id };
    }
    m_listen->setHeard(heard);
    emit listenChanged();
}

//! The selected bars (0-based, the score's), or false
static bool selectedBars(const mu::notation::INotationPtr& notation, int& from, int& to)
{
    const auto sel = notation && notation->interaction() ? notation->interaction()->selection() : nullptr;
    if (!sel || sel->isNone() || !sel->startMeasureBase()) {
        return false;
    }
    const auto* a = sel->startMeasureBase()->findMeasure();
    const auto* b = sel->endMeasureBase() ? sel->endMeasureBase()->findMeasure() : a;
    if (!a || !b) {
        return false;
    }
    from = a->index();
    to = b->index();
    return true;
}

void PracticePanelModel::toggleListenLoop()
{
    if (!m_listen) {
        return;
    }
    if (m_listen->hasLoop()) {
        m_listen->setLoop(0, 0);
        emit listenChanged();
        return;
    }
    int from = 0, to = 0;
    if (m_barMap.empty() || !selectedBars(globalContext()->currentNotation(), from, to)) {
        m_listenInfo = m_barMap.empty() ? muse::qtrc("starscore", "Looping bars needs the bar map: tap the bars in first.")
                       : muse::qtrc("starscore", "Select the bars to loop first.");
        emit listenChanged();
        return;
    }
    const double a = m_barMap.timeOfBar(from);
    const double b = m_barMap.timeOfBar(to + 1);
    m_listen->setLoop(a, b);
    m_listen->seek(a);
    m_listenInfo.clear();
    emit listenChanged();
}

void PracticePanelModel::goToSelection()
{
    int from = 0, to = 0;
    if (m_listen && !m_barMap.empty() && selectedBars(globalContext()->currentNotation(), from, to)) {
        m_listen->seek(m_barMap.timeOfBar(from));
        onListenPosition();
    }
}

QString PracticePanelModel::videoPath() const
{
    if (!starScore()) {
        return QString();
    }
    for (const StarScoreMedia& m : starScore()->media()) {
        if (m.kind == "video") {
            return starScore()->mediaPath(m.id);
        }
    }
    return QString();
}

bool PracticePanelModel::hasVideo() const
{
    return starscore::video::available() && !videoPath().isEmpty();
}

bool PracticePanelModel::videoShown() const
{
    return starscore::video::isOpen();
}

void PracticePanelModel::toggleVideo()
{
    if (starscore::video::isOpen()) {
        starscore::video::close();
    } else if (hasVideo()) {
        const auto project = globalContext()->currentProject();
        starscore::video::open(videoPath(), project ? project->displayName() : QString());
        if (m_listen) {
            starscore::video::follow(m_listen->position(), m_listen->speed(), m_listen->isPlaying());
            if (m_listen->isPlaying()) {
                m_videoTimer.start();
            }
        }
    }
    emit listenChanged();
}

void PracticePanelModel::startTapping()
{
    m_taps.clear();
    m_tapping = true;
    int from = 0, to = 0;
    // the first tap is the selected bar (bar 1 when none is selected)
    QSettings s;
    s.beginGroup(songKey());
    s.setValue("listenFirstBar", selectedBars(globalContext()->currentNotation(), from, to) ? from : 0);
    s.endGroup();
    m_listenInfo = muse::qtrc("starscore", "Tap at the start of each bar (press T, or the Tap button), from the bar selected in "
                                            "the score (bar 1 when none is). Press Done when finished.");
    if (m_listen && !m_listen->isPlaying()) {
        m_listen->play();
    }
    emit listenChanged();
}

void PracticePanelModel::tap()
{
    if (m_tapping && m_listen) {
        m_taps.push_back(m_listen->position());
    }
}

void PracticePanelModel::finishTapping()
{
    m_tapping = false;
    if (m_taps.size() >= 2) {
        QStringList list;
        for (double t : m_taps) {
            list << QString::number(t, 'f', 4);
        }
        QSettings s;
        s.beginGroup(songKey());
        s.setValue("listenTaps", list);
        s.endGroup();
    }
    m_listenInfo.clear();
    loadBarMap();
    emit listenChanged();
    emit listenPositionChanged();
}

void PracticePanelModel::clearTaps()
{
    QSettings s;
    s.beginGroup(songKey());
    s.remove("listenTaps");
    s.endGroup();
    loadBarMap();
    emit listenChanged();
}

void PracticePanelModel::onListenPosition()
{
    emit listenPositionChanged();
    if (!m_listen || m_barMap.empty() || !m_follow || !m_listen->isPlaying()) {
        return;
    }
    // the score follows the recording: the bar heard shown
    const int bar = m_barMap.barAt(m_listen->position());
    if (bar == m_listenBarNow || bar < 0) {
        return;
    }
    m_listenBarNow = bar;
    const auto notation = globalContext()->currentNotation();
    if (!notation || !notation->interaction() || !notation->elements() || !notation->elements()->msScore()) {
        return;
    }
    mu::engraving::Measure* m = notation->elements()->msScore()->firstMeasure();
    for (int i = 0; m && i < bar; ++i) {
        m = m->nextMeasure();
    }
    if (m) {
        notation->interaction()->showItem(m);
    }
}

double PracticePanelModel::listenScoreTime() const
{
    if (!m_listen || m_barMap.empty() || m_playedBarSeconds.size() < 2) {
        return -1;
    }
    const double t = m_listen->position();
    const int bar = m_barMap.barAt(t);
    if (bar < 0) {
        return std::max(0.0, m_playedBarSeconds.front() - (m_barMap.barTimes.front() - t));
    }
    const double a = m_barMap.timeOfBar(bar);
    const double b = m_barMap.timeOfBar(bar + 1);
    const double frac = b > a ? std::clamp((t - a) / (b - a), 0.0, 1.0) : 0.0;
    const size_t i = std::min(size_t(bar), m_playedBarSeconds.size() - 2);
    return m_playedBarSeconds[i] + frac * (m_playedBarSeconds[i + 1] - m_playedBarSeconds[i]);
}
