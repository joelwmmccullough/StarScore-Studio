/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: filing.
 *   Sheets and Demos (the old organize.py): empty 6 Inbox into the right song, file loose sheets in each song
 *   folder, register song folders that have a number but no code, make sure every song has 1 Lead Sheet, 1 Rhythm
 *   and Horn Part Guides (Demos, Extras and Version History appear when something is filed into them), and re-date
 *   Update Notes when a song's sheets changed.
 *   Projects and Sheets (the old maintain.py): empty 6 Inbox and the top level into the right tune (older
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
    QStringList intoSheetMusic;           // "1 Amplitudes: 1 Lead Sheet, 1 Rhythm, 3H Tpt Alt Ten" (moved into Sheet Music)
};

//! Joel, 14 Oct 2026: a song's sheet folders (every folder but Chord Charts, Demos, Reference PDFs, Horn Part Guides,
//! Update Notes and Version History; see staysAtSongTop) go inside "<song>/Sheet Music/". This moves a song folder
//! laid out the older way: each such folder is moved in whole; one that is already there too (an older StarScore
//! exported to the old place afterwards) is merged file by file, and of two files with one name the older goes to
//! Version History. The song's sheet record (6 Inbox/.organizer/sheets/CODE.json) then names the new places. Nothing
//! is deleted. Run by the folder organization and at the start of every export of the song.
struct SheetMusicMove {
    QStringList moved;                    // folder names moved in whole or merged
    std::vector<Move> superseded;         // files that went to Version History (relative to Sheets and Demos)
    QStringList failed;                   // what couldn't be moved (left where it was)
};
SheetMusicMove moveSongIntoSheetMusic(const QString& band, const QString& songRoot, const QString& code, const QString& todayIso);
//! A sheet record's paths (its sheets, the colours' lists, the folders) inside Sheet Music
QJsonObject sheetRecordInSheetMusic(const QJsonObject& record);

//! aliases: recordings.json "aliases" (song name -> code), extended when a song is registered
BandFilingReport fileBand(const Paths& paths, Codes& codes, const Roster& roster, QJsonObject& aliases, const Progress& progress);
//! songRoots: songs whose sheets changed this run
void redateUpdateNotes(const Paths& paths, const Codes& codes, const QStringList& songRoots, BandFilingReport& report);

//! Projects and Sheets' Starsign song folders (Joel, 19 Oct 2026): 1 Starsign, then Originals, Covers and Vocal Covers,
//! each split into Repertoire (ready to play) and In Progress, and the originals also into Sketches (first ideas)
const QStringList& starsignSongGroups();
//! The song folder for a kind ("original", "cover", "vocal") and stage ("repertoire", "progress", "sketch"); a cover
//! has no Sketches, so its sketch is In Progress
QString starsignGroupFor(const QString& kind, const QString& stage);
//! A Projects and Sheets path's kind and stage ("" when it isn't in a Starsign song folder)
QString starsignKindOf(const QString& relOrPath);
QString starsignStageOf(const QString& relOrPath);

struct LayoutReport {
    std::vector<Move> moved;
    QStringList problems;
};
//! The one move into the 1 Starsign folders, while "1 Starsign Originals", "2 Starsign Covers" or "3 Starsign WIP" is
//! still there: originals to Repertoire (but for the ones in progress), WIP songs to Sketches (but for the ones in
//! progress), covers to Repertoire when Sheets and Demos has a folder for them and In Progress when not, the vocal
//! covers to Vocal Covers; "2 Other Projects/Mxter Shirts" merged into the Starsign Mxter Shirts. Only moves; an old
//! group folder goes once it's empty.
LayoutReport migrateProjectsLayout(const Paths& paths);
//! Projects and Sheets' top folders numbered from 1 (Joel, 19 Oct 2026): STARSIGN -> 1 Starsign, 4 Other Projects -> 2,
//! 6 Templates -> 3, 7 Sketches -> 4, 8 Quarantine… -> 5, 9 Inbox -> 6, 10 Sweater Weather (a shortcut) -> 7. Each
//! renamed once, while the old name is there and the new one isn't. Runs before migrateProjectsLayout.
LayoutReport renumberProjectsTopFolders(const Paths& paths);

//! A tune folder in Projects and Sheets, listed once per run (listProjectTunes) and shared by everything below
struct ProjectTune {
    QString name;            // "Amplitudes"
    QString group;           // "1 Starsign/1 Originals/1 Repertoire" ("1 Starsign Originals" before the move)
    QString rel;             // "1 Starsign/1 Originals/1 Repertoire/Amplitudes"
    QString fileCode;        // the code its "CODE - Title.starscore" carries ("" without one)
    bool hasStarScore = false;
    //! In 1 Starsign (or the old groups 1-3). Filing, codes and the snapshot take "2 Other Projects" too; the folder
    //! colours take only these, because "2 Other Projects" can hold a tune of the same name as a band song.
    bool starsign = false;

    //! codes_proj.json's code for it, else fileCode (codes_proj.json may have been brought up to date this run)
    QString codeIn(const Codes& codes) const;
};
//! Every tune folder of the 1 Starsign song folders and "2 Other Projects" (and the old groups 1-3 while they're there),
//! by group then name. Shortcuts (symlinks) and hidden names are left out.
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
