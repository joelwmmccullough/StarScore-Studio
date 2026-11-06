/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Watch: falling notes (see fallingnotesview.h)
 */
#include "fallingnotesview.h"

#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPainterPath>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QWheelEvent>

#include "engraving/dom/chord.h"
#include "engraving/dom/chordrest.h"
#include "engraving/dom/note.h"
#include "engraving/dom/score.h"


using namespace mu::project;

namespace {
// C, C#, D, D#, E, F, F#, G, G#, A, A#, B: the solo sheets' note colours (starscoreengraving.cpp)
const char* PITCH_COLORS[12] = {
    "#C7C102", "#FF5DA1", "#028800", "#2DD3C0", "#001CA4", "#CC75F8",
    "#92D303", "#F47700", "#915700", "#AD000E", "#829EFF", "#770096"
};
const char* HAND_COLORS[3] = { "#3FA9F5", "#7CCB4A", "#F5A623" };   // right, left, a third staff

bool isBlack(int pitch)
{
    const int pc = ((pitch % 12) + 12) % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

//! White keys from the lowest key up to this one (its left edge, in white-key widths); a black key sits across the
//! line between two white keys
double keyX(int pitch, int low)
{
    auto whiteIndex = [](int p) {
        const int octave = int(std::floor(p / 12.0));
        static const int whiteBefore[12] = { 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6 };
        return octave * 7 + whiteBefore[((p % 12) + 12) % 12];
    };
    const double x = whiteIndex(pitch) - whiteIndex(low);
    return isBlack(pitch) ? x - 0.3 : x;
}
}

FallingNotesView::FallingNotesView(QQuickItem* parent)
    : QQuickPaintedItem(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    setAntialiasing(true);
    setAcceptedMouseButtons(Qt::LeftButton);
    m_seekTimer.setSingleShot(true);
    m_seekTimer.setInterval(40);
    connect(&m_seekTimer, &QTimer::timeout, this, [this]() {
        if (m_seekTarget >= 0 && playbackController()) {
            playbackController()->seekToSecs(muse::audio::secs_t(m_seekTarget), true);
        }
        m_seekTarget = -1;
    });
    m_frameTimer.setInterval(16);
    connect(&m_frameTimer, &QTimer::timeout, this, &FallingNotesView::onFrame);
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(300);
    connect(&m_reloadTimer, &QTimer::timeout, this, &FallingNotesView::reload);
    m_sincePosition.start();
    QTimer::singleShot(0, this, [this]() {
        listen();
        reload();
        if (qEnvironmentVariableIsSet("STARSCORE_AUTOTEST_WAIT")) {   // (the autotest's pictures of Wait for me)
            QTimer::singleShot(1500, this, [this]() {
                reload();
                const QString hands = qEnvironmentVariable("STARSCORE_AUTOTEST_WAIT");
                if (hands == "right" || hands == "left") {
                    setWaitHands(hands);
                }
                setWaitMode(true);
                emit waitChanged();
            });
        }
    });
}

void FallingNotesView::setColors(const QString& c)
{
    if (c != m_colors) {
        m_colors = c;
        emit settingsChanged();
        update();
    }
}

void FallingNotesView::setSecondsShown(double s)
{
    s = std::clamp(s, 1.0, 20.0);
    if (!qFuzzyCompare(s, m_secondsShown)) {
        m_secondsShown = s;
        emit settingsChanged();
        update();
    }
}

void FallingNotesView::setDark(bool d)
{
    if (d != m_dark) {
        m_dark = d;
        emit settingsChanged();
        update();
    }
}

void FallingNotesView::listen()
{
    if (!globalContext() || !playbackController()) {
        return;
    }
    globalContext()->currentNotationChanged().onNotify(this, [this]() { scheduleReload(); });
    playbackController()->currentPlaybackPositionChanged().onReceive(this, [this](muse::audio::secs_t secs, muse::midi::tick_t) {
        m_positionSecs = double(secs);
        m_sincePosition.restart();
        if (!m_playing) {
            update();
        }
    });
    playbackController()->isPlayingChanged().onNotify(this, [this]() {
        m_playing = playbackController()->isPlaying();
        m_sincePosition.restart();
        if (m_playing || m_waitMode) {
            m_frameTimer.start();
        } else {
            m_frameTimer.stop();
        }
        emit playingChanged();
        update();
    });
    // (1.27.2) the speed changes the notes' seconds
    playbackController()->currentTempoChanged().onNotify(this, [this]() {
        if (!qFuzzyCompare(playbackController()->tempoMultiplier(), m_loadedSpeed)) {   // (not a tempo marking)
            scheduleReload();
        }
    });
    if (midiInPort()) {
        midiInPort()->deviceChanged().onNotify(this, [this]() { updateWaitStatus(); });
        midiInPort()->eventReceived().onReceive(this, [this](muse::midi::tick_t, const muse::midi::Event& e) {
            if (!m_waitMode) {
                return;
            }
            using Op = muse::midi::Event::Opcode;
            if (e.opcode() == Op::NoteOn && e.velocity7() > 0) {
                m_wait.noteOn(e.note());
            } else if (e.opcode() == Op::NoteOn || e.opcode() == Op::NoteOff) {
                m_wait.noteOff(e.note());
            } else {
                return;
            }
            m_waitTick.restart();   // (the clock moves on from the moment the notes were played)
            updateWaitStatus();
            update();
        });
    }
}

void FallingNotesView::onFrame()
{
    if (m_waitMode) {
        const double dt = std::min<qint64>(m_waitTick.restart(), 100) / 1000.0;
        const bool wasHolding = m_wait.holding();
        m_wait.advance(dt);
        const std::vector<size_t> due = m_wait.takeOthersDue();
        if (m_waitAccompany) {
            playOthers(due);
        }
        if (m_wait.holding() != wasHolding) {
            updateWaitStatus();
            if (m_wait.holding()) {
                showNextInScore();
            }
        }
        if (m_wait.finished() && m_wait.clock() > m_wait.end() + 1.0 && !m_playing) {
            m_frameTimer.stop();
            updateWaitStatus();
        }
    }
    update();
}

void FallingNotesView::showNextInScore()
{
    // the playback cursor to the notes waited for, so they can be found in the sheet
    if (!m_notation || !m_notation->elements() || !playbackController() || m_wait.nextTick() < 0) {
        return;
    }
    mu::engraving::Score* score = m_notation->elements()->msScore();
    if (!score) {
        return;
    }
    mu::engraving::ChordRest* cr = score->findCR(mu::engraving::Fraction::fromTicks(m_wait.nextTick()), m_wait.nextTrack());
    if (cr && cr->tick().ticks() == m_wait.nextTick()) {
        playbackController()->seekElement(cr, false);
    }
}

void FallingNotesView::setWaitAccompany(bool on)
{
    if (on != m_waitAccompany) {
        m_waitAccompany = on;
        emit waitChanged();
    }
}

void FallingNotesView::playOthers(const std::vector<size_t>& noteIndexes)
{
    if (noteIndexes.empty() || !m_notation || !m_notation->elements() || !playbackController()) {
        return;
    }
    mu::engraving::Score* score = m_notation->elements()->msScore();
    if (!score) {
        return;
    }
    for (size_t i : noteIndexes) {
        if (i >= m_notes.size() || m_notes[i].noteIdx < 0) {
            continue;   // (grace notes: left out)
        }
        const FallNote& n = m_notes[i];
        mu::engraving::ChordRest* cr = score->findCR(mu::engraving::Fraction::fromTicks(n.tick), n.track);
        if (!cr || !cr->isChord() || cr->tick().ticks() != n.tick) {
            continue;
        }
        const mu::engraving::Chord* chord = mu::engraving::toChord(cr);
        if (n.noteIdx >= int(chord->notes().size())) {
            continue;
        }
        mu::playback::IPlaybackController::PlayParams params;
        params.flushSound = false;
        params.duration = muse::mpe::duration_t(std::max(0.05, n.end - n.start) * 1000000.0);
        const std::vector<const mu::engraving::EngravingItem*> notes { chord->notes()[n.noteIdx] };
        playbackController()->playElements(notes, params, false);
    }
}

int FallingNotesView::waitHandsMask() const
{
    return m_waitHands == "right" ? 1 : m_waitHands == "left" ? 6 : 7;
}

bool FallingNotesView::midiConnected() const
{
    return midiInPort() && midiInPort()->isConnected();
}

void FallingNotesView::setWaitMode(bool on)
{
    if (on == m_waitMode) {
        return;
    }
    m_waitMode = on;
    if (on) {
        // the MIDI keyboard mustn't write into the score while practising
        if (m_notation && m_notation->interaction()) {
            if (m_notation->interaction()->noteInput()->isNoteInputMode()) {
                m_notation->interaction()->noteInput()->endNoteInput();
            }
            m_notation->interaction()->clearSelection();
        }
        m_wait.setNotes(m_notes, waitHandsMask());
        m_wait.start(m_positionSecs);
        m_waitTick.restart();
        m_frameTimer.start();
        if (m_wait.holding()) {
            showNextInScore();
        }
    } else if (!m_playing) {
        m_frameTimer.stop();
    }
    updateWaitStatus();
    update();
}

void FallingNotesView::setWaitHands(const QString& h)
{
    if (h == m_waitHands) {
        return;
    }
    m_waitHands = h;
    m_wait.setNotes(m_notes, waitHandsMask());
    updateWaitStatus();
    update();
}

void FallingNotesView::waitRestart(double from)
{
    m_wait.start(from < 0 ? m_positionSecs : from);
    m_waitTick.restart();
    if (m_waitMode) {
        m_frameTimer.start();
        if (m_wait.holding()) {
            showNextInScore();
        }
    }
    updateWaitStatus();
    update();
}

void FallingNotesView::updateWaitStatus()
{
    QString s;
    if (m_waitMode) {
        if (!midiConnected()) {
            s = tr("No MIDI keyboard connected: choose one in Preferences › Audio & MIDI.") + "  ";
        }
        int bar = 0;
        const auto it = std::upper_bound(m_bars.begin(), m_bars.end(), m_wait.clock() + 0.001,
                                         [](double v, const starscore::WatchBar& b) { return v < b.start; });
        if (it != m_bars.begin()) {
            bar = std::prev(it)->number;
        }
        if (m_wait.finished()) {
            s += tr("Done: %1 right, %2 wrong").arg(m_wait.rightCount()).arg(m_wait.wrongCount());
        } else {
            if (bar > 0) {
                s += tr("Bar %1").arg(bar) + " · ";
            }
            s += (m_wait.holding() ? tr("play the lit keys") : tr("get ready")) + " · "
                 + tr("%1 right, %2 wrong").arg(m_wait.rightCount()).arg(m_wait.wrongCount());
        }
    }
    if (s != m_waitStatus) {
        m_waitStatus = s;
    }
    emit waitChanged();
}

void FallingNotesView::scheduleReload()
{
    // (after a solo's key has set its playback transposition, which the notes' playing pitch includes)
    m_reloadTimer.start();
}

void FallingNotesView::setExternalTime(double t)
{
    if (t != m_externalTime) {
        m_externalTime = t;
        update();
    }
}

double FallingNotesView::now() const
{
    if (m_waitMode) {
        return m_wait.clock();
    }
    if (m_externalTime >= 0) {
        return m_externalTime;
    }
    if (!m_playing) {
        return m_positionSecs;
    }
    // between playback's reports, the music moves on at its own speed (the reported seconds already include the
    // playback speed)
    return m_positionSecs + std::min<qint64>(m_sincePosition.elapsed(), 500) / 1000.0;
}

void FallingNotesView::reload()
{
    m_notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (m_notation) {
        m_notation->notationChanged().onNotify(this, [this]() { scheduleReload(); }, muse::async::Asyncable::Mode::SetReplace);
    }
    m_notes = starscore::watchNotes(m_notation, &m_scoreName, &m_bars);
    // (1.27.2) Wait for me: the same place in the music after a change of speed
    const double speed = playbackController() ? playbackController()->tempoMultiplier() : 1.0;
    const double waitAt = m_wait.clock() * (speed > 0 ? m_loadedSpeed / speed : 1.0);
    m_loadedSpeed = speed > 0 ? speed : 1.0;
    m_wait.setNotes(m_notes, waitHandsMask());
    m_wait.seek(waitAt);
    // the keyboard: the notes' range, whole octaves C to C, at least two octaves, within the piano's 88 keys
    int low = 127, high = 0;
    for (const FallNote& n : m_notes) {
        low = std::min(low, n.pitch);
        high = std::max(high, n.pitch);
    }
    if (m_notes.empty()) {
        low = 48;
        high = 84;
    }
    low = std::max(21, (low / 12) * 12);
    high = std::min(108, (high / 12 + 1) * 12);
    if (high - low < 24) {
        high = std::min(108, low + 24);
    }
    m_lowKey = low;
    m_highKey = high;
    emit notesChanged();
    update();
}

double FallingNotesView::fallHeight() const
{
    const double h = boundingRect().height();
    return std::max(1.0, h - std::clamp(h * 0.22, 40.0, 120.0));
}

void FallingNotesView::scrollBy(double secs)
{
    if (m_externalTime >= 0) {
        // Listen: the recording moves by bars (its own clock); a notch or a screen-height of dragging is about a bar
        m_wheelSteps += secs / std::max(0.5, m_secondsShown / 4.0);
        const int steps = int(m_wheelSteps);
        if (steps != 0) {
            m_wheelSteps -= steps;
            emit scrollSteps(steps);
        }
        return;
    }
    double end = m_notes.empty() ? 0.0 : m_notes.back().end;
    for (const FallNote& n : m_notes) {
        end = std::max(end, n.end);
    }
    const double to = std::clamp(now() + secs, 0.0, std::max(0.0, end));
    if (m_waitMode) {
        m_wait.seek(to);
        m_waitTick.restart();
        if (m_wait.holding()) {
            showNextInScore();
        }
        updateWaitStatus();
    } else {
        m_positionSecs = to;
        m_sincePosition.restart();
        m_seekTarget = to;
        if (!m_seekTimer.isActive()) {
            m_seekTimer.start();
        }
    }
    update();
}

void FallingNotesView::wheelEvent(QWheelEvent* event)
{
    // up (or two fingers up): on through the music, the notes coming down; down: back
    double secs = 0;
    if (!event->pixelDelta().isNull()) {
        secs = event->pixelDelta().y() / fallHeight() * m_secondsShown;
    } else {
        secs = event->angleDelta().y() / 120.0 * m_secondsShown * 0.1;
    }
    if (secs != 0) {
        scrollBy(secs);
    }
    event->accept();
}

void FallingNotesView::mousePressEvent(QMouseEvent* event)
{
    m_dragging = true;
    m_dragLastY = event->position().y();
    event->accept();
}

void FallingNotesView::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging) {
        return;
    }
    // the notes follow the pointer: dragged down, the music moves on
    const double dy = event->position().y() - m_dragLastY;
    m_dragLastY = event->position().y();
    scrollBy(dy / fallHeight() * m_secondsShown);
    event->accept();
}

void FallingNotesView::mouseReleaseEvent(QMouseEvent* event)
{
    m_dragging = false;
    event->accept();
}

QColor FallingNotesView::keyColor(int k, bool down, const QColor& noteColor) const
{
    if (!m_waitMode) {
        return down ? noteColor : QColor();
    }
    // Wait for me: a wrong key red, a key held its note's colour, the keys waited for lit faintly
    if (m_wait.held(k)) {
        if (m_wait.wrong(k)) {
            return QColor("#E0302A");
        }
        return down ? QColor(noteColor.red(), noteColor.green(), noteColor.blue()) : QColor("#4CC36A");
    }
    if (m_wait.holding() && m_wait.expected(k) && !m_wait.hit(k)) {
        QColor c = down ? noteColor : QColor("#4CC36A");
        c.setAlpha(150);
        return c;
    }
    if (down) {
        QColor c = noteColor;
        c.setAlpha(std::min(c.alpha(), 90));
        return c;
    }
    return QColor();
}

void FallingNotesView::paint(QPainter* p)
{
    const QRectF r = boundingRect();
    const QColor bg = m_dark ? QColor("#15120E") : QColor("#F4F1EA");
    p->fillRect(r, bg);
    if (r.width() < 50 || r.height() < 60) {
        return;
    }
    const double keyboardH = std::clamp(r.height() * 0.22, 40.0, 120.0);
    const double fallH = r.height() - keyboardH;
    const double whites = keyX(m_highKey, m_lowKey) + 1;
    const double ww = r.width() / whites;
    const double t = now();
    const double pps = fallH / m_secondsShown;   // pixels per second

    // guide lines at each C and F
    p->setPen(QPen(m_dark ? QColor(255, 255, 255, 22) : QColor(0, 0, 0, 22), 1));
    for (int k = m_lowKey; k <= m_highKey; ++k) {
        if (k % 12 == 0 || k % 12 == 5) {
            const double x = keyX(k, m_lowKey) * ww;
            p->drawLine(QPointF(x, 0), QPointF(x, fallH));
        }
    }

    // (1.27.2) a line where each bar starts, its number at the left (drawn over the notes, below), so a falling note
    // can be found in the sheet
    std::vector<std::pair<double, int> > barNumbers;
    {
        p->setPen(QPen(m_dark ? QColor(255, 255, 255, 56) : QColor(0, 0, 0, 64), 1));
        const auto firstBar = std::lower_bound(m_bars.begin(), m_bars.end(), t,
                                               [](const starscore::WatchBar& b, double v) { return b.start < v; });
        double lastNumberY = 1e9;
        for (auto it = firstBar; it != m_bars.end() && it->start <= t + m_secondsShown; ++it) {
            const double y = fallH - (it->start - t) * pps;
            p->drawLine(QPointF(0, y), QPointF(r.width(), y));
            if (lastNumberY - y >= 16 && y >= 14) {   // (bars too close together: not every number)
                barNumbers.push_back({ y, it->number });
                lastNumberY = y;
            }
        }
    }

    // the falling notes
    std::vector<bool> down(128, false);
    std::vector<QColor> downColor(128);
    const auto first = std::lower_bound(m_notes.begin(), m_notes.end(), t - 30.0,
                                        [](const FallNote& n, double v) { return n.start < v; });
    for (auto it = first; it != m_notes.end() && it->start <= t + m_secondsShown; ++it) {
        const FallNote& n = *it;
        if (n.end < t || n.pitch < m_lowKey || n.pitch > m_highKey) {
            continue;
        }
        QColor c = m_colors == "pitch" ? QColor(PITCH_COLORS[((n.pitch % 12) + 12) % 12]) : QColor(HAND_COLORS[n.hand]);
        if (m_waitMode && !m_wait.waitsFor(n)) {
            c.setAlpha(80);   // (Wait for me: the hand not being played, faint)
        }
        const bool black = isBlack(n.pitch);
        const double x = keyX(n.pitch, m_lowKey) * ww + (black ? 0.0 : ww * 0.08);
        const double w = black ? ww * 0.6 : ww * 0.84;
        const double yBottom = fallH - (n.start - t) * pps;
        double yTop = fallH - (n.end - t) * pps;
        if (yBottom - yTop > 4) {
            yTop += std::min(2.0, (yBottom - yTop) * 0.2);   // (1.29.0) a gap before the next sound: repeats show apart
        }
        QRectF rect(x, std::max(0.0, yTop), w, std::min(fallH, yBottom) - std::max(0.0, yTop));
        if (rect.height() > 0.5) {
            QPainterPath path;
            path.addRoundedRect(rect, std::min(4.0, w / 3), std::min(4.0, w / 3));
            QColor fill = black ? c.darker(125) : c;
            QColor edge = c.darker(160);
            fill.setAlpha(c.alpha());
            edge.setAlpha(c.alpha());
            p->fillPath(path, fill);
            p->setPen(QPen(edge, 1));
            p->drawPath(path);
        }
        if (n.start <= t && n.end > t) {
            down[n.pitch] = true;
            downColor[n.pitch] = c;
        }
    }

    if (!barNumbers.empty()) {
        QFont f = p->font();
        f.setPixelSize(10);
        p->setFont(f);
        const QFontMetricsF fm(f);
        for (const auto& [y, number] : barNumbers) {
            const QString text = QString::number(number);
            const QRectF box(2, y - 14, fm.horizontalAdvance(text) + 6, 13);
            p->fillRect(box, m_dark ? QColor(21, 18, 14, 190) : QColor(244, 241, 234, 210));
            p->setPen(m_dark ? QColor(255, 255, 255, 150) : QColor(0, 0, 0, 160));
            p->drawText(box, Qt::AlignCenter, text);
        }
    }

    // the keyboard: white keys, then black ones over them; keys sounding now lit in their note's colour
    const double ky = fallH;
    p->fillRect(QRectF(0, ky - 2, r.width(), 2), m_dark ? QColor("#AD000E") : QColor("#8A6D3B"));
    for (int k = m_lowKey; k <= m_highKey; ++k) {
        if (isBlack(k)) {
            continue;
        }
        const QRectF key(keyX(k, m_lowKey) * ww, ky, ww, keyboardH);
        p->fillRect(key.adjusted(0.5, 0, -0.5, 0), QColor("#FAFAF7"));
        if (const QColor kc = keyColor(k, down[k], downColor[k]); kc.isValid()) {
            QColor lit = kc.alpha() == 255 ? kc.lighter(130) : kc;
            p->fillRect(key.adjusted(0.5, 0, -0.5, 0), lit);
        }
        p->setPen(QPen(QColor("#555555"), 1));
        p->drawRect(key);
        if (k % 12 == 0 && ww > 9) {
            p->setPen(QColor("#777777"));
            QFont f = p->font();
            f.setPixelSize(int(std::clamp(ww * 0.45, 7.0, 12.0)));
            p->setFont(f);
            p->drawText(key.adjusted(0, 0, 0, -3), Qt::AlignHCenter | Qt::AlignBottom, QString("C%1").arg(k / 12 - 1));
        }
    }
    for (int k = m_lowKey; k <= m_highKey; ++k) {
        if (!isBlack(k)) {
            continue;
        }
        const QRectF key(keyX(k, m_lowKey) * ww, ky, ww * 0.6, keyboardH * 0.62);
        p->fillRect(key, QColor("#1A1A1A"));
        if (const QColor kc = keyColor(k, down[k], downColor[k]); kc.isValid()) {
            p->fillRect(key, kc);
        }
    }
}
