/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the organizer's window: progress and log of a run, the question about new shows on
 * setlist.fm, and the check of the songs found in a show's video.
 */
#pragma once

#include <functional>
#include <memory>

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "iinteractive.h"
#include "project/istarscoreservice.h"
#include "project/internal/starscore/organizer/organizer.h"

namespace mu::project {
class OrganizerModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool finished READ finished NOTIFY changed)
    Q_PROPERTY(QString step READ step NOTIFY progressChanged)
    Q_PROPERTY(double fraction READ fraction NOTIFY progressChanged)
    Q_PROPERTY(QString logText READ logText NOTIFY progressChanged)
    Q_PROPERTY(QString headline READ headline NOTIFY changed)
    Q_PROPERTY(QString bandFolder READ bandFolder NOTIFY changed)
    Q_PROPERTY(QString projectsFolder READ projectsFolder NOTIFY changed)
    Q_PROPERTY(QString modeTitle READ modeTitle NOTIFY changed)
    // setlist.fm: shows to ask about (date, venue, city, url, setlist, askedBefore)
    Q_PROPERTY(QVariantList shows READ shows NOTIFY promptChanged)
    // songs found in the videos (show, time, name, code, unsure) and the songs to choose from
    Q_PROPERTY(QVariantList songs READ songs NOTIFY promptChanged)
    Q_PROPERTY(QVariantList songChoices READ songChoices NOTIFY promptChanged)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit OrganizerModel(QObject* parent = nullptr);
    ~OrganizerModel() override;

    bool running() const { return m_running; }
    bool finished() const { return m_finished; }
    QString step() const { return m_step; }
    double fraction() const { return m_fraction; }
    QString logText() const { return m_log.join("\n"); }
    QString headline() const { return m_headline; }
    QString bandFolder() const;
    QString projectsFolder() const;
    QString modeTitle() const;
    QVariantList shows() const { return m_shows; }
    QVariantList songs() const { return m_songs; }
    QVariantList songChoices() const { return m_songChoices; }

    //! mode: "export" (after Export to Sheets and Demos), "organize", "rebuild"
    Q_INVOKABLE void start(const QString& mode);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void chooseBandFolder();
    Q_INVOKABLE void chooseProjectsFolder();
    //! answers: one {url, kind: "link" | "later" | "notfilmed", link} per show
    Q_INVOKABLE void answerShows(const QVariantList& answers);
    //! codes: one per row of songs ("" = leave it unmatched, "-" = not a song)
    Q_INVOKABLE void answerSongs(const QStringList& codes);

signals:
    void changed();
    void progressChanged();
    void promptChanged();

private:
    void say(const QString& line);
    void done(const starscore::org::RunSummary& summary);

    QString m_mode;
    bool m_running = false;
    bool m_finished = false;
    QString m_step;
    double m_fraction = 0;
    QStringList m_log;
    QString m_headline;
    std::shared_ptr<starscore::org::Organizer> m_organizer;
    std::shared_ptr<bool> m_alive;

    QVariantList m_shows;
    QVariantList m_songs;
    QVariantList m_songChoices;
    std::vector<starscore::org::NewShow> m_offered;
    std::vector<starscore::org::ShowVideo>* m_videos = nullptr;
    std::function<void(std::vector<starscore::org::ShowAnswer>)> m_showsAnswer;
    std::function<void()> m_songsAnswer;
};
}
