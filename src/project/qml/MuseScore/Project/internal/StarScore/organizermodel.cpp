/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the organizer's window
 */
#include "organizermodel.h"

#include <QDir>
#include <QFileInfo>

#include <QLocale>

#include "project/internal/starscore/organizer/orghtml.h"
#include "project/internal/starscore/organizer/orgplatform.h"
#include "project/internal/starscore/organizer/orgrecordings.h"
#include "translation.h"

using namespace mu::project;
namespace org = mu::project::starscore::org;

OrganizerModel::OrganizerModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this)), m_alive(std::make_shared<bool>(true))
{
}

OrganizerModel::~OrganizerModel()
{
    *m_alive = false;
    if (m_organizer) {
        m_organizer->stop();
    }
    // the window closed with a question open: the run finishes as if it was skipped
    if (auto k = std::move(m_showsAnswer)) {
        std::vector<org::ShowAnswer> later;
        for (const org::NewShow& s : m_offered) {
            later.push_back({ s.show.url, org::ShowAnswer::Later, QString() });
        }
        k(later);
    }
    if (auto k = std::move(m_songsAnswer)) {
        k();
    }
}

QString OrganizerModel::bandFolder() const
{
    return starScore()->bandFolder();
}

QString OrganizerModel::projectsFolder() const
{
    return starScore()->projectsFolder();
}

QString OrganizerModel::modeTitle() const
{
    if (m_mode == "rebuild") {
        return muse::qtrc("starscore", "Rebuild every generated PDF");
    }
    if (m_mode == "export") {
        return muse::qtrc("starscore", "Organizing after the export");
    }
    return muse::qtrc("starscore", "Folder organization");
}

void OrganizerModel::chooseBandFolder()
{
    const muse::io::path_t dir = interactive()->selectDirectory(muse::trc("starscore", "Choose the Sheets and Demos folder"),
                                                                muse::io::path_t(bandFolder().isEmpty() ? QDir::homePath() : bandFolder()));
    if (!dir.empty()) {
        starScore()->setBandFolder(dir.toQString());
        emit changed();
    }
}

void OrganizerModel::chooseProjectsFolder()
{
    const muse::io::path_t dir = interactive()->selectDirectory(muse::trc("starscore", "Choose the Projects and Sheets folder"),
                                                                muse::io::path_t(projectsFolder().isEmpty() ? QDir::homePath()
                                                                                 : projectsFolder()));
    if (!dir.empty()) {
        starScore()->setProjectsFolder(dir.toQString());
        emit changed();
    }
}

void OrganizerModel::say(const QString& line)
{
    m_log << line;
    emit progressChanged();
}

void OrganizerModel::start(const QString& mode)
{
    if (m_running) {
        return;
    }
    m_mode = mode;
    m_finished = false;
    m_log.clear();
    m_headline.clear();
    m_fraction = 0;

    const QString band = bandFolder();
    if (band.isEmpty() || !QFileInfo(band).isDir()) {
        m_headline = muse::qtrc("starscore", "Choose the Sheets and Demos folder first.");
        m_finished = true;
        emit changed();
        return;
    }
    org::Paths paths = org::Paths::make(band, projectsFolder());

    org::RunRequest request;
    request.scope = mode == "rebuild" ? org::RunRequest::RebuildAll
                    : mode == "export" ? org::RunRequest::AfterExport : org::RunRequest::Organize;
    if (mode == "export") {
        request.exported = starScore()->takeLastExport();
        if (request.exported && !request.exported->summary.isEmpty()) {
            m_log << request.exported->summary;
        }
    }

    org::Prompts prompts;
    std::weak_ptr<bool> alive = m_alive;
    prompts.askShows = [this, alive](const std::vector<org::NewShow>& shows, std::function<void(std::vector<org::ShowAnswer>)> answer) {
        if (alive.expired() || !*alive.lock()) {
            answer({});
            return;
        }
        m_offered = shows;
        m_showsAnswer = answer;
        m_shows.clear();
        for (const org::NewShow& s : shows) {
            m_shows << QVariantMap {
                { "url", s.show.url }, { "date", QLocale(QLocale::English).toString(s.show.date, "ddd d MMM yyyy") },
                { "venue", s.show.venue }, { "city", s.show.city }, { "setlist", s.setlist.join(" · ") },
                { "askedBefore", s.askedBefore },
            };
        }
        say(muse::qtrc("starscore", "setlist.fm has %n show(s) StarScore hasn't seen. Paste each one's YouTube link, or skip it.", "",
                       int(shows.size())));
        emit promptChanged();
    };
    prompts.confirmSongs = [this, alive](std::vector<org::ShowVideo>& videos, const QStringList& choices, std::function<void()> next) {
        if (alive.expired() || !*alive.lock()) {
            next();
            return;
        }
        m_videos = &videos;
        m_songsAnswer = next;
        m_songs.clear();
        for (const org::ShowVideo& v : videos) {
            for (const org::MatchedTimestamp& m : v.songs) {
                m_songs << QVariantMap { { "show", QLocale(QLocale::English).toString(v.show.show.date, "d MMM") + " · " + v.show.show.venue },
                                         { "time", org::hhmmss(m.seconds) }, { "name", m.name }, { "code", m.code },
                                         { "unsure", m.code.isEmpty() } };
            }
            if (!v.ok) {
                say(muse::qtrc("starscore", "Couldn't read the video for %1; StarScore will ask about that show again next time.")
                    .arg(v.show.show.venue));
            } else if (v.songs.empty()) {
                say(muse::qtrc("starscore", "The video for %1 has no timestamps in its description.").arg(v.show.show.venue));
            }
        }
        m_songChoices.clear();
        m_songChoices << QVariantMap { { "text", muse::qtrc("starscore", "— not sure, leave it —") }, { "value", "" } };
        m_songChoices << QVariantMap { { "text", muse::qtrc("starscore", "Not a song (solo, intro…)") }, { "value", "-" } };
        for (const QString& c : choices) {
            m_songChoices << QVariantMap { { "text", c }, { "value", c.left(4) } };
        }
        if (m_songs.isEmpty()) {
            m_videos = nullptr;
            m_songsAnswer = nullptr;
            next();
            return;
        }
        emit promptChanged();
    };

    org::Fetcher fetch = [](const QString& url, bool browser, std::function<void(const QString&, int)> done) {
        org::fetchPage(url, browser, done);
    };

    m_organizer = org::Organizer::create(paths, fetch, prompts);
    org::Progress progress;
    progress.log = [this, alive](const QString& line) {
        if (!alive.expired() && *alive.lock()) {
            say(line);
        }
    };
    progress.step = [this, alive](const QString& s, double f) {
        if (!alive.expired() && *alive.lock()) {
            m_step = s;
            m_fraction = f;
            emit progressChanged();
        }
    };
    m_running = true;
    emit changed();
    m_organizer->run(request, progress, [this, alive](org::RunSummary summary) {
        if (!alive.expired() && *alive.lock()) {
            done(summary);
        }
    });
}

void OrganizerModel::done(const org::RunSummary& summary)
{
    m_running = false;
    m_finished = true;
    m_headline = summary.headline;
    for (const QString& l : summary.lines) {
        m_log << "• " + l;
    }
    for (const QString& w : summary.warnings) {
        m_log << "⚠ " + w;
    }
    // the open song keeps its own copy of its recordings
    const QString code = starScore()->songCode();
    if (!code.isEmpty() && summary.changedCodes.contains(code)) {
        const QJsonObject rec = org::readJsonObject(bandFolder() + "/6 Inbox/.organizer/recordings.json");
        if (!rec.isEmpty()) {
            starScore()->setSongRecordings(org::songRecordings(rec, code));
        }
    }
    m_organizer.reset();
    emit changed();
    emit progressChanged();
}

void OrganizerModel::stop()
{
    if (m_organizer) {
        m_organizer->stop();
        say(muse::qtrc("starscore", "Stopping after this step…"));
    }
    // a question that is still open is answered with "later"
    if (m_showsAnswer) {
        answerShows({});
    }
    if (m_songsAnswer) {
        answerSongs({});
    }
}

void OrganizerModel::answerShows(const QVariantList& answers)
{
    std::vector<org::ShowAnswer> out;
    for (const org::NewShow& s : m_offered) {
        org::ShowAnswer a;
        a.url = s.show.url;
        for (const QVariant& v : answers) {
            const QVariantMap m = v.toMap();
            if (m.value("url").toString() != s.show.url) {
                continue;
            }
            const QString kind = m.value("kind").toString();
            const QString link = m.value("link").toString().trimmed();
            if (kind == "notfilmed") {
                a.kind = org::ShowAnswer::NotFilmed;
            } else if (!link.isEmpty() && !org::youTubeId(link).isEmpty()) {
                a.kind = org::ShowAnswer::Link;
                a.link = link;
            }
        }
        out.push_back(a);
    }
    m_shows.clear();
    emit promptChanged();
    auto k = std::move(m_showsAnswer);
    m_showsAnswer = nullptr;
    if (k) {
        k(out);
    }
}

void OrganizerModel::answerSongs(const QStringList& codes)
{
    if (m_videos) {
        int i = 0;
        for (org::ShowVideo& v : *m_videos) {
            for (org::MatchedTimestamp& m : v.songs) {
                if (i < codes.size()) {
                    m.code = codes[i];
                }
                ++i;
            }
        }
    }
    m_videos = nullptr;
    m_songs.clear();
    emit promptChanged();
    auto k = std::move(m_songsAnswer);
    m_songsAnswer = nullptr;
    if (k) {
        k();
    }
}
