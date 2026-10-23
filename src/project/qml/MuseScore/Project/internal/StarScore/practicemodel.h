/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Practice (Joel, 22 Oct 2026): the setlist, what each song needs, practice routines and
 * multi-day plans up to a show, the practice book PDF, iReal Pro charts, and the runner (see starscorepractice.h).
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "iinteractive.h"
#include "project/istarscoreservice.h"
#include "project/internal/starscore/starscorepractice.h"

class QFileSystemWatcher;

namespace mu::project {
class PracticeModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList songs READ songs NOTIFY changed)
    Q_PROPERTY(QString setlistText READ setlistText NOTIFY changed)
    Q_PROPERTY(QVariantList setlist READ setlist NOTIFY changed)
    Q_PROPERTY(QString showDate READ showDate NOTIFY changed)
    Q_PROPERTY(QString startDate READ startDate NOTIFY changed)
    Q_PROPERTY(int defaultMinutes READ defaultMinutes NOTIFY changed)
    Q_PROPERTY(int routineMinutes READ routineMinutes NOTIFY changed)
    Q_PROPERTY(QVariantList days READ days NOTIFY changed)
    Q_PROPERTY(int totalMinutes READ totalMinutes NOTIFY changed)
    Q_PROPERTY(QVariantList plans READ plans NOTIFY plansChanged)
    Q_PROPERTY(QString planId READ planId NOTIFY plansChanged)
    Q_PROPERTY(QVariantMap plan READ plan NOTIFY plansChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString libraryFolder READ libraryFolder NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit PracticeModel(QObject* parent = nullptr);

    QVariantList songs() const;
    QString setlistText() const;
    QVariantList setlist() const;
    QString showDate() const;
    QString startDate() const;
    int defaultMinutes() const;
    int routineMinutes() const;
    QVariantList days() const;
    int totalMinutes() const;
    QVariantList plans() const;
    QString planId() const;
    QVariantMap plan() const;
    bool busy() const;
    QString status() const;
    QString libraryFolder() const;

    Q_INVOKABLE void load();
    //! Reads the song libraries again (after new practice items were added to songs)
    Q_INVOKABLE void rescan();
    Q_INVOKABLE void setSetlistText(const QString& text);
    Q_INVOKABLE void setNeed(const QString& code, const QString& area, int value);
    Q_INVOKABLE void resetNeeds(const QString& code);
    Q_INVOKABLE void setShowDate(const QString& isoDate);
    Q_INVOKABLE void setStartDate(const QString& isoDate);
    Q_INVOKABLE void setDefaultMinutes(int minutes);
    Q_INVOKABLE void setRoutineMinutes(int minutes);
    Q_INVOKABLE void setDayMinutes(const QString& isoDate, int minutes);

    Q_INVOKABLE void makeRoutine();
    Q_INVOKABLE void makePlan();
    Q_INVOKABLE void selectPlan(const QString& id);
    Q_INVOKABLE void deletePlan(const QString& id);
    //! Plans the days from today on again, from the progress so far
    Q_INVOKABLE void rebuildPlan();
    //! The practice book: the whole plan (dayIndex -1) or one day
    Q_INVOKABLE void makeBook(int dayIndex);
    Q_INVOKABLE void exportIreal(int dayIndex);
    Q_INVOKABLE void openRunner(int dayIndex, int blockIndex);
    Q_INVOKABLE void openOutputFolder();

signals:
    void changed();
    void plansChanged();

private:
    struct SetSong {
        QString line;     // as typed
        QString code;     // empty when no song with a library matches
        QString title;
        QString path;
        int set = 0;
    };

    void parseSetlist();
    void saveSettings() const;
    void loadSettings();
    void reloadPlans();
    std::map<QString, practice::Library> librariesFor(const std::vector<practice::PlanSong>& songs) const;
    std::vector<practice::PlanSong> planSongs() const;
    std::map<QString, int> needsFor(const QString& code) const;
    QString outputFolder() const;
    void setStatus(const QString& s);
    void watchPlans();

    std::vector<practice::SongEntry> m_songs;
    QString m_root;
    QString m_setlistText;
    std::vector<SetSong> m_setlist;
    std::map<QString, std::map<QString, int> > m_needs;   // the user's changes to a song's needs
    QDate m_showDate;
    QDate m_startDate;
    int m_defaultMinutes = 120;
    int m_routineMinutes = 120;
    std::map<QDate, int> m_dayMinutes;
    std::vector<practice::Plan> m_plans;
    QString m_planId;
    bool m_busy = false;
    QString m_status;
    QString m_lastOutput;
    QFileSystemWatcher* m_watcher = nullptr;
};
}
