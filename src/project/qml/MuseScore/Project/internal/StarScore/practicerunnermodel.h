/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the practice runner: one day of a practice plan, a block at a time. What to play and how, the
 * exercise's notation, a timer, the exercise opened in StarScore at today's tempo with its backing (and the
 * metronome), iReal Pro, and marking a block done (the tempo reached and how solid it is), which is kept as progress.
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

    //! Marks the block done: the tempo reached (0: none), how solid it is now (0-4), a note
    Q_INVOKABLE void markDone(int tempoReached, int stage, const QString& note);
    Q_INVOKABLE void markNotDone();

signals:
    void changed();
    void imagesChanged();
    void timerChanged();
    void timeUp();

private:
    const practice::Item* currentItem() const;
    const practice::Library* currentLibrary() const;
    void renderImages();
    void savePlan(const practice::Block& changed);

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
};
}
