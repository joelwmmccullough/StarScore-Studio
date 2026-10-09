/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: walking Sheets and Demos and measuring its sheets
 */
#include "orgscan.h"

#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QRegularExpression>

#include "orgplatform.h"
#include "orgstores.h"

namespace mu::project::starscore::org {
static const char* CACHE_FILE = "/sheetcache.json";

QString SheetCache::load(const Paths& paths)
{
    m_entries.clear();
    m_dirty = false;
    const JsonRead read = readJsonChecked(paths.toolkit + CACHE_FILE);
    if (read.unreadable()) {
        return unreadableMessage("sheetcache.json", read);
    }
    const QJsonObject o = read.doc.object();
    if (!o.isEmpty()) {
        const QJsonObject files = o.value("files").toObject();
        for (auto it = files.begin(); it != files.end(); ++it) {
            const QJsonArray a = it.value().toArray();    // [size, mtime, md5, pages, glyphs, words, measured]
            SheetEntry e;
            e.size = qint64(a.at(0).toDouble());
            e.mtime = qint64(a.at(1).toDouble());
            e.md5 = a.at(2).toString();
            e.pages = a.at(3).toInt();
            e.glyphs = a.at(4).toInt();
            e.words = a.at(5).toInt();
            e.measured = a.at(6).toBool(true);
            m_entries[it.key()] = e;
        }
        return QString();
    }

    // First run: the old toolkit's measurements (density.tsv) and fingerprints (fingerprints.json), in the
    // toolkit or, when a first run was cut short after retiring them, in the newest Deprecated/Retired folder
    QString oldToolkit = paths.toolkit;
    if (!QFileInfo::exists(oldToolkit + "/density.tsv")) {
        QStringList retired = QDir(paths.toolkit + "/Deprecated").entryList({ "Retired *" }, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        for (int i = int(retired.size()) - 1; i >= 0; --i) {
            const QString dir = paths.toolkit + "/Deprecated/" + retired[i];
            if (QFileInfo::exists(dir + "/density.tsv")) {
                oldToolkit = dir;
                break;
            }
        }
    }
    QFile density(oldToolkit + "/density.tsv");
    if (density.open(QIODevice::ReadOnly)) {
        for (const QByteArray& line : density.readAll().split('\n')) {
            const QList<QByteArray> f = line.split('\t');
            if (f.size() < 5) {
                continue;
            }
            SheetEntry e;
            e.pages = f[1].toInt();
            e.glyphs = f[2].toInt();
            e.words = f[3].toInt();
            e.size = f[4].toLongLong();
            e.measured = true;
            m_entries[QString::fromUtf8(f[0])] = e;
        }
    }
    const QJsonObject fp = readJsonObject(oldToolkit + "/fingerprints.json");
    for (auto it = fp.begin(); it != fp.end(); ++it) {
        auto e = m_entries.find(it.key());
        if (e != m_entries.end()) {
            e->second.md5 = it.value().toString();
        }
    }
    // taken over from the old toolkit: not in sheetcache.json yet, so the first save must write it
    m_dirty = !m_entries.empty();
    return QString();
}

void SheetCache::replace(std::map<QString, SheetEntry> entries)
{
    if (entries != m_entries) {
        m_entries = std::move(entries);
        m_dirty = true;
    }
}

bool SheetCache::save(const Paths& paths) const
{
    if (!m_dirty) {
        return true;    // every entry is as it was read: sheetcache.json is up to date
    }
    QJsonObject files;
    for (const auto& [rel, e] : m_entries) {
        files[rel] = QJsonArray { double(e.size), double(e.mtime), e.md5, e.pages, e.glyphs, e.words, e.measured };
    }
    QJsonObject o;
    o["version"] = 1;
    o["about"] = "StarScore organizer: size, date (ms), md5, pages, music symbols, words for every current sheet";
    o["files"] = files;
    return writeJson(paths.toolkit + CACHE_FILE, QJsonDocument(o), true);
}

QString songRootOf(const QString& rel)
{
    const QStringList seg = rel.split('/');
    if (seg.size() < 2) {
        return QString();
    }
    if (seg[0] == "4 Works In Progress") {
        return seg.size() >= 3 ? seg[0] + "/" + seg[1] : QString();
    }
    return seg[0];
}

bool isSheetPath(const QString& rel)
{
    if (!rel.endsWith(".pdf", Qt::CaseInsensitive) || !rel.contains('/')) {
        return false;
    }
    const QStringList seg = rel.split('/');
    static const QRegularExpression songGroup("^[1-4] ");
    if (seg[0].startsWith("5 Archive") || seg[0] == "6 Inbox" || !songGroup.match(seg[0]).hasMatch()) {
        return false;
    }
    for (const QString& s : seg.mid(0, seg.size() - 1)) {
        // (Annotated Sheets: copies of sheets with a player's own notes, made by the export alongside the sheets)
        if (s == "Version History" || s == "Old Versions" || s.startsWith("Update Notes") || s == "Horn Part Guides"
            || s == "Annotated Sheets" || s.startsWith('.')) {
            return false;
        }
    }
    // loose files at the top of a song folder aren't filed sheets
    if (seg.size() < (seg[0] == "4 Works In Progress" ? 4 : 3)) {
        return false;
    }
    const QString name = seg.last();
    return !name.contains("What's Here") && !name.endsWith(" - Recordings.pdf");
}

ScanResult scanBand(const Paths& paths, SheetCache& cache, const Progress& progress)
{
    ScanResult r;
    const QString base = paths.band;
    // Folders are listed too, so that a bundle (.logicx, .band, .pages…: a folder that is really one file) can be
    // taken as one entry. Until Oct 2026 the walk went inside bundles and every inner file counted as a file of the
    // song, which inflated the totals and listed a Logic project's innards in What's Here.
    QDirIterator it(base, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString rel = relativeTo(base, fi.absoluteFilePath());
        if (rel.contains("/.organizer/") || rel.startsWith(".organizer/") || fi.fileName() == ".DS_Store"
            || fi.fileName().startsWith("Icon\r")) {
            continue;
        }
        const QStringList seg = rel.split('/');
        bool inBundle = false;
        for (int i = 0; i < seg.size() - 1 && !inBundle; ++i) {
            inBundle = isBundle(seg[i]);
        }
        if (inBundle) {
            continue;
        }
        if (fi.isDir()) {
            if (isBundle(fi.fileName())) {
                r.tree.push_back({ rel, 0, fi.lastModified().toMSecsSinceEpoch() });
            }
            continue;
        }
        r.tree.push_back({ rel, fi.size(), fi.lastModified().toMSecsSinceEpoch() });
    }
    std::sort(r.tree.begin(), r.tree.end(), [](const FileEntry& a, const FileEntry& b) { return a.rel < b.rel; });

    const std::map<QString, SheetEntry>& old = cache.entries();
    std::map<QString, SheetEntry> now;
    std::vector<const FileEntry*> sheets;
    for (const FileEntry& f : r.tree) {
        if (isSheetPath(f.rel)) {
            sheets.push_back(&f);
        }
    }
    int done = 0;
    for (const FileEntry* f : sheets) {
        if (progress.stopped()) {
            break;
        }
        SheetEntry e;
        auto o = old.find(f->rel);
        const bool same = o != old.end() && o->second.size == f->size && !o->second.md5.isEmpty()
                          && (o->second.mtime == f->mtime || o->second.mtime == 0);
        if (same) {
            e = o->second;
            e.mtime = f->mtime;
        } else {
            const QString abs = base + "/" + f->rel;
            e.size = f->size;
            e.mtime = f->mtime;
            e.md5 = md5OfFile(abs);
            if (o != old.end() && o->second.md5 == e.md5 && o->second.measured) {
                e.pages = o->second.pages;
                e.glyphs = o->second.glyphs;
                e.words = o->second.words;
                e.measured = true;
            } else {
                const PdfMeasure m = measurePdf(abs);
                e.pages = m.pages;
                e.glyphs = m.glyphs;
                e.words = m.words;
                e.measured = m.ok;
                ++r.measuredNow;
            }
        }
        now[f->rel] = e;
        if (++done % 50 == 0) {
            progress.at(QString("Reading sheets (%1 of %2)").arg(done).arg(sheets.size()), double(done) / sheets.size());
        }
    }

    if (!old.empty()) {
        for (const auto& [rel, e] : now) {
            auto o = old.find(rel);
            if (o == old.end()) {
                r.added << rel;
            } else if (!o->second.md5.isEmpty() && o->second.md5 != e.md5) {
                r.changed << rel;
            }
        }
        for (const auto& [rel, e] : old) {
            if (!now.count(rel)) {
                r.removed << rel;
            }
        }
    }
    r.sheets = now;
    if (!progress.stopped()) {
        cache.replace(now);
    }
    return r;
}
}
