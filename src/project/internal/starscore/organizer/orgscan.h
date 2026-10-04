/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: walking Sheets and Demos and measuring its sheets.
 *
 * Every current sheet is measured once (pages, music symbols, words) and fingerprinted (md5); the results are
 * kept in 6 Inbox/.organizer/sheetcache.json and reused while a file's size and date stay the same, so only new
 * or changed files are read. The first run takes its numbers from the old toolkit's density.tsv and
 * fingerprints.json when the sizes still match.
 */
#pragma once

#include <map>
#include <vector>

#include <QMap>
#include <QString>
#include <QStringList>

#include "orgcore.h"

namespace mu::project::starscore::org {
struct FileEntry {
    QString rel;           // relative to Sheets and Demos
    qint64 size = 0;       // 0 for a bundle (a .logicx or other folder that is really one file; listed as one entry)
    qint64 mtime = 0;      // ms since epoch
};

struct SheetEntry {
    qint64 size = 0;
    qint64 mtime = 0;      // 0 = taken over from the old toolkit; adopt the file's date if the size matches
    QString md5;
    int pages = 0;
    int glyphs = 0;
    int words = 0;
    bool measured = false;
};

struct ScanResult {
    std::vector<FileEntry> tree;               // every file (not .organizer, not .DS_Store; a bundle counts as one file)
    std::map<QString, SheetEntry> sheets;      // current sheets by relative path
    QStringList added, changed, removed;       // compared with the previous run
    int measuredNow = 0;                       // files read this run
};

class SheetCache
{
public:
    //! Returns unreadableMessage() for a sheetcache.json that is there but can't be read, else "". Without the
    //! cache every sheet would be read again (a long run), so the run stops instead.
    QString load(const Paths& paths);
    //! Writes only when the entries changed since load()
    bool save(const Paths& paths) const;
    bool isEmpty() const { return m_entries.empty(); }
    const std::map<QString, SheetEntry>& entries() const { return m_entries; }
    void replace(std::map<QString, SheetEntry> entries);

private:
    std::map<QString, SheetEntry> m_entries;
    bool m_dirty = false;
};

inline bool operator==(const SheetEntry& a, const SheetEntry& b)
{
    return a.size == b.size && a.mtime == b.mtime && a.md5 == b.md5 && a.pages == b.pages && a.glyphs == b.glyphs
           && a.words == b.words && a.measured == b.measured;
}

//! A current sheet: a PDF in a song folder, outside the archive and the generated folders
bool isSheetPath(const QString& rel);
//! The song folder a path belongs to: "1 Amplitudes", "4 Works In Progress/Jeju" ("" for top-level files)
QString songRootOf(const QString& rel);

ScanResult scanBand(const Paths& paths, SheetCache& cache, const Progress& progress);
}
