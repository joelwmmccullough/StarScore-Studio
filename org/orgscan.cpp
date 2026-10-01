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

#include "orgplatform.h"

namespace mu::project::starscore::org {
static const char* CACHE_FILE = "/sheetcache.json";

void SheetCache::load(const Paths& paths)
{
    m_entries.clear();
    const QJsonObject o = readJsonObject(paths.toolkit + CACHE_FILE);
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
        return;
    }

    // First run: the old toolkit's measurements (density.tsv) and fingerprints (fingerprints.json)
    QFile density(paths.toolkit + "/density.tsv");
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
    const QJsonObject fp = readJsonObject(paths.toolkit + "/fingerprints.json");
    for (auto it = fp.begin(); it != fp.end(); ++it) {
        auto e = m_entries.find(it.key());
        if (e != m_entries.end()) {
            e->second.md5 = it.value().toString();
        }
    }
}

bool SheetCache::save(const Paths& paths) const
{
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
    if (seg[0].startsWith("5 Archive") || seg[0] == "6 Inbox" || !seg[0].contains(QRegularExpression("^[1-4] "))) {
        return false;
    }
    for (const QString& s : seg.mid(0, seg.size() - 1)) {
        if (s == "Version History" || s == "Old Versions" || s.startsWith("Update Notes") || s == "Horn Part Guides"
            || s.startsWith('.')) {
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
    QDirIterator it(base, QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString rel = relativeTo(base, fi.absoluteFilePath());
        if (rel.contains("/.organizer/") || rel.startsWith(".organizer/") || fi.fileName() == ".DS_Store"
            || fi.fileName().startsWith("Icon\r")) {
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
