/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Watch: falling notes (1.27.0, Joel 5 Nov 2026: Synthesia "needs the falling notes … aid my
 * learning … for-fun"). The score showing (a part score, a solo's key) drawn as bars falling onto a piano keyboard, in
 * time with StarScore's playback: its speed, loop and count-in are playback's own. Drawn from the score itself, so it
 * always matches the sheet.
 */
#pragma once

#include <QColor>
#include <QElapsedTimer>
#include <QQuickPaintedItem>
#include <QTimer>
#include <qqmlintegration.h>

#include <vector>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "playback/iplaybackcontroller.h"
#include "project/internal/starscore/starscorewatch.h"

namespace mu::project {
class FallingNotesView : public QQuickPaintedItem, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    //! "hands": right hand and left hand in their own colours; "pitch": the 12 note colours of the solo sheets
    Q_PROPERTY(QString colors READ colors WRITE setColors NOTIFY settingsChanged)
    //! how many seconds of music show above the keyboard
    Q_PROPERTY(double secondsShown READ secondsShown WRITE setSecondsShown NOTIFY settingsChanged)
    Q_PROPERTY(bool dark READ dark WRITE setDark NOTIFY settingsChanged)
    Q_PROPERTY(int noteCount READ noteCount NOTIFY notesChanged)
    Q_PROPERTY(QString scoreName READ scoreName NOTIFY notesChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    //! (1.27.0) a time to show other than playback's (Listen: where the recording is, in the score's seconds); < 0: none
    Q_PROPERTY(double externalTime READ externalTime WRITE setExternalTime NOTIFY playingChanged)

    QML_ELEMENT

    muse::ContextInject<mu::context::IGlobalContext> globalContext = { this };
    muse::ContextInject<mu::playback::IPlaybackController> playbackController = { this };

public:
    explicit FallingNotesView(QQuickItem* parent = nullptr);

    QString colors() const { return m_colors; }
    void setColors(const QString& c);
    double secondsShown() const { return m_secondsShown; }
    void setSecondsShown(double s);
    bool dark() const { return m_dark; }
    void setDark(bool d);
    int noteCount() const { return int(m_notes.size()); }
    QString scoreName() const { return m_scoreName; }
    bool playing() const { return m_playing; }
    double externalTime() const { return m_externalTime; }
    void setExternalTime(double t);

    Q_INVOKABLE void reload();

    void paint(QPainter* painter) override;

    using FallNote = starscore::WatchNote;

signals:
    void settingsChanged();
    void notesChanged();
    void playingChanged();

private:
    void listen();
    double now() const;
    void scheduleReload();

    std::vector<FallNote> m_notes;
    int m_lowKey = 21;
    int m_highKey = 108;
    QString m_scoreName;
    QString m_colors = "hands";
    double m_secondsShown = 4.0;
    bool m_dark = true;
    bool m_playing = false;

    double m_externalTime = -1;
    double m_positionSecs = 0;   // the last position playback reported
    QElapsedTimer m_sincePosition;
    QTimer m_frameTimer;
    QTimer m_reloadTimer;
    mu::notation::INotationPtr m_notation;
};
}
