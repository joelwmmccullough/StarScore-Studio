/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: what is in each song folder, and how finished it is
 * (the old build_data.py). Built from a scan of Sheets and Demos.
 */
#pragma once

#include <map>
#include <vector>

#include <QMap>
#include <QString>
#include <QStringList>

#include "orgscan.h"

namespace mu::project::starscore::org {
enum class Quality { Ok, Empty, Thin };

struct ArrangementInfo {
    QString folder;              // "3H Tpt Alt Ten", "3H Any Horns", "1H"
    int n = 0;                   // horn players
    QStringList instruments;     // empty for Any Horns and 1H
    bool generic = false;        // Any Horns
    bool solo = false;           // 1H: the whole tune on one horn, one sheet per instrument
    QStringList files;           // relative to the arrangement folder
    std::map<QString, Quality> quality;
    QStringList liveParts, emptyParts, thinParts;
    bool scoreOnly = false;
    bool usable = false;
};

struct EnsembleInfo {
    QString folder;              // "Big Band", "Full Orchestra", "Marching Band", "4S 2Vln Vla Vc"
    QStringList files;
    QString note;
};

struct SheetIssue {
    QString folder;
    QString file;
    Quality quality = Quality::Ok;
};

struct SongStatus {
    QString lead;                // "done", "partial", "none"
    QString rhythm;
    std::map<QString, QString> roles;   // drums, bass, keys, guitar -> done / empty / none
    QString three;
    std::map<int, QString> horns;       // 1..7
};

struct SongInfo {
    QString name;                // "Amplitudes"
    QString cat;                 // "orig", "cover", "vocal", "wip"
    QString root;                // "1 Amplitudes", "4 Works In Progress/Jeju"
    QString code;
    QStringList lead;
    std::map<QString, Quality> leadq;
    QStringList rhythm;          // "Bass", "Drums", "Bass (Name)"
    std::map<QString, Quality> rhythmq;
    std::vector<ArrangementInfo> arrangements;
    std::vector<EnsembleInfo> ensembles;
    QStringList extras, demos, references;
    std::map<QString, QStringList> other;    // folders the organizer doesn't know
    int archive = 0;             // files in Version History
    QString updateNotes;         // "Update Notes 26-08-19" ("" when missing); filled in by the organizer once the
                                 // folder has been re-dated for this run, not by buildLibrary
    QStringList hornGuides;      // files in Horn Part Guides
    bool hasRecordingsPdf = false;
    SongStatus status;
    std::vector<SheetIssue> issues;
    QString oldestLeadDate;      // yyyy-MM-dd: oldest lead sheet anywhere in the song (Version History included)
    QString oldestFileDate;      // yyyy-MM-dd: oldest file anywhere in the song

    int currentSheets() const;
    bool phase1() const { return status.lead == "done" && status.rhythm == "done" && status.three == "done"; }
};

struct Library {
    std::vector<SongInfo> songs;              // sorted by name
    std::map<QString, int> byCode;            // code -> index
    std::map<QString, int> byRoot;            // root -> index
    int totalFiles = 0;
    int archivedFiles = 0;
    int inboxFiles = 0;

    const SongInfo* song(const QString& code) const;
};

//! codes: song folder ("1 Amplitudes") -> code
Library buildLibrary(const ScanResult& scan, const std::map<QString, QString>& codes);

//! "5H 2Tpt Alt Ten Tbn" -> 5, { "Trumpet 1", "Trumpet 2", "Alto Sax", "Tenor Sax", "Trombone" }
bool expandHornFolder(const QString& folder, int& n, QStringList& instruments, bool& generic);
QString hornFolderName(const QStringList& instruments);

//! The horns in score order, with the abbreviation the folder names use: { "Trumpet", "Tpt" }, … , { "Bass Trombone", "Btb" }.
//! The one table for filing (orgfiling), folder names (here) and the Band Guide's key (orghtml_home).
const std::vector<std::pair<QString, QString> >& hornOrder();
//! "Alto Sax" -> "Alt" ("" for anything that isn't a horn); "Alt" -> "Alto Sax" (the abbreviation itself when unknown)
QString hornAbbr(const QString& instrument);
QString hornFromAbbr(const QString& abbr);
}
