/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Autosave archive: copies of a song's earlier saves, kept on a thinning schedule (see starscoreautoarchive.cpp).
 */
#pragma once

#include <QDateTime>
#include <QString>

namespace mu::project::starscore {
//! ~/StarScore Studio/Autosave Archive
QString autosaveArchiveRoot();
bool autosaveArchiveEnabled();

//! Called just before a save replaces songPath: copies the version on disk into the archive when the newest copy is
//! 10 minutes old or more and differs from it, then thins the song's archive. Returns the new copy's path, or nothing.
QString archiveBeforeSave(const QString& songPath, const QDateTime& now = QDateTime::currentDateTime());

//! Removes the copies the schedule doesn't keep; returns how many
int thinAutosaveArchive(const QString& dir, const QString& base, const QDateTime& now);
}
