/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Autosave archive: earlier saves of each song, packed into one compressed history file per song and thinned on a
 * schedule (see starscoreautoarchive.cpp). File › Autosave archive… opens any of them.
 */
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <vector>

namespace mu::project::starscore {
//! ~/StarScore Studio/Autosave Archive
QString autosaveArchiveRoot();
bool autosaveArchiveEnabled();

//! Called just before a save replaces songPath: when the newest archived version is 10 minutes old or more and
//! differs from the file, copies the file into the archive's Incoming folder (instant) and packs it in the
//! background. Returns the copy's path, or nothing.
//! (background false: not packed now; for tests)
QString archiveBeforeSave(const QString& songPath, const QDateTime& now = QDateTime::currentDateTime(), bool background = true);

//! Packs every copy waiting in Incoming into its song's history file and thins the history. Runs on the calling
//! thread (one run at a time); returns how many copies it packed.
int packIncoming(const QDateTime& now = QDateTime::currentDateTime());
void packIncomingInBackground();

struct ArchivedVersion {
    QDateTime time;
    qint64 fileBytes = 0;     // the song file's size then
    qint64 storedBytes = 0;   // what it takes in the history file (0 while waiting in Incoming)
    bool waiting = false;     // still a plain copy in Incoming
};

//! Songs with archived versions (file names without .starscore), newest change first
QStringList archivedSongs();
//! A song's archived versions, newest first
std::vector<ArchivedVersion> archivedVersions(const QString& song);
//! Total bytes the song's history (and waiting copies) take
qint64 archivedBytes(const QString& song);

//! Writes the version as a normal .starscore file to target (or, when target is empty, to
//! Autosave Archive/Opened/<song> (autosave yyyy-MM-dd HHmm).starscore). Returns the file's path, or nothing.
QString extractVersion(const QString& song, const QDateTime& time, const QString& target = QString(), QString* error = nullptr);
//! Every version of the song as .starscore files in folder; returns how many
int extractAllVersions(const QString& song, const QString& folder);
}
