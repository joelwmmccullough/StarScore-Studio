/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: keeping recordings.json up to date.
 *
 *  - setlist.fm: every run looks at the band's setlist list for shows it hasn't seen. A show is only offered once
 *    its date has passed (a show listed for 9 Oct is offered from 10 Oct). Joel pastes the YouTube link of the
 *    filmed show, or skips it for now, or marks it as not filmed. The first run only takes note of the shows
 *    already there.
 *  - YouTube: the pasted video's title, description and date; its description's timestamps become the
 *    performances, matched to songs with recordings.json "aliases" (Joel confirms any it can't place).
 *  - Play counts: setlist.fm's stats pages for this year and last year.
 */
#pragma once

#include <functional>
#include <vector>

#include <QDate>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

#include "orgonline.h"

namespace mu::project::starscore::org {
inline constexpr const char* SETLISTFM_ARTIST = "https://www.setlist.fm/setlists/starsign-5bfe57e0.html";
inline constexpr const char* SETLISTFM_STATS = "https://www.setlist.fm/stats/starsign-5bfe57e0.html";

//! A show setlist.fm has and recordings.json doesn't
struct NewShow {
    SetlistShow show;
    QStringList setlist;           // full song list from the show's own page
    bool askedBefore = false;      // skipped last time
};

//! What Joel said about one show
struct ShowAnswer {
    QString url;                   // setlist.fm url (identifies the show)
    enum Kind { Later, Link, NotFilmed } kind = Later;
    QString link;                  // YouTube link
};

//! A parsed video, its timestamps matched to songs
struct ShowVideo {
    NewShow show;
    YouTubeVideo video;
    bool ok = false;               // the video page could be read
    std::vector<MatchedTimestamp> songs;
};

//! Fetch a page (tests replace this). done(html, status) is called later on the main thread.
using Fetcher = std::function<void(const QString& url, bool browser, std::function<void(const QString&, int)> done)>;

struct OnlineResult {
    bool reached = false;                     // setlist.fm answered
    bool firstRun = false;                    // nothing seen before: every show is only noted (baselineShows)
    std::vector<SetlistShow> allShows;        // first run: every show on every list page; later: shows already recorded
    std::vector<NewShow> newShows;
    QMap<QString, int> thisYear, lastYear;    // setlist.fm song name -> plays
    int year = 0;
    QStringList log;
};

//! Look for new shows and fetch play counts. recordings: recordings.json (read only here).
void checkSetlistFm(const QJsonObject& recordings, const QDate& today, const Fetcher& fetch, std::function<void(OnlineResult)> done);
//! Read each answered show's video
void readVideos(const std::vector<std::pair<NewShow, QString> >& shows, const QJsonObject& recordings, const Fetcher& fetch,
                std::function<void(std::vector<ShowVideo>)> done);

//! Write the outcome into recordings.json: shows marked seen / skipped / not filmed, new shows and performances,
//! aliases Joel confirmed. Returns log lines (HTML) and the codes whose recordings changed.
QStringList applyShows(QJsonObject& recordings, const std::vector<NewShow>& offered, const std::vector<ShowAnswer>& answers,
                       const std::vector<ShowVideo>& videos, const QDate& today, QStringList& changedCodes);
//! First run: remember every show setlist.fm has, without asking
void baselineShows(QJsonObject& recordings, const std::vector<SetlistShow>& shows, const QDate& today);

//! setlist.fm names -> library codes for the play counts; names it can't place go to notInLibrary
void mapPlayCounts(const QJsonObject& recordings, const QMap<QString, int>& byName, std::map<QString, int>& byCode,
                   QStringList& notInLibrary);
//! Store fetched counts in recordings.json ("playCounts": {"2026": {...}, "2025": {...}, "fetched": date})
void storePlayCounts(QJsonObject& recordings, const OnlineResult& r, const QDate& today);

//! The recordings of one song (code): its performances and releases, with their shows, for the Recordings window
//! and the copy kept inside the .starscore
QJsonObject songRecordings(const QJsonObject& recordings, const QString& code);
//! Merge a song's copy (edited in the Recordings window, or kept in a .starscore) into recordings.json.
//! Entries are matched by id; the newer "modified" wins; ids in "deleted" are removed.
bool mergeSongRecordings(QJsonObject& recordings, const QString& code, const QJsonObject& songCopy);
QString newRecordingId(const QJsonObject& recordings, const QString& prefix);
}
