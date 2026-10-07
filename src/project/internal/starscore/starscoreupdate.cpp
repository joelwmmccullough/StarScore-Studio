/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — "Update all sheets" (Joel, 6 Oct 2026): when a new StarScore makes sheets come out differently
 * (the sheet format, STARSCORE_SHEET_FORMAT), every song's sheets that were exported while Finished are made again;
 * only the ones that actually change are written, with their version's last number raised. Nothing that wasn't
 * Finished is touched, a sheet that comes out the same stays as it is, and every replaced file goes to Version History
 * as in any export.
 */
#include "starscoreservice.h"

#include <algorithm>
#include <map>
#include <set>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include "log.h"
#include "translation.h"

using namespace mu::project;
using namespace muse;

//! (starscorebandexport.cpp) the section folder a sheet belongs to, above "Section Scores" or "Horn 1"…
QString starscoreSectionFolderOf(const QString& rel);

namespace {
//! "1.2.3" as numbers, for comparing (missing parts are 0)
std::vector<int> versionParts(const QString& v)
{
    std::vector<int> out;
    for (const QString& p : v.split('.')) {
        out.push_back(p.toInt());
    }
    while (out.size() < 3) {
        out.push_back(0);
    }
    return out;
}

bool versionLess(const QString& a, const QString& b)
{
    return versionParts(a) < versionParts(b);
}

//! The last number raised: 1.2.3 -> 1.2.4
QString versionBumped(const QString& v)
{
    std::vector<int> p = versionParts(v.isEmpty() ? QString("1.0.0") : v);
    p[2] += 1;
    return QString("%1.%2.%3").arg(p[0]).arg(p[1]).arg(p[2]);
}

QJsonObject readJson(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? QJsonDocument::fromJson(f.readAll()).object() : QJsonObject();
}

//! A Score under its name from before 1.18.18 ("CODE - Score.pdf", "CODE - Score (Transposing).pdf", "CODE - Score (Bb).pdf")
//! or a 1.18.18 one ("CODE - Concert Score.pdf"…) not renamed yet
bool isOldScoreName(const QString& rel)
{
    static const QRegularExpression v11818(" - (Concert|Transposing|Bb|Eb|Treble Clef|Bass Clef) Score\\.pdf$");
    const QString file = rel.section('/', -1);
    return file.endsWith(" - Score.pdf") || file.contains(" - Score (") || v11818.match(file).hasMatch();
}
}

std::vector<StarScoreOutdatedSong> StarScoreService::outdatedSongs() const
{
    std::vector<StarScoreOutdatedSong> out;
    const QString library = auditLibraryFolder();
    const QString band = bandFolder();
    if (library.isEmpty() || band.isEmpty()) {
        return out;
    }
    // song folder in Sheets and Demos ("2 Bumper Cars") -> code, from codes.json
    const QJsonObject codes = readJson(band + "/6 Inbox/.organizer/codes.json");
    static const QRegularExpression category("^\\d+\\s+");
    std::map<QString, QString> codeOfTitle;   // "Bumper Cars" -> "BUCA"
    for (auto it = codes.begin(); it != codes.end(); ++it) {
        QString title = it.key().section('/', -1);
        title.remove(category);
        codeOfTitle[title.toLower()] = it.value().toString();
    }
    for (const QString& path : auditLibraryFiles(library)) {
        const QString title = QFileInfo(path).absoluteDir().dirName();
        auto c = codeOfTitle.find(title.toLower());
        if (c == codeOfTitle.end() || c->second.isEmpty()) {
            continue;   // never exported to Sheets and Demos
        }
        const QJsonObject record = readJson(band + "/6 Inbox/.organizer/sheets/" + c->second + ".json");
        if (record.isEmpty()) {
            continue;
        }
        const int format = record.value("sheetFormat").toInt(1);
        if (format >= STARSCORE_SHEET_FORMAT) {
            continue;
        }
        int finished = 0;
        const QJsonObject sheets = record.value("sheets").toObject();
        for (auto it = sheets.begin(); it != sheets.end(); ++it) {
            finished += it.value().toObject().value("status").toString() == "finished" ? 1 : 0;
        }
        if (finished == 0) {
            continue;
        }
        StarScoreOutdatedSong song;
        song.path = path;
        song.title = title;
        song.code = c->second;
        song.sheetFormat = format;
        song.exportedWith = record.value("exportedWith").toString();
        song.finishedSheets = finished;
        out.push_back(song);
    }
    std::sort(out.begin(), out.end(), [](const StarScoreOutdatedSong& a, const StarScoreOutdatedSong& b) {
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
    return out;
}

RetVal<QString> StarScoreService::updateCurrentSongSheets()
{
    const RetVal<StarScoreBandExportPlan> planned = planBandExport();
    if (!planned.ret) {
        return RetVal<QString>::make_ret(planned.ret);
    }
    const StarScoreBandExportPlan& plan = planned.val;
    if (plan.newSong || plan.code.isEmpty()) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "not in Sheets and Demos yet"));
    }
    const QString recordPath = plan.bandFolder + "/6 Inbox/.organizer/sheets/" + plan.code + ".json";
    // sheets exported under an older path or name moved to their place now (the Scores' names, 1.18.19; the Section
    // Scores and Horn subfolders, 1.18.22)
    const int renamed = moveSheetsToCurrentPaths(plan);
    QJsonObject record = readJson(recordPath);
    if (record.isEmpty()) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "no sheet record (never exported)"));
    }
    auto markCurrent = [&]() {
        QJsonObject r = readJson(recordPath);
        r["sheetFormat"] = STARSCORE_SHEET_FORMAT;
#ifdef STARSCORE_VERSION_STR
        r["exportedWith"] = QString::fromUtf8(STARSCORE_VERSION_STR);
#endif
        QSaveFile out(recordPath);
        if (out.open(QIODevice::WriteOnly)) {
            out.write(QJsonDocument(r).toJson(QJsonDocument::Indented));
            out.commit();
        }
    };
    const QString renamedNote = renamed > 0 ? muse::qtrc("starscore", " %1 sheet(s) moved or renamed.").arg(renamed) : QString();
    const Data data = load();

    // The sheets to make again, by the version printed on them: exported while Finished, and still Finished now (a
    // sheet being worked on again is left alone). A Score under its old name stands for all of its folder's Scores.
    std::map<QString, QString> versionOf;   // plan path -> the version on the sheet it replaces
    const QJsonObject sheets = record.value("sheets").toObject();
    for (auto it = sheets.begin(); it != sheets.end(); ++it) {
        const QJsonObject e = it.value().toObject();
        if (e.value("status").toString() != "finished") {
            continue;
        }
        const QString rel = it.key();
        const QString version = e.value("version").toString();
        const QString folder = rel.section('/', 0, -2);
        for (const StarScoreBandFile& f : plan.files) {
            if (!f.sourceFile.isEmpty()) {
                continue;   // reference PDFs are copied, not made
            }
            const bool same = f.relativePath == rel || f.formerPath == rel;   // (or the same sheet under its old name)
            const bool renamedScore = f.isScore && isOldScoreName(rel)
                                      && starscoreSectionFolderOf(f.relativePath) == starscoreSectionFolderOf(rel);
            if (!same && !renamedScore) {
                continue;
            }
            if (exportedSheetStatus(data, f) != StarScoreStatus::Finished) {
                continue;
            }
            auto have = versionOf.find(f.relativePath);
            if (have == versionOf.end() || versionLess(have->second, version)) {
                versionOf[f.relativePath] = version;
            }
        }
    }
    std::map<QString, QStringList> groups;   // version -> its sheets
    for (const auto& [rel, version] : versionOf) {
        groups[version] << rel;
    }

    const QString original = scoreVersion();
    // 1. which of them come out differently, each made with the version already printed on it
    std::map<QString, QStringList> changed;
    int same = 0;
    for (const auto& [version, paths] : groups) {
        setScoreVersion(version.isEmpty() ? original : version);
        m_exportDryRun = true;
        m_dryRunChanged.clear();
        const RetVal<QString> tried = exportToBandFolder(paths);
        m_exportDryRun = false;
        if (!tried.ret) {
            setScoreVersion(original);
            return RetVal<QString>::make_ret(Ret::Code::UnknownError,
                                             muse::qtrc("starscore", "comparing failed: %1").arg(QString::fromStdString(tried.ret.toString()))
                                             .toStdString());
        }
        if (!m_dryRunChanged.isEmpty()) {
            changed[version] = m_dryRunChanged;
        }
        same += int(paths.size()) - int(m_dryRunChanged.size());
        LOGI() << "[starscore] update " << plan.code << " v" << version << ": " << m_dryRunChanged.size() << " of "
               << paths.size() << " would change: " << m_dryRunChanged.join(", ");
    }

    // 2. those written, each with its version's last number raised; the song keeps the highest version
    QString highest = original;
    QStringList written;
    for (const auto& [version, paths] : changed) {
        const QString bumped = versionBumped(version.isEmpty() ? original : version);
        setScoreVersion(bumped);
        const RetVal<QString> done = exportToBandFolder(paths);
        if (!done.ret) {
            setScoreVersion(versionLess(highest, original) ? original : highest);
            return RetVal<QString>::make_ret(Ret::Code::UnknownError,
                                             muse::qtrc("starscore", "export failed: %1").arg(QString::fromStdString(done.ret.toString()))
                                             .toStdString());
        }
        written << paths;
        if (versionLess(highest, bumped)) {
            highest = bumped;
        }
    }
    setScoreVersion(highest);
    endExportProgress();

    // 3. the record marked as made with this StarScore (an export already did that; nothing written: done here)
    markCurrent();

    if (written.isEmpty()) {
        return RetVal<QString>::make_ok(muse::qtrc("starscore", "%1: no sheet changed (%2 checked).%3").arg(plan.code).arg(same)
                                        .arg(renamedNote));
    }
    return RetVal<QString>::make_ok(muse::qtrc("starscore", "%1: %2 sheet(s) updated, now up to version %3; %4 unchanged.%5")
                                    .arg(plan.code).arg(written.size()).arg(highest).arg(same).arg(renamedNote));
}

// ---------------------------------------------------------------------------
//  Going through the library, one song at a time
// ---------------------------------------------------------------------------

void StarScoreService::startUpdateAllSheets(const QStringList& paths)
{
    if (m_updateStatus.running) {
        return;
    }
    m_updateQueue = paths;
    m_bulkStyles = false;
    m_updateStatus = StarScoreUpdateAllStatus();
    m_updateStatus.running = true;
    m_updateStatus.songCount = int(paths.size());
    m_updateStatus.songIndex = -1;
    LOGI() << "[starscore] update all sheets: " << paths.size() << " song(s)";
    QTimer::singleShot(0, &m_timerGuard, [this]() { updateAllNext(); });
}

void StarScoreService::startApplyStylesToAll(const QStringList& paths)
{
    if (m_updateStatus.running) {
        return;
    }
    startUpdateAllSheets(paths);
    m_bulkStyles = true;
    LOGI() << "[starscore] (applying part styles, not updating sheets)";
}

void StarScoreService::cancelUpdateAllSheets()
{
    // stops after the song being updated
    m_updateQueue.clear();
    m_updateStatus.results << muse::qtrc("starscore", "Stopped: the songs after this one were left as they were.");
}

StarScoreUpdateAllStatus StarScoreService::updateAllStatus() const
{
    StarScoreUpdateAllStatus s = m_updateStatus;
    if (s.running && m_exportProgress.running) {
        s.phase = QString("%1 %2 of %3: %4").arg(m_exportProgress.phase).arg(m_exportProgress.done + 1)
                  .arg(m_exportProgress.total).arg(m_exportProgress.step);
    }
    return s;
}

void StarScoreService::updateAllNext()
{
    if (m_updateQueue.isEmpty()) {
        m_updateStatus.running = false;
        m_updateStatus.finished = true;
        m_updateStatus.phase = muse::qtrc("starscore", "Done");
        LOGI() << "[starscore] update all sheets: done";
        return;
    }
    const QString path = m_updateQueue.takeFirst();
    ++m_updateStatus.songIndex;
    m_updateStatus.song = QFileInfo(path).absoluteDir().dirName();
    m_updateStatus.phase = muse::qtrc("starscore", "Opening");
    m_updateWaits = 0;
    LOGI() << "[starscore] update all sheets: opening " << path;
    auto current = globalContext()->currentProject();
    if (!current || QFileInfo(current->path().toQString()).absoluteFilePath() != QFileInfo(path).absoluteFilePath()) {
        // the song open now saved first, so opening the next one doesn't stop at "Save changes?"
        if (current && current->needSave().val && !projectFilesController()->saveProject()) {
            m_updateStatus.results << muse::qtrc("starscore", "Stopped: the song that was open couldn't be saved. Save it, then start again.");
            m_updateQueue.clear();
            m_updateStatus.running = false;
            m_updateStatus.finished = true;
            return;
        }
        dispatcher()->dispatch("starscore-audit-open", muse::actions::ActionData::make_arg1<QUrl>(QUrl::fromLocalFile(path)));
    }
    QTimer::singleShot(500, &m_timerGuard, [this, path]() { updateAllWhenOpen(path); });
}

void StarScoreService::updateAllWhenOpen(const QString& path)
{
    auto current = globalContext()->currentProject();
    const bool open = current && QFileInfo(current->path().toQString()).absoluteFilePath() == QFileInfo(path).absoluteFilePath();
    if (!open) {
        if (++m_updateWaits > 120) {   // a minute
            m_updateStatus.results << muse::qtrc("starscore", "%1: couldn't be opened; left as it was.").arg(m_updateStatus.song);
            QTimer::singleShot(200, &m_timerGuard, [this]() { updateAllNext(); });
            return;
        }
        QTimer::singleShot(500, &m_timerGuard, [this, path]() { updateAllWhenOpen(path); });
        return;
    }
    // opened: a few seconds for what runs once a song opens (its layout, tabs, tidying), then the update
    if (m_updateWaits >= 0) {
        m_updateWaits = -1;
        m_updateStatus.phase = muse::qtrc("starscore", "Opened");
        QTimer::singleShot(4000, &m_timerGuard, [this, path]() { updateAllWhenOpen(path); });
        return;
    }
    QString line;
    if (m_bulkStyles) {
        m_updateStatus.phase = muse::qtrc("starscore", "Applying part styles");
        const int n = applyStyles();
        line = muse::qtrc("starscore", "%1: part styles applied to %2 score(s) and part book(s).").arg(m_updateStatus.song).arg(n);
    } else {
        m_updateStatus.phase = muse::qtrc("starscore", "Comparing");
        const RetVal<QString> result = updateCurrentSongSheets();
        line = result.ret ? result.val
               : muse::qtrc("starscore", "%1: not updated (%2).").arg(m_updateStatus.song,
                                                                       QString::fromStdString(result.ret.toString()));
    }
    // saved, so the next song can open in its place (and the new version numbers are kept)
    m_updateStatus.phase = muse::qtrc("starscore", "Saving");
    if (current->needSave().val && !projectFilesController()->saveProject()) {
        line += " " + muse::qtrc("starscore", "Couldn't save the song: save it by hand before closing it.");
        m_updateQueue.clear();   // the next song would ask to save this one: stop here
    }
    m_updateStatus.results << line;
    LOGI() << "[starscore] update all sheets: " << line;
    QTimer::singleShot(500, &m_timerGuard, [this]() { updateAllNext(); });
}
