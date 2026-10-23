/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the practice runner: one day of a practice plan, a block at a time. What to play and how, the
 * exercise's notation, a timer, the exercise opened in StarScore at today's tempo with its backing (and the
 * metronome), iReal Pro, and marking a block done (the tempo reached and how solid it is), which is kept as progress.
 *
 * 1.20 (Joel, 23 Oct 2026): an item's parts each have their own timer, a "Play example" (the part's bars as written),
 * and a "Play along" (the backing, looped for the part's minutes, stepping up from today's tempo to the goal). Hands
 * free: one press plays the whole day, part after part, with breaks (and quick ratings in the breaks).
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "actions/iactionsdispatcher.h"
#include "project/istarscoreservice.h"
#include "project/internal/starscore/starscorepractice.h"

namespace mu::project {
class PracticeRunnerModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QString planName READ planName NOTIFY changed)
    Q_PROPERTY(QString dayTitle READ dayTitle NOTIFY changed)
    Q_PROPERTY(int blockCount READ blockCount NOTIFY changed)
    Q_PROPERTY(int blockIndex READ blockIndex NOTIFY changed)
    Q_PROPERTY(QVariantList blocks READ blocks NOTIFY changed)
    Q_PROPERTY(QVariantMap block READ block NOTIFY changed)
    Q_PROPERTY(QStringList images READ images NOTIFY imagesChanged)
    Q_PROPERTY(bool rendering READ rendering NOTIFY imagesChanged)
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY timerChanged)
    Q_PROPERTY(bool running READ running NOTIFY timerChanged)
    Q_PROPERTY(int doneCount READ doneCount NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    //! How big the runner's text and the exercise's notation are (1 = normal); remembered between sessions
    Q_PROPERTY(double textScale READ textScale WRITE setTextScale NOTIFY scaleChanged)
    Q_PROPERTY(double scoreScale READ scoreScale WRITE setScoreScale NOTIFY scaleChanged)
    //! The width, in image pixels, that a full system takes (so a short exercise isn't blown up to the full width)
    Q_PROPERTY(int systemWidth READ systemWidth NOTIFY imagesChanged)
    //! The block's parts: index, label, minutes, seconds, hasNotation, current, finished
    Q_PROPERTY(QVariantList parts READ parts NOTIFY partChanged)
    Q_PROPERTY(int partIndex READ partIndex NOTIFY partChanged)
    Q_PROPERTY(int partSecondsLeft READ partSecondsLeft NOTIFY timerChanged)
    //! The play-along now playing: its passes' tempos, and which pass the clock is in
    Q_PROPERTY(QVariantList passTempos READ passTempos NOTIFY partChanged)
    Q_PROPERTY(int currentPassTempo READ currentPassTempo NOTIFY timerChanged)
    Q_PROPERTY(bool playingAlong READ playingAlong NOTIFY partChanged)
    //! Hands free: "" (off), "practice", "break", "done"
    Q_PROPERTY(QString handsFree READ handsFree NOTIFY handsFreeChanged)
    Q_PROPERTY(int breakSecondsLeft READ breakSecondsLeft NOTIFY timerChanged)
    Q_PROPERTY(QString nextUp READ nextUp NOTIFY handsFreeChanged)
    //! Blocks played hands free and not rated yet: index, song, title, stage
    Q_PROPERTY(QVariantList toRate READ toRate NOTIFY handsFreeChanged)
    Q_PROPERTY(int dayMinutesLeft READ dayMinutesLeft NOTIFY timerChanged)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };

public:
    explicit PracticeRunnerModel(QObject* parent = nullptr);
    ~PracticeRunnerModel() override;

    QString planName() const;
    QString dayTitle() const;
    int blockCount() const;
    int blockIndex() const;
    QVariantList blocks() const;
    QVariantMap block() const;
    QStringList images() const;
    bool rendering() const;
    int secondsLeft() const;
    bool running() const;
    int doneCount() const;
    QString message() const;
    double textScale() const;
    void setTextScale(double s);
    double scoreScale() const;
    void setScoreScale(double s);
    int systemWidth() const;
    QVariantList parts() const;
    int partIndex() const;
    int partSecondsLeft() const;
    QVariantList passTempos() const;
    int currentPassTempo() const;
    bool playingAlong() const;
    QString handsFree() const;
    int breakSecondsLeft() const;
    QString nextUp() const;
    QVariantList toRate() const;
    int dayMinutesLeft() const;

    Q_INVOKABLE void load(const QString& planId, int dayIndex, int blockIndex);
    Q_INVOKABLE void go(int blockIndex);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();

    Q_INVOKABLE void startTimer();
    Q_INVOKABLE void pauseTimer();
    Q_INVOKABLE void resetTimer();

    //! Opens the exercise in StarScore (in place of the open score) at this tempo, and plays it when asked
    Q_INVOKABLE void openExercise(int tempo, bool play);
    Q_INVOKABLE void openSong();
    Q_INVOKABLE void togglePlay();
    Q_INVOKABLE void toggleMetronome();
    Q_INVOKABLE void openIreal();

    Q_INVOKABLE void goPart(int partIndex);
    //! A part's bars as written, every instrument, at today's tempo, once
    Q_INVOKABLE void playExample(int partIndex);
    //! A part's backing, looped for its minutes in passes from today's tempo up to the goal (and the timer started)
    Q_INVOKABLE void playAlong(int partIndex);
    Q_INVOKABLE void stopPlaying();

    //! Hands free: the day from this block on, without stopping
    Q_INVOKABLE void startHandsFree();
    Q_INVOKABLE void pauseHandsFree();
    Q_INVOKABLE void resumeHandsFree();
    Q_INVOKABLE void stopHandsFree();
    Q_INVOKABLE void skipBreak();
    Q_INVOKABLE void skipPart();
    //! A rating for a block played hands free (stage 1-4)
    Q_INVOKABLE void rate(int blockIndex, int stage);
    //! Keeps the blocks played hands free and not rated as done, with their stage as it was
    Q_INVOKABLE void keepUnrated();
    //! More practice: N minutes of new items at the end of the day (and on with them)
    Q_INVOKABLE void addMinutes(int minutes);

    //! Marks the block done: the tempo reached (0: none), how solid it is now (0-4), a note
    Q_INVOKABLE void markDone(int tempoReached, int stage, const QString& note);
    Q_INVOKABLE void markNotDone();

signals:
    void changed();
    void imagesChanged();
    void timerChanged();
    void timeUp();
    void partChanged();
    void handsFreeChanged();
    void scaleChanged();

private:
    const practice::Item* currentItem() const;
    const practice::Library* currentLibrary() const;
    void renderImages();
    void savePlan(const practice::Block& changed);
    void setupParts();
    void tick();
    void partFinished();
    void blockFinishedHandsFree();
    void startBlockHandsFree();
    void recordDone(int blockIndex, int minutes, int tempoReached, int stage, const QString& note);
    int nextOpenBlock(int after) const;

    struct RunPart {
        QString label;
        int seconds = 0;
        int from = 0;      // bars (0: all)
        int to = 0;
        bool notation = false;
        bool finished = false;
    };
    struct Pending {
        QString code, itemId;
        int minutes = 0;
        int tempo = 0;
        QString song, title;
        int stage = 0;     // before
    };

    practice::Plan m_plan;
    int m_dayIndex = 0;
    int m_blockIndex = 0;
    std::map<QString, practice::Library> m_libs;
    std::map<QString, QString> m_paths;   // code → .starscore
    QStringList m_images;
    bool m_rendering = false;
    QTimer m_timer;
    int m_secondsLeft = 0;
    QString m_message;
    int m_renderSerial = 0;
    double m_textScale = 1.0;
    double m_scoreScale = 1.0;
    int m_systemWidth = 0;
    std::vector<RunPart> m_parts;
    int m_partIndex = 0;
    int m_partSecondsLeft = 0;
    QList<int> m_passTempos;
    QList<int> m_passStarts;     // seconds into the part each pass starts
    int m_passTotal = 0;
    bool m_playingAlong = false;
    QString m_handsFree;
    QString m_pausedPhase;
    int m_breakSecondsLeft = 0;
    int m_minutesSinceBreak = 0;
    std::vector<Pending> m_toRate;
};
}
