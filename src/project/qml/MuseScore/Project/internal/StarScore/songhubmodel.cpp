/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Song page (see songhubmodel.h)
 */
#include "songhubmodel.h"

#include <algorithm>
#include <map>
#include <memory>
#include <set>

#include <QCoreApplication>
#include <QDateTime>
#include <QPointer>
#include <QThreadPool>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLocale>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include "dockwindow/idockwindow.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/masterscore.h"
#include "project/internal/starscore/starscorestatus.h"
#include "translation.h"

using namespace mu::project;

static const char* OPEN_HERE_SETTING = "StarScore/songPage/openHere";
static const char* PRACTICE_MODE_REQUEST = "StarScore/practice/requestMode";

SongHubModel::SongHubModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

bool SongHubModel::openSongsHere()
{
    return QSettings().value(OPEN_HERE_SETTING, true).toBool();
}

bool SongHubModel::openHere() const
{
    return openSongsHere();
}

void SongHubModel::setOpenHere(bool on)
{
    if (on != openSongsHere()) {
        QSettings().setValue(OPEN_HERE_SETTING, on);
        emit changed();
    }
}

QVariantList SongHubModel::statusColors() const
{
    return starscore::statusColours();
}

void SongHubModel::load()
{
    if (!m_loaded) {
        m_loaded = true;
        if (starScore()) {
            starScore()->changed().onNotify(this, [this]() { refresh(); });
        }
        if (globalContext()) {
            globalContext()->currentProjectChanged().onNotify(this, [this]() { refresh(); });
        }
    }
    refresh();

    // (the autotest: STARSCORE_AUTOTEST_HUBDO="arr:<name>" | "part:<name>" | "solo:<n>" | "ref:<n>" | "practice:<mode>" |
    // "todo" | "score", done 2 s after the page loads, to test where each goes)
    static bool done = false;
    const QString todo = qEnvironmentVariable("STARSCORE_AUTOTEST_HUBDO");
    if (!done && !todo.isEmpty()) {
        // (done when it runs: the page may be unloaded and loaded again before then, and this model with it)
        QTimer::singleShot(2000, this, [this, todo]() {
            if (done) {
                return;
            }
            done = true;
            const QString kind = todo.section(':', 0, 0);
            const QString arg = todo.section(':', 1);
            auto byName = [](const QVariantList& list, const QString& key, const QString& value) {
                for (const QVariant& v : list) {
                    if (v.toMap().value(key).toString() == value) {
                        return v.toMap();
                    }
                }
                return QVariantMap();
            };
            if (kind == "arr") {
                openArrangement(byName(m_arrangements, "name", arg).value("id").toString());
            } else if (kind == "part") {
                openPartScore(arg);
            } else if (kind == "solo" && arg.toInt() < m_solos.size()) {
                openSolo(m_solos.at(arg.toInt()).toMap().value("id").toString());
            } else if (kind == "ref" && arg.toInt() < m_references.size()) {
                openReference(m_references.at(arg.toInt()).toMap().value("id").toString());
            } else if (kind == "practice") {
                practice(arg);
            } else if (kind == "todo") {
                openPanel("todo");
            } else {
                openScore();
            }
        });
    }
}

namespace {
QString sizeText(qint64 bytes)
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString kindTitle(const QString& kind)
{
    if (kind == "full") {
        return muse::qtrc("starscore", "Full mix");
    } else if (kind == "piano") {
        return muse::qtrc("starscore", "Piano only");
    } else if (kind == "nopiano") {
        return muse::qtrc("starscore", "Everything but piano");
    } else if (kind == "video") {
        return muse::qtrc("starscore", "Video");
    } else if (kind == "transcribe") {
        return muse::qtrc("starscore", "Transcribe! file");
    }
    return muse::qtrc("starscore", "Recording");
}
}

//! A folder for the page: its name, how many PDFs (and other files) are in it and below it, and when the newest
//! changed
QVariantMap SongHubModel::folderEntry(const QString& name, const QString& path)
{
    int pdfs = 0;
    int files = 0;
    QDateTime newest;
    QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext() && files < 5000) {
        it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.fileName().startsWith('.')) {
            continue;
        }
        ++files;
        if (fi.suffix().compare("pdf", Qt::CaseInsensitive) == 0) {
            ++pdfs;
        }
        if (!newest.isValid() || fi.lastModified() > newest) {
            newest = fi.lastModified();
        }
    }
    QString detail;
    if (pdfs > 0) {
        detail = muse::qtrc("starscore", "%n PDF(s)", nullptr, pdfs);
        if (files > pdfs) {
            detail += ", " + muse::qtrc("starscore", "%n other file(s)", nullptr, files - pdfs);
        }
    } else {
        detail = files > 0 ? muse::qtrc("starscore", "%n file(s)", nullptr, files) : muse::qtrc("starscore", "empty");
    }
    if (newest.isValid()) {
        detail += " · " + QLocale().toString(newest.date(), QLocale::ShortFormat);
    }
    return QVariantMap { { "name", name }, { "path", path }, { "detail", detail }, { "pdfs", pdfs }, { "files", files } };
}

void SongHubModel::refresh()
{
    m_arrangements.clear();
    m_sections.clear();
    m_partScores.clear();
    m_solos.clear();
    m_soloKeys.clear();
    m_references.clear();
    m_media.clear();
    m_todo.clear();
    m_title.clear();
    m_facts.clear();
    m_filePath.clear();
    m_songFolder.clear();

    IStarScoreService* ss = starScore().get();
    const muse::io::path_t mainPath = ss ? ss->mainProjectPath() : muse::io::path_t();
    m_hasSong = ss && ss->hasScore() && !mainPath.empty();
    m_isStarScore = m_hasSong && ss->isStarScoreFile();
    m_isSoloFile = m_hasSong && ss->isSoloTranscription();
    if (!m_hasSong) {
        emit changed();
        return;
    }

    m_filePath = mainPath.toQString();
    if (m_filePath != m_foldersFor) {   // (another song: its folders aren't these)
        m_bandFolders.clear();
        m_localFolders.clear();
        m_foldersReady = false;
        m_foldersFor = m_filePath;
    }
    const QFileInfo file(m_filePath);

    // --- the title and the facts line
    mu::notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    mu::engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    m_title = ss->songTitle();
    if (m_title.isEmpty() || m_title == "Untitled score") {
        m_title = file.completeBaseName();
        const qsizetype dash = m_title.indexOf(" - ");
        if (dash == 4) {
            m_title = m_title.mid(7);   // "BETT - Bet" -> "Bet"
        }
    }
    QStringList facts;
    const QString code = ss->songCode();
    if (!code.isEmpty()) {
        facts << code;
    }
    if (m_isSoloFile) {
        facts << muse::qtrc("starscore", "Solo transcription");
    } else {
        const QString kind = ss->songKind();
        if (kind == "original") {
            facts << muse::qtrc("starscore", "Original");
        } else if (kind == "cover") {
            facts << muse::qtrc("starscore", "Cover");
        } else if (kind == "vocal") {
            facts << muse::qtrc("starscore", "Vocal cover");
        }
        const QString group = ss->songGroup();
        if (!group.isEmpty()) {
            facts << group.section('/', -1).section(' ', 1);   // "1 Starsign/1 Originals/2 In Progress" -> "In Progress"
        }
    }
    const QString version = ss->scoreVersion();
    if (!version.isEmpty() && version != "0.0.0") {
        facts << muse::qtrc("starscore", "Version %1").arg(version);
    }
    if (ms) {
        const QString composer = ms->metaTag(u"composer").toQString().simplified();
        if (!composer.isEmpty()) {
            facts << composer;
        }
    }
    facts << muse::qtrc("starscore", "saved %1").arg(QLocale().toString(file.lastModified(), QLocale::ShortFormat));
    m_facts = facts.join("  ·  ");

    // --- arrangements and sections
    const std::vector<StarScoreSection> sections = ss->sections();
    const QString active = ss->activeArrangementId();
    for (const StarScoreArrangement& a : ss->arrangements()) {
        const int status = int(ss->arrangementStatus(a.id));
        QStringList names;
        for (const QString& sid : a.sectionIds) {
            for (const StarScoreSection& s : sections) {
                if (s.id == sid) {
                    names << s.name;
                }
            }
        }
        m_arrangements << QVariantMap {
            { "id", a.id }, { "name", a.name }, { "status", status }, { "statusName", starscore::statusName(status) },
            { "color", QString(starscore::statusColour(status)) }, { "active", a.id == active },
            { "sections", names.join(", ") }, { "scoreName", a.scoreName },
        };
    }
    for (const StarScoreSection& s : sections) {
        const int status = int(s.status);
        m_sections << QVariantMap {
            { "id", s.id }, { "name", s.name }, { "status", status }, { "statusName", starscore::statusName(status) },
            { "color", QString(starscore::statusColour(status)) }, { "on", s.on },
        };
    }

    // --- part scores (not the arrangements' own scores: those are on the arrangements)
    std::set<QString> arrangementScores;
    for (const StarScoreArrangement& a : ss->arrangements()) {
        if (!a.scoreName.isEmpty()) {
            arrangementScores.insert(a.scoreName);
        }
    }
    if (master) {
        for (const mu::notation::IExcerptNotationPtr& e : master->excerpts()) {
            if (!e) {
                continue;
            }
            const QString name = e->name();
            int status = -1;
            if (ms) {
                // (from the engraving excerpt: a part score not opened yet has no notation of its own)
                for (const mu::engraving::Excerpt* ex : ms->excerpts()) {
                    if (ex && ex->name().toQString() == name && ex->excerptScore()) {
                        status = ss->partScoreStatus(ex->excerptScore());
                        break;
                    }
                }
            }
            m_partScores << QVariantMap {
                { "name", name }, { "status", status },
                { "statusName", status >= 0 ? starscore::statusName(status) : QString() },
                { "color", status >= 0 ? QString(starscore::statusColour(status)) : QString() },
                { "arrangementScore", arrangementScores.count(name) > 0 }, { "visible", e->hasVisibleParts() },
            };
        }
    }

    // --- solos: a song's in-file solo transcriptions; a solo transcription's keys and how they're learned
    for (const StarScoreSolo& s : ss->solos()) {
        m_solos << QVariantMap { { "id", s.id }, { "name", s.name },
                                 { "detail", muse::qtrc("starscore", "bars %1–%2").arg(s.startBar).arg(s.endBar) } };
    }
    if (m_isSoloFile) {
        const QString canon = QFileInfo(m_filePath).canonicalFilePath();
        for (const QJsonValue& v : ss->soloLearningList()) {
            const QJsonObject e = v.toObject();
            if (QFileInfo(e.value("path").toString()).canonicalFilePath() != canon) {
                continue;
            }
            for (const QJsonValue& k : e.value("keys").toArray()) {
                const QJsonObject key = k.toObject();
                m_soloKeys << QVariantMap { { "name", key.value("name").toString() }, { "short", key.value("short").toString() },
                                            { "state", key.value("state").toString() } };
            }
        }
    }

    // --- stored in the file
    for (const StarScoreReference& r : ss->references()) {
        m_references << QVariantMap { { "id", r.id }, { "name", r.name }, { "instrument", r.instrument } };
    }
    for (const StarScoreMedia& m : ss->media()) {
        m_media << QVariantMap { { "id", m.id }, { "name", m.name }, { "kind", m.kind }, { "kindTitle", kindTitle(m.kind) },
                                 { "fileName", m.fileName }, { "size", sizeText(m.bytes) } };
    }

    // --- the song's folder in Sheets and Demos: its folders, and the sheet folders inside Sheet Music
    m_songFolder = ss->bandSongFolderPath();

    // --- the folders (in Sheets and Demos, and beside the file) are read on another thread: Sheets and Demos is on
    // Google Drive, where reading every file in Version History can take a while. They fill in when ready.
    const QString songFolder = m_songFolder;
    const QString fileFolder = file.absolutePath();
    const int generation = ++m_generation;
    QPointer<SongHubModel> self(this);
    QThreadPool::globalInstance()->start([self, songFolder, fileFolder, generation]() {
        QVariantList band;
        if (!songFolder.isEmpty()) {
            for (const QFileInfo& d : QDir(songFolder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                if (d.fileName().startsWith('.')) {
                    continue;
                }
                if (d.fileName().startsWith("Sheet Music")) {
                    // each sheet folder on its own line ("1 Lead Sheet", "3H Tpt Alt Ten", …)
                    for (const QFileInfo& sub : QDir(d.filePath()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                        if (!sub.fileName().startsWith('.')) {
                            QVariantMap entry = folderEntry(sub.fileName(), sub.filePath());
                            entry["group"] = d.fileName();
                            band << entry;
                        }
                    }
                    continue;
                }
                QVariantMap entry = folderEntry(d.fileName(), d.filePath());
                entry["group"] = QString();
                band << entry;
            }
        }
        // beside the file in Projects and Sheets (Version History, a solo's Sheets, MuseScore Files, …)
        QVariantList local;
        for (const QFileInfo& d : QDir(fileFolder).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            if (!d.fileName().startsWith('.')) {
                local << folderEntry(d.fileName(), d.filePath());
            }
        }
        QMetaObject::invokeMethod(qApp, [self, band, local, generation]() {
            if (self && self->m_generation == generation) {
                self->m_bandFolders = band;
                self->m_localFolders = local;
                self->m_foldersReady = true;
                emit self->changed();
            }
        }, Qt::QueuedConnection);
    });

    // --- what's left to do
    if (!m_isSoloFile) {
        for (const StarScoreTodoItem& t : ss->todoList()) {
            if (t.status == int(StarScoreStatus::Finished)) {
                continue;
            }
            m_todo << QVariantMap {
                { "title", t.title }, { "status", t.status },
                { "statusName", t.status >= 0 ? starscore::statusName(t.status) : muse::qtrc("starscore", "Not started") },
                { "color", t.status >= 0 ? QString(starscore::statusColour(t.status)) : QString("#8A8A8A") },
                { "details", t.details.join("; ") }, { "note", t.note },
            };
        }
    }

    emit changed();
}

void SongHubModel::toScore()
{
    if (interactive()) {
        interactive()->open(muse::Uri("musescore://notation"));
    }
}

void SongHubModel::openScore()
{
    if (starScore() && !starScore()->currentSoloId().isEmpty()) {
        starScore()->showMainScore();
    }
    mu::notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (master) {
        globalContext()->setCurrentNotation(master->notation());
    }
    toScore();
}

void SongHubModel::openArrangement(const QString& arrangementId)
{
    if (starScore() && !starScore()->currentSoloId().isEmpty()) {
        starScore()->showMainScore();
    }
    mu::notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (master) {
        globalContext()->setCurrentNotation(master->notation());
    }
    if (starScore()) {
        starScore()->showArrangement(arrangementId);
    }
    toScore();
}

void SongHubModel::openPartScore(const QString& name)
{
    if (starScore() && !starScore()->currentSoloId().isEmpty()) {
        starScore()->showMainScore();
    }
    mu::notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master) {
        return;
    }
    for (const mu::notation::IExcerptNotationPtr& e : master->excerpts()) {
        if (e && e->name() == name) {
            mu::notation::INotationPtr n = e->notation();
            if (n) {
                master->setExcerptIsOpen(n, true);
                globalContext()->setCurrentNotation(n);
            }
            break;
        }
    }
    toScore();
}

void SongHubModel::openSolo(const QString& soloId)
{
    if (starScore()) {
        starScore()->showSolo(soloId);
    }
    toScore();
}

void SongHubModel::openPanel(const QString& which)
{
    static const std::map<QString, std::pair<const char*, const char*> > PANELS {
        { "todo", { "starscoreTodoPanel", "" } },
        { "reference", { "starscoreReferencePanel", "" } },
        { "practice", { "starscorePracticePanel", "" } },
    };
    const auto it = PANELS.find(which);
    toScore();
    if (it == PANELS.end()) {
        return;
    }
    const QString dock = QString::fromLatin1(it->second.first);
    // (the panel belongs to the score's page: opened, or its tab brought to the front, once that page is showing.
    // This page, and this model with it, is unloaded when the score's page shows, so the timer is on its own.)
    auto provider = dockWindowProvider();
    auto tries = std::make_shared<int>(0);
    auto timer = new QTimer();
    timer->setInterval(200);
    QObject::connect(timer, &QTimer::timeout, timer, [provider, dock, tries, timer]() {
        muse::dock::IDockWindow* window = provider ? provider->window() : nullptr;
        if (window && window->currentPageUri() == "musescore://notation") {
            window->setDockOpen(dock, true);
            timer->stop();
            timer->deleteLater();
        } else if (++*tries > 25) {
            timer->stop();
            timer->deleteLater();
        }
    });
    timer->start();
}

void SongHubModel::openReference(const QString& referenceId)
{
    if (!starScore()) {
        return;
    }
    starScore()->setCurrentReferenceId(referenceId);
    openPanel("reference");
}

void SongHubModel::openReferenceElsewhere(const QString& referenceId)
{
    if (starScore()) {
        openPath(starScore()->referencePath(referenceId).toQString());
    }
}

void SongHubModel::openMedia(const QString& mediaId)
{
    if (starScore()) {
        const muse::Ret ret = starScore()->openScoreAudio(m_filePath, mediaId);
        if (!ret && interactive()) {
            interactive()->error(muse::trc("starscore", "Couldn't open it"), ret.toString());
        }
    }
}

void SongHubModel::addMedia()
{
    if (starScore()) {
        const muse::RetVal<QString> ret = starScore()->addScoreAudio(m_filePath);
        if (!ret.ret && !ret.ret.text().empty() && interactive()) {
            interactive()->error(muse::trc("starscore", "Couldn't add it"), ret.ret.toString());
        }
        refresh();
    }
}

void SongHubModel::addReference()
{
    // as the StarScore bar's "+ add": PDFs picked, one identical to a PDF already in asks first
    if (!starScore() || !interactive()) {
        return;
    }
    const muse::io::paths_t paths = interactive()->selectOpeningFilesSync(
        muse::trc("starscore", "Add reference PDFs"), muse::io::path_t(QFileInfo(m_filePath).absolutePath()), { "PDF (*.pdf)" });
    for (const muse::io::path_t& p : paths) {
        const QString same = starScore()->identicalReferenceName(p);
        if (!same.isEmpty()) {
            constexpr int Add = static_cast<int>(muse::IInteractive::Button::CustomButton) + 1;
            constexpr int Skip = static_cast<int>(muse::IInteractive::Button::CustomButton) + 2;
            const muse::IInteractive::Result answer = interactive()->questionSync(
                muse::trc("starscore", "This PDF is already here"),
                muse::qtrc("starscore", "“%1” is identical to the reference PDF “%2” already in this score. Import it anyway?")
                .arg(QFileInfo(p.toQString()).fileName(), same).toStdString(), {
                muse::IInteractive::ButtonData(Skip, muse::trc("starscore", "Don't import"), true),
                muse::IInteractive::ButtonData(Add, muse::trc("starscore", "Import anyway")),
            }, Skip);
            if (answer.button() != Add) {
                continue;
            }
        }
        const muse::RetVal<QString> ret = starScore()->addReference(p);
        if (!ret.ret) {
            interactive()->error(muse::trc("starscore", "Couldn't add the reference PDF"), ret.ret.toString());
        }
    }
    refresh();
}

void SongHubModel::practice(const QString& mode)
{
    // the Practice panel picks this up when it shows (StarScorePracticePanel.qml)
    QSettings().setValue(PRACTICE_MODE_REQUEST, mode);
    openPanel("practice");
}

void SongHubModel::openPath(const QString& path)
{
    if (!path.isEmpty() && QFileInfo::exists(path)) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
}

void SongHubModel::showInFolder(const QString& path)
{
    if (path.isEmpty()) {
        return;
    }
    const QFileInfo fi(path);
    openPath(fi.isDir() ? fi.filePath() : fi.absolutePath());
}

QVariantList SongHubModel::filesIn(const QString& folder) const
{
    QVariantList out;
    if (folder.isEmpty()) {
        return out;
    }
    QDirIterator it(folder, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    std::vector<QFileInfo> found;
    while (it.hasNext() && found.size() < 300) {
        it.next();
        if (!it.fileInfo().fileName().startsWith('.')) {
            found.push_back(it.fileInfo());
        }
    }
    std::sort(found.begin(), found.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return a.filePath().compare(b.filePath(), Qt::CaseInsensitive) < 0;
    });
    const QDir base(folder);
    for (const QFileInfo& fi : found) {
        out << QVariantMap { { "name", base.relativeFilePath(fi.filePath()) }, { "path", fi.filePath() },
                             { "detail", QLocale().toString(fi.lastModified().date(), QLocale::ShortFormat) } };
    }
    return out;
}

void SongHubModel::dispatch(const QString& actionCode)
{
    dispatcher()->dispatch(actionCode.toStdString());
}
