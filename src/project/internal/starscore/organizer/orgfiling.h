/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: filing.
 *   Sheets and Demos (the old organize.py): empty 6 Inbox into the right song, file loose sheets in each song
 *   folder, register song folders that have a number but no code, make sure every song has 1 Lead Sheet, 1 Rhythm
 *   and Horn Part Guides (Demos, Extras and Version History appear when something is filed into them), and re-date
 *   Update Notes when a song's sheets changed.
 *   Projects and Sheets (the old maintain.py): empty 9 Inbox and the top level into the right tune (older
 *   MuseScore material into the tune's MuseScore Files/), move empty folders to "Z Empty Folders (safe to delete)".
 *   Codes: keep codes.json and codes_proj.json in step.
 * Nothing is ever deleted: a file that would be replaced moves to Version History (band) or Deprecated (Projects).
 */
#pragma once

#include <map>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "orgcore.h"
#include "orgstores.h"

namespace mu::project::starscore::org {
struct Move {
    QString from, to;        // relative to the folder the report is about
};

struct BandFilingReport {
    std::vector<Move> filed, inboxFiled, superseded;
    QStringList unrecognised, inboxUnmatched;
    QStringList newSongs;                 // top-level folders with no number prefix (left alone)
    QStringList registered;               // "1 Mxter Shirts -> MXTR"
    QStringList foldersMade;              // "1 Bet/Demos"
    QStringList updateNotesRedated;       // "1 Amplitudes: Update Notes 26-08-19 -> Update Notes 26-10-01"
};

//! aliases: recordings.json "aliases" (song name -> code), extended when a song is registered
BandFilingReport fileBand(const Paths& paths, Codes& codes, const Roster& roster, QJsonObject& aliases, const Progress& progress);
//! songRoots: songs whose sheets changed this run
void redateUpdateNotes(const Paths& paths, const Codes& codes, const QStringList& songRoots, BandFilingReport& report);

//! A tune folder in Projects and Sheets, listed once per run (listProjectTunes) and shared by everything below
struct ProjectTune {
    QString name;            // "Amplitudes"
    QString group;           // "1 Starsign Originals"
    QString rel;             // "1 Starsign Originals/Amplitudes"
    QString fileCode;        // the code its "CODE - Title.starscore" carries ("" without one)
    bool hasStarScore = false;
    //! Groups 1-3 (Originals, Covers, WIP). Filing, codes and the snapshot take every group 1-4; the folder colours
    //! take only these, because "4 Other Projects" can hold a tune of the same name as a band song.
    bool starsign = false;

    //! codes_proj.json's code for it, else fileCode (codes_proj.json may have been brought up to date this run)
    QString codeIn(const Codes& codes) const;
};
//! Every tune folder of groups 1-4, by group then name. Shortcuts (symlinks) and hidden names are left out.
std::vector<ProjectTune> listProjectTunes(const Paths& paths);

struct ProjectsFilingReport {
    std::vector<Move> filed, superseded, swept;
    QStringList unmatched;
    QStringList skippedRecent;
};
ProjectsFilingReport fileProjects(const Paths& paths, const std::vector<ProjectTune>& tunes, const Progress& progress);

//! The counts and tunes table for the Projects Maintenance Report (projstate.json)
QJsonObject projectsSnapshot(const Paths& paths, const Codes& codes, const std::vector<ProjectTune>& tunes);

//! Keep codes_proj.json in step with codes.json and the .starscore files ("CODE - Title.starscore").
//! The band folder's code wins when they disagree. Returns what changed, as log lines.
QStringList syncCodes(const Paths& paths, Codes& codes, const std::vector<ProjectTune>& tunes);

//! Move the old Python toolkits into Deprecated/Retired <date>/ (once). Returns what moved.
QStringList retireOldToolkits(const Paths& paths);

//! The instrument a part name stands for ("alto saxophone" -> "Alto Sax", "Tpt 2" -> "Trumpet 2"), or ""
QString canonInstrument(const QString& raw);
}
