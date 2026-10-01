/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: reading setlist.fm and YouTube pages.
 * Pure text parsing (no network here), so it can be tested on its own.
 */
#pragma once

#include <vector>

#include <QDate>
#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QStringList>

namespace mu::project::starscore::org {
//! One show in a setlist.fm list page ("…/setlists/starsign-5bfe57e0.html?page=N")
struct SetlistShow {
    QDate date;
    QString url;          // absolute https://www.setlist.fm/setlist/… URL
    QString title;        // "Starsign at Nautilus Tavern, La Jolla, CA, USA"
    QString venue;        // "Nautilus Tavern"
    QString city;         // "La Jolla, CA, USA"
    QStringList songs;    // as shown in the preview (may be cut short with "…")
};

//! The songs of one setlist page, in order. Sections ("Set 1:", "Encore:") are left out.
struct SetlistDetail {
    QDate date;
    QString venue;
    QString city;
    QStringList songs;
};

//! A YouTube video, from ytInitialPlayerResponse on its watch page
struct YouTubeVideo {
    QString id;
    QString title;
    QString description;
    int lengthSeconds = 0;
    QDate published;
};

//! "1:06:25 Amplitudes" in a video description
struct Timestamp {
    int seconds = 0;
    QString name;
};

//! One song in a timestamped description, matched to the library
struct MatchedTimestamp {
    int seconds = 0;
    QString name;          // as written in the description
    QString code;          // library song code ("AMPL"), "" = not matched, "-" = not a song (skip)
    QString variant;       // "intro only", "encore", "ft. Nubella Honey", … or ""
};

QString htmlUnescape(const QString& s);

std::vector<SetlistShow> parseSetlistList(const QString& html);
//! How many list pages setlist.fm says there are (1 when there is no pager)
int parseSetlistPageCount(const QString& html);
SetlistDetail parseSetlistPage(const QString& html);
//! Song name -> times played, from a stats page ("…/stats/starsign-5bfe57e0.html?year=2026")
QMap<QString, int> parseSetlistStats(const QString& html);

//! The 11-character video id in a YouTube link (watch?v=, youtu.be/, /live/, /shorts/, /embed/), or ""
QString youTubeId(const QString& link);
bool parseYouTubeWatchPage(const QString& html, YouTubeVideo& out);
//! The JSON object assigned to `name` in a page's script (balanced braces, strings respected)
QJsonObject extractJsonAssignment(const QString& html, const QString& name);
std::vector<Timestamp> parseTimestamps(const QString& description);

//! Map a description's song name to a library code with the aliases (normalized, case-insensitive).
//! aliases: name -> code; skipNames: names that are not songs.
MatchedTimestamp matchTimestamp(const Timestamp& t, const QMap<QString, QString>& aliases, const QStringList& skipNames);
//! Lower case, accents and punctuation removed, "&" = "and", single spaces
QString normalizeName(const QString& s);
}
