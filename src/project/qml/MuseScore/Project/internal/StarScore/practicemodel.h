/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Practice (Joel, 22 Oct 2026): the setlist, what each song needs, practice routines and
 * multi-day plans up to a show, the practice book PDF, iReal Pro charts, and the runner (see starscorepractice.h).
 */
#pragma once

#include <QJsonObject>
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
    //! Saved setlists (each with its show date, its needs and minutes, and its plans)
    Q_PROPERTY(QVariantList setlists READ setlists NOTIFY changed)
    Q_PROPERTY(QString setlistId READ setlistId NOTIFY changed)
    Q_PROPERTY(QString setlistName READ setlistName NOTIFY changed)
    //! Breaks in a routine or plan: every N minutes of practice (0: none), for M minutes
    Q_PROPERTY(int breakEvery READ breakEvery NOTIFY changed)
    Q_PROPERTY(int breakMinutes READ breakMinutes NOTIFY changed)
    //! How good he is at each skill: id, name, group, rating (0-5), level text, items, last, trend (since 2 weeks ago)
    Q_PROPERTY(QVariantList skills READ skills NOTIFY skillsChanged)

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
    QVariantList setlists() const;
    QString setlistId() const;
    QString setlistName() const;
    int breakEvery() const;
    int breakMinutes() const;
    QVariantList skills() const;

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
    Q_INVOKABLE void setBreakEvery(int minutes);
    Q_INVOKABLE void setBreakMinutes(int minutes);

    Q_INVOKABLE void selectSetlist(const QString& id);
    //! A new setlist: a copy of the current one (setlist, needs, minutes) under a new name and date
    Q_INVOKABLE void newSetlist(const QString& name, const QString& isoDate);
    Q_INVOKABLE void renameSetlist(const QString& name);
    Q_INVOKABLE void deleteSetlist();

    //! Ratings of every skill, from the progress so far
    Q_INVOKABLE void refreshSkills();

    Q_INVOKABLE void makeRoutine();
    Q_INVOKABLE void makePlan();
    Q_INVOKABLE void selectPlan(const QString& id);
    Q_INVOKABLE void deletePlan(const QString& id);
    //! Plans the days from today on again, from the progress so far
    Q_INVOKABLE void rebuildPlan();
    //! More practice for a day already planned: N minutes of new items at its end, from what it already has
    Q_INVOKABLE void addMinutes(int dayIndex, int minutes);
    Q_INVOKABLE void addJam(int dayIndex, int minutes);
    //! The practice book: the whole plan (dayIndex -1) or one day
    Q_INVOKABLE void makeBook(int dayIndex);
    Q_INVOKABLE void exportIreal(int dayIndex);
    Q_INVOKABLE void openRunner(int dayIndex, int blockIndex, bool handsFree = false);
    Q_INVOKABLE void openOutputFolder();
    Q_INVOKABLE void chooseLibraryFolder();

signals:
    void changed();
    void plansChanged();
    void skillsChanged();

private:
    void addToDay(int dayIndex, int minutes, bool jam);
    struct SavedSetlist {
        QString id;
        QString name;
        QJsonObject data;   // setlist, showDate, defaultMinutes, routineMinutes, dayMinutes, needs, breakEvery, breakMinutes
        bool legacy = false;   // made from the one setlist of 1.19 (plans without a setlist belong to it)
    };
    QJsonObject currentData() const;
    void applyData(const QJsonObject& o);
    void saveSetlists() const;

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
    std::vector<SavedSetlist> m_setlists;
    QString m_setlistId;
    int m_breakEvery = 0;
    int m_breakMinutes = 5;
    QVariantList m_skills;
};
}
