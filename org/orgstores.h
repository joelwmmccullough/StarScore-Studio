/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: the data files it keeps.
 *
 * Band toolkit (Sheets and Demos/6 Inbox/.organizer):
 *   codes.json        song folder -> code (same format as always; StarScore's "Add song" writes it too)
 *   roster.json       the band: who plays what, horn chairs, current or former   (new)
 *   changelog.json    every player's changelog entries per song, newest first     (kept forever)
 *   maintlog.json     the Maintenance Report's session log, newest first          (kept forever)
 *   recordings.json   shows, live performances, releases, ratings, aliases       (new; replaces livedata.py)
 *   sheetcache.json   measurements of every current sheet                          (new; replaces density.tsv etc.)
 *   hornguides/CODE.json   each song's horn parts, analysed from the score at export (new)
 * Projects toolkit (Projects and Sheets/.organizer):
 *   codes_proj.json   tune title -> code
 *   projstate.json    counts, the tunes table and the Projects Maintenance Report's log
 */
#pragma once

#include <map>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include "orgcore.h"

namespace mu::project::starscore::org {
struct Chair {
    int chair = 0;            // 1 = top
    QString key;              // "Bb", "Eb", "C"
    QString instrument;       // "alto sax"
};

struct Player {
    QString name;
    QStringList instruments;  // part names they read: "Trumpet", "Trumpet 1", "Tenor Sax 2"… (first = main)
    QString blurb;            // "alto / 2nd tenor / soprano / clarinet / bass clarinet / flute"
    bool current = true;
    bool horn = false;        // gets a Horn Part Guide
    std::map<int, Chair> chairs;   // Any Horns: horn count (2, 3) -> chair

    QJsonObject toJson() const;
    static Player fromJson(const QJsonObject& o);
};

class Roster
{
public:
    void load(const Paths& paths);
    bool save(const Paths& paths) const;
    static Roster defaults();

    std::vector<Player> players;
    //! Old sheets named after a player ("Seagrass-Name.pdf"): first name -> instrument, for filing them
    std::map<QString, QString> fileNameHints;

    std::vector<const Player*> current() const;
    std::vector<const Player*> currentHorns() const;
    //! Who reads this part ("Alto Sax", "Tenor Sax 2", "Trumpet 1", "Bass (Name)"): current players only
    QStringList readersOf(const QString& part) const;
    //! The one player a named horn part belongs to (first current player who lists it)
    QString ownerOf(const QString& part) const;
};

class Codes
{
public:
    void load(const Paths& paths);
    bool saveBand(const Paths& paths) const;
    bool saveProjects(const Paths& paths) const;

    std::map<QString, QString> band;      // "1 Amplitudes" -> "AMPL"
    std::map<QString, QString> projects;  // "Amplitudes" -> "AMPL"
    bool bandChanged = false;
    bool projectsChanged = false;

    QString rootOf(const QString& code) const;
    QStringList allCodes() const;
};

//! A four-letter code for a new song in the style of the others (AMPL, FYKB, HTLS…) that isn't taken
QString suggestCode(const QString& title, const QStringList& taken);

//! A JSON file kept as it is, with the parts StarScore doesn't know about left alone
struct JsonStore {
    QString file;            // relative to the toolkit folder
    QJsonDocument doc;
    bool changed = false;

    bool load(const QString& folder);
    bool save(const QString& folder);
};
}
