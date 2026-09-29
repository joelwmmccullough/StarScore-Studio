/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Songbooks
 */
#include "songbookmodel.h"

#include <algorithm>
#include <functional>
#include <memory>

#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include "translation.h"
#include "starscoresongs.h"
#include "songbookpages.h"
#include "project/internal/starscore/starscorepdf.h"

using namespace mu::project;
using namespace mu::project::starscoresongs;

namespace {
struct Seat {
    QString line;        // "Duo · Bottom line"
    QString kind;        // "DUO" / "TRIO"
    QString title;       // "Bottom line"
    const char* section; // "2-horn-any" / "3-horn-any"
    int chair;
    int dia;
    int chrom;
    int clef;
};

struct BookDef {
    QString id;
    QString title;           // in the list
    bool chart = false;
    // album books
    QString instrument;      // on the cover and each sheet
    QString keyLabel;
    bool horn = true;
    int leadDia = 0, leadChrom = 0, leadClef = 0;
    bool leadTranspose = false;
    std::vector<Seat> seats;
    QString rhythmRole;      // rhythm books: the part
    QString rhythmTitle;
    QString linesSummary;
    // charts
    QString arrangement;     // template key
    QString chartTitle;      // "5-Horn Arrangement"
};

const std::vector<BookDef>& bookDefs()
{
    static const std::vector<BookDef> defs = [] {
        std::vector<BookDef> d;
        auto horn = [](const QString& id, const QString& title, const QString& instrument, const QString& key, int ld, int lc,
                       int lclef, std::vector<Seat> seats, const QString& summary) {
            BookDef b;
            b.id = id;
            b.title = title;
            b.instrument = instrument;
            b.keyLabel = key;
            b.leadTranspose = true;
            b.leadDia = ld;
            b.leadChrom = lc;
            b.leadClef = lclef;
            b.seats = std::move(seats);
            b.linesSummary = summary;
            return b;
        };
        const QString flat = QString::fromUtf8("♭");
        d.push_back(horn("tenor", "Tenor Sax Songbook", "Tenor Saxophone", "Parts in B" + flat, -1, -2, 0, {
            { "Duo · Bottom line", "DUO", "Bottom line", "2-horn-any", 2, -8, -14, 0 },
            { "Trio · Middle line", "TRIO", "Middle line", "3-horn-any", 2, -8, -14, 0 },
            { "Trio · Bottom line", "TRIO", "Bottom line", "3-horn-any", 3, -8, -14, 0 },
        }, "Tenor takes the bottom line in a duo. In a trio, take the bottom line if you're the lowest horn; "
           "if a bari, trombone or bass clarinet is there, take the middle line."));
        d.push_back(horn("alto", "Alto Sax Songbook", "Alto Saxophone", "Parts in E" + flat, -5, -9, 0, {
            { "Duo · Top line", "DUO", "Top line", "2-horn-any", 1, -5, -9, 0 },
            { "Trio · Top line", "TRIO", "Top line", "3-horn-any", 1, -5, -9, 0 },
            { "Trio · Middle line", "TRIO", "Middle line", "3-horn-any", 2, -5, -9, 0 },
        }, "Alto takes the top line in a duo. In a trio, take the top line if you're the highest horn; "
           "if a trumpet, soprano or clarinet is there, take the middle line."));
        d.push_back(horn("trumpet", QString("B%1 Trumpet Songbook").arg(flat), "Trumpet in B" + flat, "Parts in B" + flat, -1, -2, 0, {
            { "Duo · Top line", "DUO", "Top line", "2-horn-any", 1, -1, -2, 0 },
            { "Trio · Top line", "TRIO", "Top line", "3-horn-any", 1, -1, -2, 0 },
            { "Trio · Middle line", "TRIO", "Middle line", "3-horn-any", 2, -1, -2, 0 },
        }, "Trumpet takes the top line in a duo. In a trio, take the top line; with a second trumpet (or a soprano "
           "or clarinet on top), take the middle line."));
        d.push_back(horn("trombone", "Trombone Songbook", "Trombone", "Parts in bass clef", 7, 12, 1, {
            { "Duo · Bottom line", "DUO", "Bottom line", "2-horn-any", 2, 0, 0, 1 },
            { "Trio · Bottom line", "TRIO", "Bottom line", "3-horn-any", 3, 0, 0, 1 },
        }, "Trombone takes the bottom line in a duo and in a trio."));

        auto rhythm = [](const QString& id, const QString& title, const QString& instrument, const QString& key, bool transposeLead,
                         int ld, int lc, int lclef, const QString& role, const QString& partTitle) {
            BookDef b;
            b.id = id;
            b.title = title;
            b.instrument = instrument;
            b.keyLabel = key;
            b.horn = false;
            b.leadTranspose = transposeLead;
            b.leadDia = ld;
            b.leadChrom = lc;
            b.leadClef = lclef;
            b.rhythmRole = role;
            b.rhythmTitle = partTitle;
            return b;
        };
        d.push_back(rhythm("piano", "Piano Songbook", "Piano", "Parts in C", false, 0, 0, 0, "keys", "Piano part"));
        d.push_back(rhythm("guitar", "Guitar Songbook", "Guitar", "Parts in C", false, 0, 0, 0, "guitar", "Guitar part"));
        d.push_back(rhythm("bass", "Bass Songbook", "Bass", "Parts in bass clef", true, 7, 12, 1, "bass", "Bass part"));

        auto chart = [](const QString& id, const QString& title, const QString& tpl, const QString& chartTitle) {
            BookDef b;
            b.id = id;
            b.title = title;
            b.chart = true;
            b.arrangement = tpl;
            b.chartTitle = chartTitle;
            return b;
        };
        d.push_back(chart("c4", "4-Horn Chart", "4-horn-standard", "4-Horn Arrangement"));
        d.push_back(chart("c5", "5-Horn Chart", "5-horn-standard", "5-Horn Arrangement"));
        d.push_back(chart("c6", "6-Horn Chart", "6-horn-standard", "6-Horn Arrangement"));
        d.push_back(chart("c7", "7-Horn Chart", "7-horn-standard", "7-Horn Arrangement"));
        d.push_back(chart("bigband", "Big Band Chart", "big-band", "Big Band Arrangement"));
        d.push_back(chart("marching", "Marching Band Chart", "marching-band", "Marching Band Arrangement"));
        d.push_back(chart("orchestra", "Orchestra Chart", "orchestra", "Orchestra Arrangement"));
        return d;
    }();
    return defs;
}

const BookDef& bookDef(const QString& id)
{
    for (const BookDef& b : bookDefs()) {
        if (b.id == id) {
            return b;
        }
    }
    return bookDefs().front();
}

QString asciiName(QString s)
{
    s.replace(QString::fromUtf8("♭"), "b").replace(QString::fromUtf8("♯"), "#");
    s.replace(QRegularExpression("[/\\\\:*?\"<>|]"), "-");
    return s.trimmed();
}

//! Moves an existing file or folder into <root>/Deprecated/<date>/… (Joel's rule: old versions are kept, not deleted)
void deprecate(const QString& root, const QString& path)
{
    if (!QFileInfo::exists(path)) {
        return;
    }
    const QString rel = QDir(root).relativeFilePath(path);
    QString target = root + "/Deprecated/" + QDate::currentDate().toString(Qt::ISODate) + "/" + rel;
    QDir().mkpath(QFileInfo(target).absolutePath());
    const QString base = target;
    for (int i = 2; QFileInfo::exists(target); ++i) {
        target = QFileInfo(base).isDir() || !base.contains('.') ? QString("%1 (%2)").arg(base).arg(i)
                 : QString("%1 (%2).%3").arg(base.section('.', 0, -2)).arg(i).arg(base.section('.', -1));
    }
    QDir().rename(path, target);
}
}

SongbookModel::SongbookModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariantList SongbookModel::books() const
{
    QVariantList out;
    for (const BookDef& b : bookDefs()) {
        out << QVariantMap { { "id", b.id }, { "title", b.title }, { "chart", b.chart } };
    }
    return out;
}

QString SongbookModel::bookId() const
{
    return m_bookId;
}

bool SongbookModel::isChart() const
{
    return bookDef(m_bookId).chart;
}

QStringList SongbookModel::albums() const
{
    return m_albumOrder;
}

QString SongbookModel::album() const
{
    return m_album;
}

QString SongbookModel::tracklist() const
{
    auto it = m_albums.find(m_album);
    return it == m_albums.end() ? QString() : it->second.join("\n");
}

QVariantList SongbookModel::librarySongs() const
{
    QVariantList out;
    for (const QString& path : m_libraryFiles) {
        out << QVariantMap { { "title", QFileInfo(path).absoluteDir().dirName() }, { "path", path } };
    }
    return out;
}

QString SongbookModel::songPath() const
{
    return m_songPath;
}

QVariantList SongbookModel::plan() const
{
    return m_plan;
}

int SongbookModel::readyCount() const
{
    return m_ready;
}

int SongbookModel::songCount() const
{
    return int(m_songs.size());
}

bool SongbookModel::busy() const
{
    return m_busy || m_refreshing;
}

QString SongbookModel::status() const
{
    return m_status;
}

QString SongbookModel::report() const
{
    return m_report;
}

QString SongbookModel::outputFolder() const
{
    const QString saved = QSettings().value("StarScore/songbook/output").toString();
    return saved.isEmpty() ? defaultOutput() : saved;
}

QString SongbookModel::lastOutput() const
{
    return m_lastOutput;
}

QString SongbookModel::defaultOutput() const
{
    if (!m_library.isEmpty()) {
        return m_library + "/Songbooks";
    }
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) + "/Starsign Songbooks";
}

void SongbookModel::load()
{
    m_library = starScore()->auditLibraryFolder();
    m_libraryFiles = starScore()->auditLibraryFiles(m_library);

    // Album tracklists: saved, or the ones known on 29 Sep 2026
    const QJsonObject saved = QJsonDocument::fromJson(QSettings().value("StarScore/songbook/albums").toByteArray()).object();
    m_albums.clear();
    m_albumOrder.clear();
    for (const QJsonValue& v : saved.value("order").toArray()) {
        const QString name = v.toString();
        QStringList songs;
        for (const QJsonValue& s : saved.value("albums").toObject().value(name).toArray()) {
            songs << s.toString();
        }
        m_albums[name] = songs;
        m_albumOrder << name;
    }
    if (m_albumOrder.isEmpty()) {
        m_albums["Ichiban"] = { "Seagrass", "Fish Oil", "Cumulonimbus", "Deimos", "Shatter", "Amplitudes", "Royal", "Deep Speech",
                                "Last Pint", "Okane" };
        m_albums["Feed Your Kids Bugs"] = { "Double Entendre", "Feed Your Kids Bugs", "Everpresent", "Branston Pickle", "February",
                                            "Hit List", "Dream of Mushroom", "Updog" };
        m_albumOrder = { "Ichiban", "Feed Your Kids Bugs" };
    }
    if (!m_albums.count(m_album)) {
        m_album = m_albumOrder.value(0);
    }
    if (m_songPath.isEmpty() && !m_libraryFiles.isEmpty()) {
        m_songPath = m_libraryFiles.first();
    }
    recheck();
}

void SongbookModel::saveAlbums() const
{
    QJsonObject albums;
    for (const auto& [name, songs] : m_albums) {
        albums[name] = QJsonArray::fromStringList(songs);
    }
    const QJsonObject o { { "order", QJsonArray::fromStringList(m_albumOrder) }, { "albums", albums } };
    QSettings().setValue("StarScore/songbook/albums", QJsonDocument(o).toJson(QJsonDocument::Compact));
}

void SongbookModel::setBookId(const QString& id)
{
    if (m_busy || id == m_bookId) {
        return;
    }
    m_bookId = id;
    m_report.clear();
    recheck();
}

void SongbookModel::setAlbum(const QString& album)
{
    if (m_busy || !m_albums.count(album)) {
        return;
    }
    m_album = album;
    m_report.clear();
    recheck();
}

void SongbookModel::setTracklist(const QString& text)
{
    QStringList songs;
    for (const QString& line : text.split('\n')) {
        const QString t = line.trimmed();
        if (!t.isEmpty()) {
            songs << t;
        }
    }
    m_albums[m_album] = songs;
    saveAlbums();
    recheck();
}

void SongbookModel::addAlbum(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty() || m_albums.count(n)) {
        return;
    }
    m_albums[n] = {};
    m_albumOrder << n;
    m_album = n;
    saveAlbums();
    recheck();
}

void SongbookModel::setSongPath(const QString& path)
{
    if (m_busy) {
        return;
    }
    m_songPath = path;
    m_report.clear();
    recheck();
}

QString SongbookModel::notes(const QString& song) const
{
    return QSettings().value("StarScore/songbook/notes/" + normalName(song)).toString();
}

void SongbookModel::setNotes(const QString& song, const QString& text)
{
    QSettings().setValue("StarScore/songbook/notes/" + normalName(song), text);
}

void SongbookModel::chooseOutputFolder()
{
    const muse::io::path_t dir = interactive()->selectDirectory(muse::trc("starscore", "Where songbooks and charts are saved"),
                                                                muse::io::path_t(outputFolder()));
    if (dir.empty()) {
        return;
    }
    QSettings().setValue("StarScore/songbook/output", dir.toQString());
    emit changed();
}

void SongbookModel::openOutput()
{
    const QString target = m_lastOutput.isEmpty() ? outputFolder() : m_lastOutput;
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(target).isDir() ? target : QFileInfo(target).absolutePath()));
}

SongbookModel::Song SongbookModel::songFor(const QString& title) const
{
    Song s;
    s.title = title;
    s.legacy = isLegacy(title);
    for (const QString& path : m_libraryFiles) {
        if (normalName(QFileInfo(path).absoluteDir().dirName()) == normalName(title)) {
            s.path = path;
            break;
        }
    }
    return s;
}

std::vector<SongbookModel::SheetPlan> SongbookModel::sheetsFor(const Song& song) const
{
    const BookDef& b = bookDef(m_bookId);
    std::vector<SheetPlan> out;
    const StarScoreAuditFileSummary& sum = song.summary;
    const QString notDone = song.legacy ? muse::qtrc("starscore", "not audited yet") : muse::qtrc("starscore", "not marked Finished");

    auto arrangementReady = [&](const QString& tpl, const QString& name, QString* why) {
        const StarScoreFileArrangement* found = nullptr;
        for (const StarScoreFileArrangement& a : sum.arrangementList) {
            if (a.templateKey == tpl || (tpl == "2-horn-any" && a.column == "2F") || (tpl == "3-horn-any" && a.column == "3F")) {
                if (!found || arrangementDone(song.legacy, a)) {
                    found = &a;
                }
            }
        }
        if (!found) {
            *why = muse::qtrc("starscore", "no %1 arrangement").arg(name);
            return false;
        }
        if (!arrangementDone(song.legacy, *found)) {
            *why = QString("%1 %2").arg(name, notDone);
            return false;
        }
        return true;
    };

    // Solo: the lead sheet
    {
        SheetPlan sp;
        sp.sheet.kind = "lead";
        sp.sheet.transpose = b.leadTranspose;
        sp.sheet.transposeDiatonic = b.leadDia;
        sp.sheet.transposeChromatic = b.leadChrom;
        sp.sheet.clef = b.leadClef;
        sp.sheet.left = b.instrument;
        sp.sheet.right = b.horn ? QString("Solo · Melody and changes") : QString("Lead Sheet");
        sp.label = b.horn ? QString("Solo") : QString("Lead sheet");
        sp.chapterKind = b.horn ? QString("SOLO") : QString("LEAD SHEET");
        sp.chapterTitle = "Melody and changes";
        if (sum.sectionStatus.find("lead-sheet") == sum.sectionStatus.end()) {
            sp.why = muse::qtrc("starscore", "no lead sheet");
        } else if (!sectionDone(song.legacy, sum, "lead-sheet")) {
            sp.why = muse::qtrc("starscore", "lead sheet %1").arg(notDone);
        } else {
            sp.ready = true;
        }
        out.push_back(sp);
    }

    for (const Seat& seat : b.seats) {
        SheetPlan sp;
        sp.sheet.kind = "chair";
        sp.sheet.sectionKey = QString::fromUtf8(seat.section);
        sp.sheet.chair = seat.chair;
        sp.sheet.transpose = true;
        sp.sheet.transposeDiatonic = seat.dia;
        sp.sheet.transposeChromatic = seat.chrom;
        sp.sheet.clef = seat.clef;
        sp.sheet.left = b.instrument;
        sp.sheet.right = seat.line;
        sp.label = seat.line;
        sp.chapterKind = seat.kind;
        sp.chapterTitle = seat.title;
        const QString name = sp.sheet.sectionKey == "2-horn-any" ? QString("2-Horn Flexible") : QString("3-Horn Flexible");
        sp.ready = arrangementReady(sp.sheet.sectionKey, name, &sp.why);
        out.push_back(sp);
    }

    if (!b.rhythmRole.isEmpty()) {
        SheetPlan sp;
        sp.sheet.kind = "rhythm";
        sp.sheet.role = b.rhythmRole;
        sp.sheet.left = b.instrument;
        sp.sheet.right = b.rhythmTitle;
        sp.label = b.rhythmTitle;
        sp.chapterKind = "PART";
        sp.chapterTitle = b.rhythmTitle;
        sp.optional = b.rhythmRole == "keys";   // keys may read the lead sheet
        if (sum.sectionStatus.find("rhythm") == sum.sectionStatus.end()) {
            sp.why = muse::qtrc("starscore", "no rhythm section");
        } else if (!sectionDone(song.legacy, sum, "rhythm")) {
            sp.why = muse::qtrc("starscore", "rhythm section %1").arg(notDone);
        } else {
            sp.ready = true;
        }
        out.push_back(sp);
    }
    return out;
}

void SongbookModel::recheck()
{
    const BookDef& b = bookDef(m_bookId);
    std::map<QString, StarScoreAuditFileSummary> cached;
    for (const StarScoreAuditFileSummary& s : starScore()->cachedLibraryAudit(m_library)) {
        cached[s.path] = s;
    }

    m_songs.clear();
    if (b.chart) {
        if (!m_songPath.isEmpty()) {
            Song s = songFor(QFileInfo(m_songPath).absoluteDir().dirName());
            s.path = m_songPath;
            m_songs.push_back(s);
        }
    } else {
        for (const QString& title : m_albums[m_album]) {
            m_songs.push_back(songFor(title));
        }
    }
    for (Song& s : m_songs) {
        auto it = cached.find(s.path);
        if (it != cached.end()) {
            s.summary = it->second;
            s.scanned = true;
        }
    }

    m_plan.clear();
    m_sheets.clear();
    m_ready = 0;
    int track = 0;
    for (const Song& s : m_songs) {
        ++track;
        QVariantList items;
        bool ready = true;
        std::vector<SheetPlan> sheets;
        if (s.path.isEmpty()) {
            ready = false;
            items << QVariantMap { { "label", muse::qtrc("starscore", "Song file") }, { "ready", false },
                                   { "why", muse::qtrc("starscore", "no .starscore for this song in the library") } };
        } else if (!s.scanned) {
            ready = false;
            items << QVariantMap { { "label", muse::qtrc("starscore", "Not read yet") }, { "ready", false },
                                   { "why", muse::qtrc("starscore", "press Check to read it") } };
        } else if (b.chart) {
            QString why;
            const StarScoreFileArrangement* found = nullptr;
            for (const StarScoreFileArrangement& a : s.summary.arrangementList) {
                if (a.templateKey == b.arrangement) {
                    found = &a;
                }
            }
            if (!found) {
                why = muse::qtrc("starscore", "no %1 in this song").arg(b.chartTitle);
            } else if (!arrangementDone(s.legacy, *found)) {
                why = s.legacy ? muse::qtrc("starscore", "not audited yet") : muse::qtrc("starscore", "not marked Finished");
            }
            ready = why.isEmpty();
            items << QVariantMap { { "label", b.chartTitle }, { "ready", ready }, { "why", why } };
        } else {
            sheets = sheetsFor(s);
            for (const SheetPlan& sp : sheets) {
                if (!sp.ready && !sp.optional) {
                    ready = false;
                }
                items << QVariantMap { { "label", sp.label }, { "ready", sp.ready }, { "why", sp.why }, { "optional", sp.optional } };
            }
        }
        m_sheets.push_back(sheets);
        m_ready += ready ? 1 : 0;
        m_plan << QVariantMap { { "song", s.title }, { "track", track }, { "path", s.path }, { "ready", ready },
                                { "items", items }, { "legacy", s.legacy } };
    }
    emit changed();
}

void SongbookModel::refresh()
{
    if (m_busy || m_refreshing) {
        return;
    }
    m_libraryFiles = starScore()->auditLibraryFiles(m_library);
    recheck();
    m_scanQueue.clear();
    for (const Song& s : m_songs) {
        if (!s.path.isEmpty() && !s.scanned) {
            m_scanQueue << s.path;
        }
    }
    if (m_scanQueue.isEmpty()) {
        return;
    }
    m_refreshing = true;
    const int total = m_scanQueue.size();
    auto scanOne = std::make_shared<std::function<void()> >();
    *scanOne = [this, total, scanOne]() {
        if (!m_refreshing) {
            return;
        }
        if (m_scanQueue.isEmpty()) {
            m_refreshing = false;
            m_status.clear();
            recheck();
            return;
        }
        m_status = muse::qtrc("starscore", "Reading %1 of %2…").arg(total - m_scanQueue.size() + 1).arg(total);
        emit changed();
        starScore()->auditFile(m_scanQueue.takeFirst(), false);
        QTimer::singleShot(10, this, [scanOne]() { (*scanOne)(); });
    };
    QTimer::singleShot(0, this, [scanOne]() { (*scanOne)(); });
}

void SongbookModel::cancel()
{
    m_refreshing = false;
    m_busy = false;
    m_status = muse::qtrc("starscore", "Stopped.");
    emit changed();
}

void SongbookModel::build()
{
    if (m_busy || m_refreshing || m_ready == 0) {
        return;
    }
    const BookDef& b = bookDef(m_bookId);
    m_busy = true;
    m_report.clear();
    m_lastOutput.clear();
    m_buildSongs.clear();
    m_rendered.clear();
    m_buildIndex = 0;
    m_workDir = QDir::tempPath() + "/StarScoreSongbook-" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(m_workDir);

    for (size_t i = 0; i < m_songs.size(); ++i) {
        if (m_plan[int(i)].toMap().value("ready").toBool()) {
            m_buildSongs.push_back(i);
        }
    }
    m_outFile = b.chart
                ? outputFolder() + "/Charts/" + asciiName(m_songs.front().title) + "/" + asciiName(m_songs.front().title + " - " + b.title)
                : outputFolder() + "/" + asciiName(m_album) + "/" + asciiName(m_album + " Songbook - " + b.instrument) + ".pdf";
    emit changed();
    QTimer::singleShot(0, this, [this]() { step(); });
}

void SongbookModel::step()
{
    if (!m_busy) {
        QDir(m_workDir).removeRecursively();
        return;
    }
    const BookDef& b = bookDef(m_bookId);
    if (m_buildIndex >= m_buildSongs.size()) {
        finishBuild();
        return;
    }
    const Song& song = m_songs[m_buildSongs[m_buildIndex]];
    m_status = muse::qtrc("starscore", "Writing %1 (%2 of %3)…").arg(song.title).arg(m_buildIndex + 1).arg(m_buildSongs.size());
    emit changed();

    // let the status show before the (slow) rendering
    QTimer::singleShot(30, this, [this, &b]() {
        const size_t songIdx = m_buildSongs[m_buildIndex];
        const Song& song = m_songs[songIdx];
        std::vector<StarScoreSongbookSheet> sheets;
        if (b.chart) {
            deprecate(outputFolder(), m_outFile);
            const auto planned = starScore()->songbookChartSheets(song.path, b.arrangement, b.chartTitle, m_outFile);
            if (!planned.ret) {
                m_report += song.title + ": " + QString::fromStdString(planned.ret.toString()) + "\n";
            } else {
                sheets = planned.val;
            }
        } else {
            int k = 0;
            for (const SheetPlan& sp : m_sheets[songIdx]) {
                if (!sp.ready) {
                    continue;
                }
                StarScoreSongbookSheet s = sp.sheet;
                s.pdfPath = QString("%1/%2/%3.pdf").arg(m_workDir).arg(songIdx).arg(k++);
                sheets.push_back(s);
            }
        }
        if (!sheets.empty()) {
            starScore()->songbookRenderSheets(song.path, sheets);
        }
        m_rendered.push_back(sheets);
        ++m_buildIndex;
        QTimer::singleShot(10, this, [this]() { step(); });
    });
}

void SongbookModel::finishBuild()
{
    const BookDef& b = bookDef(m_bookId);
    QStringList problems;

    if (b.chart) {
        for (const auto& sheets : m_rendered) {
            for (const StarScoreSongbookSheet& s : sheets) {
                if (!s.error.isEmpty()) {
                    problems << QFileInfo(s.pdfPath).completeBaseName() + ": " + s.error;
                }
            }
        }
        m_lastOutput = m_outFile;
        m_status = problems.isEmpty() ? muse::qtrc("starscore", "Chart written.") : muse::qtrc("starscore", "Chart written, with problems:");
        m_report += problems.join("\n");
        m_busy = false;
        QDir(m_workDir).removeRecursively();
        emit changed();
        return;
    }

    // Songs whose required sheets rendered go in; the page numbers follow from the sheets' page counts
    songbook::Book book;
    book.album = m_album;
    book.instrument = b.instrument;
    book.keyLabel = b.keyLabel;
    book.horn = b.horn;
    book.linesSummary = b.linesSummary;
    for (const Seat& s : b.seats) {
        book.lines << s.line;
    }

    std::vector<std::vector<StarScoreSongbookSheet> > kept;
    std::vector<size_t> keptSongs;
    for (size_t i = 0; i < m_rendered.size(); ++i) {
        const Song& song = m_songs[m_buildSongs[i]];
        std::vector<StarScoreSongbookSheet> ok;
        bool failed = false;
        for (const StarScoreSongbookSheet& s : m_rendered[i]) {
            if (s.error.isEmpty() && s.pages > 0) {
                ok.push_back(s);
            } else if (s.kind == "rhythm" && s.role == "keys") {
                continue;   // keys read the lead sheet in this song
            } else {
                failed = true;
                problems << QString("%1 – %2: %3").arg(song.title, s.right, s.error.isEmpty() ? QString("no pages") : s.error);
            }
        }
        if (failed || ok.empty()) {
            book.notInThisEdition << song.title;
            continue;
        }
        kept.push_back(ok);
        keptSongs.push_back(m_buildSongs[i]);
    }
    for (size_t i = 0; i < m_songs.size(); ++i) {
        if (std::find(m_buildSongs.begin(), m_buildSongs.end(), i) == m_buildSongs.end()) {
            book.notInThisEdition << m_songs[i].title;
        }
    }

    if (kept.empty()) {
        m_status = muse::qtrc("starscore", "Nothing could be written.");
        m_report = problems.join("\n");
        m_busy = false;
        QDir(m_workDir).removeRecursively();
        emit changed();
        return;
    }

    int page = songbook::frontPageCount(book) + 1;
    for (size_t i = 0; i < kept.size(); ++i) {
        songbook::Chapter c;
        c.song = m_songs[keptSongs[i]].title;
        c.track = int(keptSongs[i]) + 1;
        c.notes = notes(c.song);
        c.openerPage = page++;
        for (const StarScoreSongbookSheet& s : kept[i]) {
            songbook::ChapterSheet cs;
            // "Trio · Middle line" -> TRIO / Middle line
            const QStringList parts = s.right.split(QString::fromUtf8(" · "));
            cs.kind = parts.size() == 2 ? parts[0].toUpper() : (s.kind == "lead" ? QString("LEAD SHEET") : QString("PART"));
            cs.title = parts.size() == 2 ? parts[1] : s.right;
            if (s.kind == "lead" && b.horn) {
                cs.kind = "SOLO";
                cs.title = "Melody and changes";
            }
            cs.page = page;
            page += s.pages;
            c.sheets.push_back(cs);
        }
        book.chapters.push_back(c);
    }

    std::vector<QString> inputs;
    std::vector<bool> numbered;
    const QString front = m_workDir + "/front.pdf";
    songbook::writeFrontPages(book, front);
    inputs.push_back(front);
    numbered.push_back(false);
    for (size_t i = 0; i < kept.size(); ++i) {
        const QString opener = QString("%1/opener-%2.pdf").arg(m_workDir).arg(i);
        songbook::writeOpener(book, book.chapters[i], opener);
        inputs.push_back(opener);
        numbered.push_back(false);
        for (const StarScoreSongbookSheet& s : kept[i]) {
            inputs.push_back(s.pdfPath);
            numbered.push_back(true);
        }
    }

    QDir().mkpath(QFileInfo(m_outFile).absolutePath());
    deprecate(outputFolder(), m_outFile);
    const bool ok = starscore::mergePdfs(inputs, numbered, m_outFile, m_album + " Songbook – " + b.instrument);
    QDir(m_workDir).removeRecursively();

    m_busy = false;
    if (!ok) {
        m_status = muse::qtrc("starscore", "Couldn't write the songbook PDF.");
    } else {
        m_lastOutput = m_outFile;
        m_status = muse::qtrc("starscore", "Songbook written: %n song(s), %1 pages.", nullptr, int(kept.size())).arg(page - 1);
    }
    QStringList lines;
    if (!book.notInThisEdition.isEmpty()) {
        lines << muse::qtrc("starscore", "Not in this edition: %1").arg(book.notInThisEdition.join(", "));
    }
    lines << problems;
    m_report = lines.join("\n");
    emit changed();
}
