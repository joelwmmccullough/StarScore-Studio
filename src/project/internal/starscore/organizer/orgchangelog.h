/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: changelog entries (changelog.json: song folder -> player -> entries, newest first).
 *
 * After an export StarScore knows, bar by bar, what changed in every sheet (it keeps a signature of each bar of
 * each exported sheet in the .starscore), so the entry can say "3-Horn Arrangement: your Alto Sax part changed in
 * bars 33–48 (letter C)". Sheets that changed some other way (dropped into the inbox, edited by hand) get a plainer
 * entry from the file comparison.
 */
#pragma once

#include <vector>

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "orglibrary.h"
#include "orgstores.h"

namespace mu::project::starscore::org {
struct SheetChange {
    QString relativePath;              // in the song folder: "3H Tpt Alt Ten/AMPL - Alto Sax.pdf"
    QString arrangement;               // printed name: "3-Horn Arrangement", "Lead Sheet", "Rhythm Section"
    QString part;                      // "Alto Sax", "Horn 2"
    enum Kind { Added, Changed, Same } kind = Same;
    std::vector<std::pair<int, int> > bars;   // changed bars (1-based, inclusive ranges)
    QStringList letters;               // rehearsal marks those bars fall under
    bool isScore = false;
    bool barsKnown = true;             // false: no signature from an earlier export to compare with
};

//! What a StarScore export did, handed to the organizer
struct ExportInfo {
    QString songRoot;                  // "1 Amplitudes"
    QString code;
    QString title;
    QString version;                   // "4.0.3"
    std::vector<SheetChange> sheets;
    QStringList written;               // relative to the song folder
    QStringList archived;              // relative to the song folder (now in Version History)
    QJsonObject hornAnalysis;          // hornguides/CODE.json (empty when the song has no 1-3 horn parts)
    QJsonObject recordings;            // the song's recordings kept in the .starscore (merged into recordings.json)
    QString summary;                   // what the export said ("Wrote 12 PDF(s)…", sheets it skipped)
};

//! "33–48", "12, 30–31"
QString barRanges(const std::vector<std::pair<int, int> >& bars);

//! Add today's entries for one song. fileChanges: paths relative to Sheets and Demos that the scan saw added,
//! changed or removed (Version History excluded). Returns how many player entries were added or extended.
int addChangelogEntries(QJsonObject& changelog, const SongInfo& song, const Roster& roster, const ExportInfo* exported,
                        const QStringList& added, const QStringList& changed, const QStringList& removed, const QDate& today);

//! A player who joined (no entries for a song) starts with the history of the former player on the same instrument
//! (a new drummer inherits the old drummer's history). Returns the players seeded.
QStringList seedNewPlayers(QJsonObject& changelog, const Roster& roster);
}
