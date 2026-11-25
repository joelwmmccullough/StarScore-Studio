/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * (1.37.0) Saving into a Google Drive folder safely.
 *
 * MuseScore saved a song by writing "<song>_saving" beside it, deleting the song file, copying the new file to the
 * song's name and deleting "_saving". In a Google Drive for desktop folder, deleting a file and creating a new one under
 * the same name a moment later can make Drive cancel the new file's upload and move it out of the folder, into
 * ~/Library/Application Support/Google/DriveFS/canceled_uploads (this is how cisu2.starscore disappeared right after
 * a save on 23 Nov 2026). Now:
 *  - the temporary file is written in a local folder, and the song file is overwritten in place (never deleted);
 *  - after each save a copy of the saved file is kept in ~/StarScore Studio/Autosave Archive/Last Saved, and the song
 *    file is checked 5 seconds, 1 minute and 5 minutes later: if it's gone, StarScore writes it back and says so;
 *  - opening a song (and starting StarScore) looks in Drive's canceled_uploads and lost_and_found folders for a newer
 *    copy and offers to put it back.
 */
#pragma once

#include <QDateTime>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <vector>

namespace mu::project::starscore {
//! Writes src's bytes into dst itself (dst keeps being the same file: it's never deleted and created again; it's
//! created when it doesn't exist), then reads dst back to check it. False (and error) when something failed.
bool replaceFileContents(const QString& src, const QString& dst, QString* error = nullptr);

//! (1.38.0) ~/Library/Application Support/…/Working: the open songs' working copies of their solos, reference PDFs and
//! recordings (not the system's temporary folder, which macOS empties of old files). Folders left there more than
//! 30 days are removed the first time this is called.
QString workingFoldersRoot();

//! Where a save writes its temporary file for target: a local temporary folder (not the song's cloud folder)
QString localSavingPath(const QString& target);

//! ~/StarScore Studio/Autosave Archive/Last Saved/<file name> (<folder name>).<suffix>
QString lastSavedCopyPath(const QString& songPath);

//! Called after the project was saved to songPath (not autosaves, not copies): keeps the Last Saved copy and checks,
//! 5 seconds, 1 minute and 5 minutes later, that the file is still there.
void afterSave(const QString& songPath);
//! (for tests) the check now
void checkSavedFile(const QString& songPath, const QDateTime& savedAt);

struct SaveWatchHooks {
    std::function<bool(const QString& path)> isOpenAt;                          // a song open now is saved there
    std::function<void(const QString& title, const QString& text)> tell;        // a message for the user
};
void setSaveWatchHooks(SaveWatchHooks hooks);

//! Google Drive for desktop's folders of files it took out of a synced folder (canceled_uploads, lost_and_found)
QStringList driveRemovedFolders();

struct DriveRemovedCopy {
    QString path;
    QDateTime time;
    qint64 bytes = 0;
};
//! The .starscore and .mscz files in them, newest first
std::vector<DriveRemovedCopy> driveRemovedCopies();
//! A copy of songPath's file (same file name) there that is newer than songPath, or songPath is missing, that has other
//! content, and that hasn't been answered yet. The newest one.
std::optional<DriveRemovedCopy> driveRemovedCopyFor(const QString& songPath);
//! Copies not answered yet
std::vector<DriveRemovedCopy> unansweredDriveRemovedCopies();
//! Writes the copy to songPath. The file there now is first kept as
//! ~/StarScore Studio/Autosave Archive/Replaced/<name> (replaced yyyy-MM-dd HHmm).<suffix>
bool putBackDriveCopy(const DriveRemovedCopy& copy, const QString& songPath, QString* error = nullptr);
//! Not asked about again
void markDriveCopyAnswered(const DriveRemovedCopy& copy);
}
