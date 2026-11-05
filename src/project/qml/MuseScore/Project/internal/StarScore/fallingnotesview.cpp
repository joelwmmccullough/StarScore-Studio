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
    m_frameTimer.setInterval(16);
    connect(&m_frameTimer, &QTimer::timeout, this, [this]() { update(); });
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(300);
    connect(&m_reloadTimer, &QTimer::timeout, this, &FallingNotesView::reload);
    m_sincePosition.start();
    QTimer::singleShot(0, this, [this]() {
        listen();
        reload();
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
        if (m_playing) {
            m_frameTimer.start();
        } else {
            m_frameTimer.stop();
        }
        emit playingChanged();
        update();
    });
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
    m_notes = starscore::watchNotes(m_notation, &m_scoreName);
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
        const QColor c = m_colors == "pitch" ? QColor(PITCH_COLORS[((n.pitch % 12) + 12) % 12]) : QColor(HAND_COLORS[n.hand]);
        const bool black = isBlack(n.pitch);
        const double x = keyX(n.pitch, m_lowKey) * ww + (black ? 0.0 : ww * 0.08);
        const double w = black ? ww * 0.6 : ww * 0.84;
        const double yBottom = fallH - (n.start - t) * pps;
        const double yTop = fallH - (n.end - t) * pps;
        QRectF rect(x, std::max(0.0, yTop), w, std::min(fallH, yBottom) - std::max(0.0, yTop));
        if (rect.height() > 0.5) {
            QPainterPath path;
            path.addRoundedRect(rect, std::min(4.0, w / 3), std::min(4.0, w / 3));
            p->fillPath(path, black ? c.darker(125) : c);
            p->setPen(QPen(c.darker(160), 1));
            p->drawPath(path);
        }
        if (n.start <= t && n.end > t) {
            down[n.pitch] = true;
            downColor[n.pitch] = c;
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
        p->fillRect(key.adjusted(0.5, 0, -0.5, 0), down[k] ? downColor[k].lighter(130) : QColor("#FAFAF7"));
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
        p->fillRect(key, down[k] ? downColor[k] : QColor("#1A1A1A"));
    }
}
