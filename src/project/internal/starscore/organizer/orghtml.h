/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: the generated PDFs, written as HTML and printed by WebKit
 * (orgplatform_mac.mm). The look is the old toolkit's, made by Chromium until August 2026.
 */
#pragma once

#include <map>
#include <vector>

#include <QDate>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "orglibrary.h"
#include "orgstores.h"

namespace mu::project::starscore::org {
QString esc(const QString& s);
//! "15 Aug 2026"
QString prettyDate(const QString& iso);
//! "1:06:25" / "3:12"
QString hhmmss(int seconds);
QString plural(int n, const QString& one, const QString& many = QString());
//! "1,234"
QString thousands(int n);

//! The shared style sheet (gen_pdfs.CSS)
extern const char* BASE_CSS;
//! A whole page. extraCss is added after the shared style. margins (mm: top right bottom left) only go into the
//! page's @page rule, which matters when the HTML is opened in a browser (the test mode's preview copies): the
//! printed PDF's margins come from the RenderJob (organizer.cpp sets them per PDF), because WebKit's print path
//! ignores @page.
QString htmlPage(const QString& title, const QString& body, const QString& extraCss = QString(),
                 const QString& margins = "14mm 15mm 13mm 15mm");

// --- one song
QString whatsHereHtml(const SongInfo& song, const Roster& roster, const QDate& today);

// --- recordings (recordings.json)
struct RecordingsData {
    QJsonObject root;                         // the whole file
    std::map<QString, QJsonObject> shows;     // id -> show
    QJsonObject venueShort;                   // long venue names shortened for the tables ("venueShort")

    void load(const QJsonObject& o);
    QString venue(const QString& name) const { return venueShort.value(name).toString(name); }
    QJsonArray performancesOf(const QString& code) const;
    QJsonArray releasesOf(const QString& code) const;
    int filmedStarsignShows() const;          // filmed with a timestamped setlist
};
//! A song's Recordings page (one page in the band folder)
QString recordingsBlock(const RecordingsData& rec, const QString& name, const QString& code, const QString& anchor,
                        bool swOnly, const QString& swName);
QString recordingsFooter(const RecordingsData& rec, const QDate& today, bool sw);
QString recordingsHtml(const RecordingsData& rec, const SongInfo& song, const QDate& today);
//! Joel's combined copy, at the top of Projects and Sheets
QString allRecordingsHtml(const RecordingsData& rec, const Library& lib, const QDate& today);

// --- changelogs (changelog.json)
QString changelogHtml(const SongInfo& song, const Player& player, const QJsonArray& entries);

// --- horn part guides (hornguides/CODE.json)
QString hornGuideHtml(const SongInfo& song, const Player& player, const QJsonObject& analysis, const Roster& roster);

// --- the three top-level band PDFs
QString bandGuideHtml(const Library& lib, const QDate& today);
struct PlayCounts {
    std::map<QString, int> thisYear, lastYear;   // song code -> plays (setlist.fm stats)
    int year = 0;
    QStringList notInLibrary;                    // setlist.fm songs with no folder
    int plays(const QString& code) const;        // this year + last year
};
//! One outstanding job on the checklist (the Progress tracker's list, and the Maintenance Report's counts and top item)
struct Task {
    QString tier, song, code, text, shortText;
    int sc = 0, thisYear = 0, lastYear = 0, sub = 1, n = 0;
    bool wip = false;
};
//! Every outstanding task, highest priority first. Built once per run and handed to both pages.
std::vector<Task> buildTasks(const Library& lib, const PlayCounts& plays);
QString progressHtml(const Library& lib, const PlayCounts& plays, const std::vector<Task>& tasks, const QDate& today);
//! log: maintlog.json, newest first (the page shows the last twelve months of it)
QString maintenanceHtml(const Library& lib, const std::vector<Task>& tasks, const QJsonArray& log, const QDate& today, int hornGuides,
                        int changelogs);
//! A log entry (maintlog.json / projstate.json "log"): {date, title, actions: [{head, kind: change|warn|ok|"", text (HTML)}]}
QString logEntryHtml(const QJsonObject& entry, bool newest);

// --- Projects and Sheets
//! projstate: counts (files, starscore, mscz, templates, quarantined), tunes [{name, group, code, starscore, museScoreFiles,
//! deprecated, versionHistory}], log
QString projectsReportHtml(const QJsonObject& projstate, const QDate& today);
}
