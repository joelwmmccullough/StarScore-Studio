/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — organizer: Finder colours from the sheet records (see orgcolours.h)
 */
#include "orgcolours.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

namespace mu::project::starscore::org {
namespace {
//! empty 0, sketch 1, in progress 2, needs review 3, finished 4; -1 = no status
int rank(const QString& key)
{
    if (key == "finished") {
        return 4;
    }
    if (key == "needs-review") {
        return 3;
    }
    if (key == "in-progress") {
        return 2;
    }
    if (key == "sketch") {
        return 1;
    }
    if (key == "empty") {
        return 0;
    }
    return -1;
}

class Checker
{
public:
    Checker(const Paths& paths, const QString& songRoot, const QJsonObject& record, const std::map<QString, SheetEntry>& sheets)
        : m_dir(joinPath(paths.band, songRoot)), m_root(songRoot), m_sheets(record.value("sheets").toObject()), m_scan(sheets)
    {
    }

    bool exists(const QString& rel) const { return QFileInfo(m_dir + "/" + rel).isFile(); }

    //! The status the file was exported with, while it is still that file; -1 otherwise
    int status(const QString& rel)
    {
        auto memo = m_memo.find(rel);
        if (memo != m_memo.end()) {
            return memo->second;
        }
        int result = -1;
        const QJsonObject rec = m_sheets.value(rel).toObject();
        const QFileInfo fi(m_dir + "/" + rel);
        if (!rec.isEmpty() && fi.isFile() && qint64(rec.value("size").toDouble()) == fi.size()) {
            QString md5;
            auto it = m_scan.find(m_root + "/" + rel);
            if (it != m_scan.end() && it->second.size == fi.size()) {
                md5 = it->second.md5;
            }
            if (md5.isEmpty()) {
                md5 = md5OfFile(fi.filePath());
            }
            if (!md5.isEmpty() && md5 == rec.value("md5").toString()) {
                result = rank(rec.value("status").toString());
            }
        }
        m_memo[rel] = result;
        return result;
    }

    const QString& dir() const { return m_dir; }

private:
    QString m_dir;
    QString m_root;
    QJsonObject m_sheets;
    const std::map<QString, SheetEntry>& m_scan;
    std::map<QString, int> m_memo;
};

QStringList strings(const QJsonValue& v)
{
    QStringList out;
    for (const QJsonValue& x : v.toArray()) {
        out << x.toString();
    }
    return out;
}
}

std::map<QString, QJsonObject> loadSheetRecords(const Paths& paths)
{
    std::map<QString, QJsonObject> out;
    const QDir dir(paths.toolkit + "/sheets");
    for (const QString& f : dir.entryList({ "*.json" }, QDir::Files, QDir::Name)) {
        const QJsonObject o = readJsonObject(dir.filePath(f));
        const QString code = o.value("code").toString();
        if (!code.isEmpty()) {
            out[code] = o;
        }
    }
    return out;
}

SongColours songColours(const Paths& paths, const QString& songRoot, const QJsonObject& record,
                        const std::map<QString, SheetEntry>& sheets)
{
    SongColours out;
    Checker check(paths, songRoot, record, sheets);

    // --- the song folder: up the ladder while each colour's sheets are there and far enough along
    static const std::vector<std::tuple<QString, QString, int> > LADDER {
        { "red", "Red", 1 }, { "orange", "Orange", 4 }, { "yellow", "Yellow", 4 },
        { "green", "Green", 4 }, { "blue", "Blue", 4 }, { "purple", "Purple", 4 },
    };
    static const QStringList LEVEL { "empty", "Sketch", "In progress", "Needs review", "Finished" };
    const QJsonObject tiers = record.value("tiers").toObject();
    out.song = "Gray";
    for (const auto& [key, colour, need] : LADDER) {
        const QJsonObject t = tiers.value(key).toObject();
        QStringList why = strings(t.value("missing"));
        for (const QString& p : strings(t.value("paths"))) {
            const int s = check.status(p);
            if (s < need) {
                why << (s < 0 ? (check.exists(p) ? QString("%1: not exported from StarScore as it is now").arg(p)
                                 : QString("%1: missing").arg(p))
                        : QString("%1: exported as %2").arg(p, LEVEL.value(s)));
            }
        }
        if (!why.isEmpty()) {
            out.why = why;
            break;
        }
        out.song = colour;
    }

    // --- each sheet folder
    const QJsonObject folders = record.value("folders").toObject();
    for (auto it = folders.begin(); it != folders.end(); ++it) {
        const QString folder = it.key();
        const QString abs = check.dir() + "/" + folder;
        if (!QFileInfo(abs).isDir()) {
            continue;
        }
        const QJsonObject o = it.value().toObject();
        bool gray = !strings(o.value("missing")).isEmpty();
        for (const QString& rel : strings(o.value("required"))) {
            gray |= !check.exists(rel);
        }
        // Percussion sheets (Congas, Bongos…) never count toward a colour
        static const QRegularExpression percussion(" - (Percussion|Congas|Bongos|Timbales|Cajon|Shakers?)( \\(.*\\))?( \\d+)?\\.pdf$",
                                                   QRegularExpression::CaseInsensitiveOption);
        int least = 4;
        int counted = 0;
        const QStringList files = QDir(abs).entryList({ "*.pdf", "*.PDF" }, QDir::Files, QDir::Name);
        for (const QString& f : files) {
            if (!f.startsWith('.') && !percussion.match(f).hasMatch()) {
                least = std::min(least, check.status(folder + "/" + f));
                ++counted;
            }
        }
        // sheets it needs in a subfolder (the 7-Horn bass horns) count too
        for (const QString& rel : strings(o.value("required"))) {
            if (rel.section('/', 0, -2) != folder && check.exists(rel)) {
                least = std::min(least, check.status(rel));
                ++counted;
            }
        }
        if (counted == 0) {
            gray = true;
        }
        const QString colour = gray || least < 1 ? QString("Gray")
                               : least == 4 ? QString("Green") : least == 3 ? QString("Yellow")
                               : least == 2 ? QString("Orange") : QString("Red");
        out.folders.emplace_back(folder, colour);
    }
    return out;
}
}
