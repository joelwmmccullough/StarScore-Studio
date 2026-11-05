/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Media inside the .starscore (1.26.0, Joel 5 Nov 2026): "a single .starscore file to contain all this info … on a song
 * by song basis". The stems he makes in Logic (full mix, piano only, everything but piano), the players' video and
 * Transcribe! files are imported into the song's .starscore, "same as the reference pdfs are … so everything is in one
 * place seamlessly". Video is stored at full size.
 *
 * - In the file: "StarScoreMedia/<id>/<file name>" for each file, stored uncompressed, and "StarScoreMedia/media.json",
 *   the list (id, name, kind, file name, size, checksum, a Transcribe! file's recording).
 * - While a song is open, each file has a working copy in the song's working folder (as the reference PDFs do). A save
 *   writes them back into the file, streamed (starscorezip.h): nothing is held in memory or packed again.
 * - A Transcribe! file opened from StarScore opens its working copy, pointed at the working copy of its recording; what
 *   Transcribe! saves there marks the song as changed, so the next save keeps it.
 * - 1.25.0 kept these files in an "Audio" folder beside the .starscore. Opening a song that has one offers to move them
 *   in; the folder's copies go to Version History once the song has been saved with them inside.
 * - A song that isn't open (Home's learning list) gets new files written into it at once, the file rewritten as a save
 *   does (its previous state goes to the Autosave Archive first).
 */
#include "starscoreservice.h"
#include "starscoreautoarchive.h"
#include "starscorezip.h"

#include <algorithm>

#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include "actions/actiontypes.h"
#include "log.h"

using namespace mu::project;
using namespace muse;

namespace {
const QString MEDIA_DIR("StarScoreMedia/");
const QString MEDIA_INDEX("StarScoreMedia/media.json");

const QStringList AUDIO_SUFFIXES { "mp3", "m4a", "aac", "wav", "aif", "aiff", "flac", "ogg", "opus", "caf" };
const QStringList VIDEO_SUFFIXES { "mp4", "mov", "m4v" };

//! the file types the Audio row takes (and the 1.25.0 Audio folder held)
QStringList mediaFilters()
{
    QStringList f;
    for (const QString& s : AUDIO_SUFFIXES + VIDEO_SUFFIXES + QStringList { "xsc" }) {
        f << "*." + s;
    }
    return f;
}

QString suffixOf(const QString& path)
{
    return QFileInfo(path).suffix().toLower();
}

bool isVideoFile(const QString& path)
{
    return VIDEO_SUFFIXES.contains(suffixOf(path));
}

bool isTranscribeFile(const QString& path)
{
    return suffixOf(path) == "xsc";
}

QString safeFileName(QString name)
{
    static const QRegularExpression unsafe("[/:\\\\\\x00-\\x1f]");
    name.replace(unsafe, "-");
    return name.trimmed().isEmpty() ? QString("file") : name.trimmed();
}

QJsonObject mediaJson(const StarScoreMedia& m)
{
    QJsonObject o { { "id", m.id }, { "name", m.name }, { "kind", m.kind }, { "fileName", m.fileName },
                    { "bytes", double(m.bytes) }, { "crc", double(m.crc) } };
    if (!m.soundId.isEmpty()) {
        o["soundId"] = m.soundId;
    }
    return o;
}

QByteArray mediaIndex(const std::vector<StarScoreMedia>& list)
{
    QJsonArray a;
    for (const StarScoreMedia& m : list) {
        a.append(mediaJson(m));
    }
    return QJsonDocument(QJsonObject { { "version", 1 }, { "media", a } }).toJson(QJsonDocument::Indented);
}

//! The media listed in a .starscore whose entries are really there
std::vector<StarScoreMedia> readMediaList(const starscore::zip::Reader& zip)
{
    std::vector<StarScoreMedia> out;
    const starscore::zip::Entry* index = zip.find(MEDIA_INDEX);
    if (!index) {
        return out;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(zip.read(*index));
    for (const QJsonValue& v : doc.object().value("media").toArray()) {
        const QJsonObject o = v.toObject();
        StarScoreMedia m;
        m.id = o.value("id").toString();
        m.name = o.value("name").toString();
        m.kind = o.value("kind").toString("audio");
        m.fileName = o.value("fileName").toString();
        m.bytes = qint64(o.value("bytes").toDouble());
        m.crc = quint32(o.value("crc").toDouble());
        m.soundId = o.value("soundId").toString();
        if (m.id.isEmpty() || m.fileName.isEmpty()) {
            continue;
        }
        if (!zip.find(m.entry())) {
            LOGW() << "[starscore] media listed but missing from the file: " << m.entry();
            continue;
        }
        out.push_back(m);
    }
    return out;
}

std::vector<StarScoreMedia> readMediaList(const QString& scorePath)
{
    const starscore::zip::Reader zip(scorePath);
    return zip.ok() ? readMediaList(zip) : std::vector<StarScoreMedia> {};
}

quint32 fileCrc(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return 0;
    }
    quint32 crc = 0;
    QByteArray buf(4 * 1024 * 1024, Qt::Uninitialized);
    for (qint64 n; (n = f.read(buf.data(), buf.size())) > 0;) {
        crc = starscore::zip::crc32(crc, buf.constData(), n);
    }
    return crc;
}

//! The recording a Transcribe! document is for: the first absolute path in it to a file with an audio or video
//! ending (Transcribe! keeps it as a full path; every line is looked at). existing: only paths that exist.
QString transcribeSoundPath(const QString& xscPath, bool existing)
{
    QFile f(xscPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return QString();
    }
    static const QRegularExpression pathRe("(/[^,\\r\\n\\t]+\\.(mp3|m4a|aac|wav|aif|aiff|flac|ogg|opus|caf|mp4|mov|m4v))",
                                           QRegularExpression::CaseInsensitiveOption);
    const QString text = QString::fromUtf8(f.read(4 * 1024 * 1024));
    QRegularExpressionMatchIterator it = pathRe.globalMatch(text);
    while (it.hasNext()) {
        const QString p = it.next().captured(1).trimmed();
        if (!existing || QFileInfo(p).isFile()) {
            return p;
        }
    }
    return QString();
}

//! Points a Transcribe! document at another copy of its recording (every place its old path is written)
bool pointTranscribeAt(const QString& xscPath, const QString& soundPath)
{
    const QString old = transcribeSoundPath(xscPath, false);
    if (old.isEmpty() || old == soundPath) {
        return !old.isEmpty();
    }
    QFile f(xscPath);
    if (!f.open(QIODevice::ReadOnly)) {
        return false;
    }
    QByteArray data = f.readAll();
    f.close();
    data.replace(old.toUtf8(), soundPath.toUtf8());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return f.write(data) == data.size();
}

QString uniqueName(const QString& base, const QStringList& taken)
{
    QString name = base;
    for (int n = 2; taken.contains(name, Qt::CaseInsensitive); ++n) {
        name = QString("%1 %2").arg(base).arg(n);
    }
    return name;
}

//! 1.25.0's Audio folder beside a score: its files
QFileInfoList audioFolderFiles(const QString& scorePath)
{
    if (scorePath.isEmpty()) {
        return {};
    }
    const QDir audio(QFileInfo(scorePath).absolutePath() + "/Audio");
    return audio.exists() ? audio.entryInfoList(mediaFilters(), QDir::Files, QDir::Name | QDir::IgnoreCase) : QFileInfoList {};
}

//! What a file in the 1.25.0 Audio folder is, from the name it was given there
std::pair<QString, QString> guessKind(const QFileInfo& f)
{
    const QString base = f.completeBaseName();
    if (isTranscribeFile(f.fileName())) {
        return { "transcribe", base };
    }
    if (isVideoFile(f.fileName())) {
        return { "video", "Video" };
    }
    if (base.startsWith("Original recording", Qt::CaseInsensitive)) {
        return { "full", "Full mix" };
    }
    return { "audio", base };
}

QString versionHistoryDir(const QString& scorePath, const QString& what)
{
    return QFileInfo(scorePath).absolutePath() + "/Version History/" + QDate::currentDate().toString(Qt::ISODate) + " " + what;
}

QString freeTarget(const QString& dir, const QString& fileName)
{
    const QFileInfo fi(fileName);
    QString target = dir + "/" + fileName;
    for (int i = 2; QFileInfo::exists(target); ++i) {
        target = QString("%1/%2 %3%4").arg(dir, fi.completeBaseName()).arg(i)
                 .arg(fi.suffix().isEmpty() ? QString() : "." + fi.suffix());
    }
    return target;
}

//! Replaces target with the file at tmp, the old one stepping aside first, so a song file is always on disk
Ret replaceFile(const QString& tmp, const QString& target)
{
    const QString old = target + ".media-old";
    QFile::remove(old);
    if (QFileInfo::exists(target) && !QFile::rename(target, old)) {
        QFile::remove(tmp);
        return make_ret(Ret::Code::UnknownError);
    }
    if (!QFile::rename(tmp, target)) {
        QFile::rename(old, target);
        QFile::remove(tmp);
        return make_ret(Ret::Code::UnknownError);
    }
    QFile::remove(old);
    return make_ok();
}
}

// ---------------------------------------------------------------------------------------------------------------------
//  The open song's media
// ---------------------------------------------------------------------------------------------------------------------

std::vector<StarScoreMedia> StarScoreService::media() const
{
    return m_media;
}

QString StarScoreService::mediaWorkPath(const StarScoreMedia& m) const
{
    return m_workDir.isEmpty() ? QString() : m_workDir + "/media/" + m.id + "/" + m.fileName;
}

QString StarScoreService::mediaPath(const QString& mediaId) const
{
    for (const StarScoreMedia& m : m_media) {
        if (m.id == mediaId) {
            return mediaWorkPath(m);
        }
    }
    return QString();
}

void StarScoreService::loadMedia()
{
    m_media.clear();
    m_mediaFolderMoves.clear();
    m_mediaOpenedOutside = false;
    if (m_mainProject && QFileInfo::exists(m_mainProject->path().toQString())) {
        m_media = readMediaList(m_mainProject->path().toQString());
    }
}

void StarScoreService::extractMedia()
{
    if (m_media.empty() || m_workDir.isEmpty() || !m_mainProject) {
        return;
    }
    const starscore::zip::Reader zip(m_mainProject->path().toQString());
    std::vector<StarScoreMedia> kept;
    for (const StarScoreMedia& m : m_media) {
        const starscore::zip::Entry* e = zip.find(m.entry());
        const QString work = mediaWorkPath(m);
        QDir().mkpath(QFileInfo(work).absolutePath());
        if (!e || !zip.copyTo(*e, work)) {
            LOGE() << "[starscore] couldn't unpack " << m.entry() << " from " << m_mainProject->path();
            continue;
        }
        kept.push_back(m);
    }
    m_media = kept;
    // Transcribe! files pointed at the working copies of their recordings
    for (const StarScoreMedia& m : m_media) {
        if (m.kind == "transcribe" && !m.soundId.isEmpty() && !mediaPath(m.soundId).isEmpty()) {
            pointTranscribeAt(mediaWorkPath(m), mediaPath(m.soundId));
        }
    }
    watchMedia();
}

void StarScoreService::watchMedia()
{
    auto* watcher = new QFileSystemWatcher();
    m_mediaWatcher.reset(watcher);
    for (const StarScoreMedia& m : m_media) {
        if (m.kind == "transcribe") {
            watcher->addPath(mediaWorkPath(m));
        }
    }
    QObject::connect(watcher, &QFileSystemWatcher::fileChanged, watcher, [this, watcher](const QString& path) {
        // (Transcribe! may save by replacing the file: watched again)
        if (QFileInfo::exists(path) && !watcher->files().contains(path)) {
            watcher->addPath(path);
        }
        if (m_mainProject) {
            LOGI() << "[starscore] changed outside StarScore: " << path.toStdString() << "; kept at the next save";
            m_mainProject->markAsUnsaved();
        }
    });
}

StarScoreMediaSource StarScoreService::askMediaKind(const QString& path, const std::vector<StarScoreMedia>& existing) const
{
    constexpr int Full = static_cast<int>(IInteractive::Button::CustomButton) + 1;
    constexpr int Piano = static_cast<int>(IInteractive::Button::CustomButton) + 2;
    constexpr int NoPiano = static_cast<int>(IInteractive::Button::CustomButton) + 3;
    constexpr int OwnName = static_cast<int>(IInteractive::Button::CustomButton) + 4;
    constexpr int Cancel = static_cast<int>(IInteractive::Button::CustomButton) + 5;
    const IInteractive::Result answer = interactive()->questionSync(
        muse::trc("starscore", "What is this recording?"),
        muse::qtrc("starscore", "“%1” is stored inside the score, under the name you pick.").arg(QFileInfo(path).fileName()).toStdString(),
        { IInteractive::ButtonData(Full, muse::trc("starscore", "Full mix")),
          IInteractive::ButtonData(Piano, muse::trc("starscore", "Piano only")),
          IInteractive::ButtonData(NoPiano, muse::trc("starscore", "Everything but piano")),
          IInteractive::ButtonData(OwnName, muse::trc("starscore", "Keep its file name")),
          IInteractive::ButtonData(Cancel, muse::trc("starscore", "Cancel")) }, Full);
    Q_UNUSED(existing);
    switch (answer.button()) {
    case Full: return { path, "full", muse::qtrc("starscore", "Full mix") };
    case Piano: return { path, "piano", muse::qtrc("starscore", "Piano") };
    case NoPiano: return { path, "nopiano", muse::qtrc("starscore", "No piano") };
    case OwnName: return { path, "audio", QFileInfo(path).completeBaseName() };
    default: return { path, QString(), QString() };
    }
}

std::vector<StarScoreMediaSource> StarScoreService::mediaSourcesFor(const QStringList& paths,
                                                                             const std::vector<StarScoreMedia>& existing,
                                                                             bool ask) const
{
    std::vector<MediaSource> out;
    QStringList fileNames;   // the recordings already there or coming, by file name
    for (const StarScoreMedia& m : existing) {
        fileNames << m.fileName;
    }
    for (const QString& p : paths) {
        fileNames << QFileInfo(p).fileName();
    }
    for (const QString& p : paths) {
        if (isTranscribeFile(p)) {
            out.push_back({ p, "transcribe", QFileInfo(p).completeBaseName() });
            // its recording comes along when it isn't there yet
            const QString sound = transcribeSoundPath(p, true);
            if (!sound.isEmpty() && !fileNames.contains(QFileInfo(sound).fileName())) {
                fileNames << QFileInfo(sound).fileName();
                if (isVideoFile(sound)) {
                    out.push_back({ sound, "video", muse::qtrc("starscore", "Video") });
                } else {
                    const MediaSource s = ask ? askMediaKind(sound, existing)
                                          : MediaSource { sound, "full", muse::qtrc("starscore", "Full mix") };
                    if (!s.kind.isEmpty()) {
                        out.push_back(s);
                    }
                }
            }
        } else if (isVideoFile(p)) {
            out.push_back({ p, "video", muse::qtrc("starscore", "Video") });
        } else {
            const MediaSource s = ask ? askMediaKind(p, existing) : MediaSource { p, "audio", QFileInfo(p).completeBaseName() };
            if (s.kind.isEmpty()) {
                return {};   // cancelled
            }
            out.push_back(s);
        }
    }
    return out;
}

//! The new media for the sources: ids, names and file names not taken, sizes and checksums. Files already inside
//! (same size and checksum) are left out and named in skipped.
static std::vector<StarScoreMedia> plannedMedia(const std::vector<StarScoreMediaSource>& sources,
                                                const std::vector<StarScoreMedia>& existing, QStringList& skipped,
                                                std::vector<QString>& sourcePaths)
{
    QStringList ids, names, fileNames;
    for (const StarScoreMedia& m : existing) {
        ids << m.id;
        names << m.name;
    }
    std::vector<StarScoreMedia> out;
    for (const StarScoreMediaSource& s : sources) {
        const QFileInfo fi(s.path);
        if (!fi.isFile()) {
            skipped << muse::qtrc("starscore", "%1 (not found)").arg(fi.fileName());
            continue;
        }
        StarScoreMedia m;
        m.bytes = fi.size();
        m.crc = fileCrc(s.path);
        const auto same = std::find_if(existing.begin(), existing.end(), [&](const StarScoreMedia& e) {
            return e.bytes == m.bytes && e.crc == m.crc;
        });
        if (same != existing.end()) {
            skipped << muse::qtrc("starscore", "%1 (already in the score as “%2”)").arg(fi.fileName(), same->name);
            continue;
        }
        int n = 1;
        while (ids.contains(QString("media%1").arg(n))) {
            ++n;
        }
        m.id = QString("media%1").arg(n);
        ids << m.id;
        m.kind = s.kind;
        m.name = uniqueName(s.name.trimmed().isEmpty() ? fi.completeBaseName() : s.name.trimmed(), names);
        names << m.name;
        m.fileName = safeFileName(fi.fileName());
        out.push_back(m);
        sourcePaths.push_back(s.path);
    }
    // a Transcribe! file linked to its recording: one of these, or one already inside, by file name
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i].kind != "transcribe") {
            continue;
        }
        const QString sound = QFileInfo(transcribeSoundPath(sourcePaths[i], false)).fileName();
        for (size_t k = 0; k < out.size(); ++k) {
            if (k != i && QFileInfo(sourcePaths[k]).fileName() == sound) {
                out[i].soundId = out[k].id;
            }
        }
        for (const StarScoreMedia& e : existing) {
            if (out[i].soundId.isEmpty() && e.fileName == safeFileName(sound)) {
                out[i].soundId = e.id;
            }
        }
    }
    return out;
}

static qint64 mediaBytes(const std::vector<StarScoreMedia>& list)
{
    qint64 n = 0;
    for (const StarScoreMedia& m : list) {
        n += m.bytes;
    }
    return n;
}

static Ret tooBig(qint64 bytes)
{
    return Ret(static_cast<int>(Ret::Code::UnknownError),
               muse::qtrc("starscore", "That would make the score %1 GB, and a .starscore can hold at most %2 GB. "
                                       "Use a smaller copy (a compressed audio file, or a video at a lower resolution).")
               .arg(double(bytes) / (1024.0 * 1024 * 1024), 0, 'f', 1)
               .arg(double(starscore::zip::MAX_FILE_BYTES) / (1024.0 * 1024 * 1024), 0, 'f', 1).toStdString());
}

RetVal<QStringList> StarScoreService::addMediaToOpenSong(const std::vector<MediaSource>& sources)
{
    if (!m_mainProject || m_workDir.isEmpty()) {
        return RetVal<QStringList>::make_ret(Ret::Code::InternalError);
    }
    QStringList skipped;
    std::vector<QString> from;
    std::vector<StarScoreMedia> added = plannedMedia(sources, m_media, skipped, from);
    const qint64 total = QFileInfo(m_mainProject->path().toQString()).size() + mediaBytes(added);
    if (total > starscore::zip::MAX_FILE_BYTES) {
        return RetVal<QStringList>(tooBig(total));
    }
    QStringList ids;
    for (size_t i = 0; i < added.size(); ++i) {
        StarScoreMedia& m = added[i];
        const QString work = mediaWorkPath(m);
        QDir().mkpath(QFileInfo(work).absolutePath());
        QFile::remove(work);
        if (!QFile::copy(from[i], work)) {
            return RetVal<QStringList>::make_ret(Ret::Code::UnknownError,
                                                 muse::qtrc("starscore", "Couldn't copy %1.").arg(from[i]).toStdString());
        }
        m_media.push_back(m);
        ids << m.id;
        LOGI() << "[starscore] stored in the song: " << m.entry().toStdString() << " (" << m.name.toStdString() << ", " << m.kind.toStdString() << ")";
    }
    for (const StarScoreMedia& m : added) {
        if (m.kind == "transcribe" && !m.soundId.isEmpty()) {
            pointTranscribeAt(mediaWorkPath(m), mediaPath(m.soundId));
        }
    }
    if (!added.empty()) {
        watchMedia();
        m_mainProject->markAsUnsaved();
        m_soloLearningChanged.notify();
        scheduleChanged();
    }
    if (!skipped.isEmpty() && !autotestRequested()) {
        interactive()->info(muse::trc("starscore", "Not added"), skipped.join("\n").toStdString());
    }
    return RetVal<QStringList>::make_ok(ids);
}

RetVal<QStringList> StarScoreService::addMediaToClosedFile(const QString& scorePath, const std::vector<MediaSource>& sources)
{
    std::vector<StarScoreMedia> list;
    QStringList skipped;
    std::vector<QString> from;
    std::vector<StarScoreMedia> added;
    const QString tmp = scorePath + ".media-tmp";
    {
        const starscore::zip::Reader zip(scorePath);
        if (!zip.ok()) {
            return RetVal<QStringList>::make_ret(Ret::Code::UnknownError,
                                                 muse::qtrc("starscore", "Couldn't read %1.").arg(scorePath).toStdString());
        }
        list = readMediaList(zip);
        added = plannedMedia(sources, list, skipped, from);
        if (added.empty()) {
            if (!skipped.isEmpty() && !autotestRequested()) {
                interactive()->info(muse::trc("starscore", "Not added"), skipped.join("\n").toStdString());
            }
            return RetVal<QStringList>::make_ok(QStringList());
        }
        const qint64 total = QFileInfo(scorePath).size() + mediaBytes(added);
        if (total > starscore::zip::MAX_FILE_BYTES) {
            return RetVal<QStringList>(tooBig(total));
        }
        // the file as it is, into the Autosave Archive first
        starscore::archiveBeforeSave(scorePath);

        starscore::zip::Writer out(tmp);
        for (const starscore::zip::Entry& e : zip.entries()) {
            if (e.name != MEDIA_INDEX) {
                out.addRaw(zip, e);
            }
        }
        for (size_t i = 0; i < added.size(); ++i) {
            out.addFile(added[i].entry(), from[i]);
            list.push_back(added[i]);
        }
        out.addData(MEDIA_INDEX, mediaIndex(list));
        if (!out.close()) {
            QFile::remove(tmp);
            return RetVal<QStringList>::make_ret(Ret::Code::UnknownError,
                                                 muse::qtrc("starscore", "Couldn't write %1.").arg(scorePath).toStdString());
        }
    }
    const Ret replaced = replaceFile(tmp, scorePath);
    if (!replaced) {
        return RetVal<QStringList>(replaced);
    }
    QStringList ids;
    for (const StarScoreMedia& m : added) {
        ids << m.id;
    }
    m_soloLearningChanged.notify();
    scheduleChanged();
    return RetVal<QStringList>::make_ok(ids);
}

// ---------------------------------------------------------------------------------------------------------------------
//  The Audio row (StarScore bar) and Home's learning list
// ---------------------------------------------------------------------------------------------------------------------

QJsonArray StarScoreService::scoreAudioFiles(const QString& scorePath) const
{
    QJsonArray out;
    if (scorePath.isEmpty() || !QFileInfo::exists(scorePath)) {
        return out;
    }
    const bool open = m_mainProject && m_mainProject->path().toQString() == scorePath;
    for (const StarScoreMedia& m : open ? m_media : readMediaList(scorePath)) {
        out.append(QJsonObject { { "id", m.id }, { "name", m.name }, { "kind", m.kind }, { "inside", true },
                                 { "path", open ? mediaWorkPath(m) : QString() }, { "scorePath", scorePath } });
    }
    for (const QFileInfo& f : audioFolderFiles(scorePath)) {
        out.append(QJsonObject { { "id", f.absoluteFilePath() }, { "name", f.completeBaseName() }, { "path", f.absoluteFilePath() },
                                 { "kind", isTranscribeFile(f.fileName()) ? "transcribe" : "audio" }, { "inside", false },
                                 { "scorePath", scorePath } });
    }
    return out;
}

RetVal<QString> StarScoreService::addScoreAudio(const QString& scorePath)
{
    if (scorePath.isEmpty() || !QFileInfo::exists(scorePath)) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "Save the score first."));
    }
    QStringList filters;
    for (const QString& f : mediaFilters()) {
        filters << f;
    }
    const io::paths_t picked = interactive()->selectOpeningFilesSync(
        muse::trc("starscore", "Choose recordings, a video or a Transcribe! file"), io::path_t(QDir::homePath() + "/Downloads"),
        { muse::trc("starscore", "Audio, video and Transcribe! files") + " (" + filters.join(' ').toStdString() + ")" });
    if (picked.empty()) {
        return RetVal<QString>::make_ok(QString());
    }
    QStringList paths;
    for (const io::path_t& p : picked) {
        paths << p.toQString();
    }
    const bool open = m_mainProject && m_mainProject->path().toQString() == scorePath;
    const std::vector<MediaSource> sources = mediaSourcesFor(paths, open ? m_media : readMediaList(scorePath), true);
    if (sources.empty()) {
        return RetVal<QString>::make_ok(QString());
    }
    const RetVal<QStringList> added = open ? addMediaToOpenSong(sources) : addMediaToClosedFile(scorePath, sources);
    if (!added.ret) {
        return RetVal<QString>(added.ret);
    }
    return RetVal<QString>::make_ok(added.val.isEmpty() ? QString() : added.val.first());
}

Ret StarScoreService::removeScoreAudio(const QString& idOrPath)
{
    // a file still in the 1.25.0 Audio folder: moved to Version History
    const QFileInfo f(idOrPath);
    if (f.isAbsolute() && f.exists()) {
        if (f.dir().dirName() != "Audio") {
            return make_ret(Ret::Code::UnknownError);
        }
        const IInteractive::Result answer = interactive()->questionSync(
            muse::trc("starscore", "Remove this file from the score?"),
            muse::qtrc("starscore", "“%1” goes into the Version History folder beside the score (it isn't deleted).").arg(f.fileName()).toStdString(),
            { IInteractive::Button::Cancel, IInteractive::Button::Ok }, IInteractive::Button::Ok);
        if (answer.standardButton() != IInteractive::Button::Ok) {
            return make_ok();
        }
        QDir scoreDir = f.dir();
        scoreDir.cdUp();
        const QString dir = scoreDir.absolutePath() + "/Version History/" + QDate::currentDate().toString(Qt::ISODate) + " Removed audio";
        QDir().mkpath(dir);
        if (!QFile::rename(f.absoluteFilePath(), freeTarget(dir, f.fileName()))) {
            return make_ret(Ret::Code::UnknownError, muse::qtrc("starscore", "Couldn't move it to %1.").arg(dir).toStdString());
        }
        m_soloLearningChanged.notify();
        scheduleChanged();
        return make_ok();
    }

    auto it = std::find_if(m_media.begin(), m_media.end(), [&](const StarScoreMedia& m) { return m.id == idOrPath; });
    if (it == m_media.end() || !m_mainProject) {
        return make_ret(Ret::Code::UnknownError);
    }
    const QString scorePath = m_mainProject->path().toQString();
    if (!autotestRequested()) {
        const IInteractive::Result answer = interactive()->questionSync(
            muse::trc("starscore", "Remove this from the score?"),
            muse::qtrc("starscore", "“%1” is taken out of the score when you next save it. A copy goes into the Version History "
                                    "folder beside the score.").arg(it->name).toStdString(),
            { IInteractive::Button::Cancel, IInteractive::Button::Ok }, IInteractive::Button::Ok);
        if (answer.standardButton() != IInteractive::Button::Ok) {
            return make_ok();
        }
    }
    const QString dir = versionHistoryDir(scorePath, "Removed audio");
    QDir().mkpath(dir);
    if (!QFile::copy(mediaWorkPath(*it), freeTarget(dir, it->fileName))) {
        return make_ret(Ret::Code::UnknownError, muse::qtrc("starscore", "Couldn't copy it to %1.").arg(dir).toStdString());
    }
    const QString id = it->id;
    m_media.erase(it);
    for (StarScoreMedia& m : m_media) {
        if (m.soundId == id) {
            m.soundId.clear();
        }
    }
    m_mainProject->markAsUnsaved();
    m_soloLearningChanged.notify();
    scheduleChanged();
    return make_ok();
}

Ret StarScoreService::relabelScoreAudio(const QString& mediaId)
{
    auto it = std::find_if(m_media.begin(), m_media.end(), [&](const StarScoreMedia& m) { return m.id == mediaId; });
    if (it == m_media.end() || !m_mainProject) {
        return make_ret(Ret::Code::UnknownError);
    }
    std::vector<StarScoreMedia> others;
    QStringList names;
    for (const StarScoreMedia& m : m_media) {
        if (m.id != mediaId) {
            others.push_back(m);
            names << m.name;
        }
    }
    const MediaSource s = askMediaKind(mediaWorkPath(*it), others);
    if (s.kind.isEmpty()) {
        return make_ok();
    }
    it->kind = s.kind;
    it->name = uniqueName(s.kind == "audio" ? QFileInfo(it->fileName).completeBaseName() : s.name, names);
    m_mainProject->markAsUnsaved();
    m_soloLearningChanged.notify();
    scheduleChanged();
    return make_ok();
}

Ret StarScoreService::saveScoreAudioCopy(const QString& mediaId)
{
    const auto it = std::find_if(m_media.begin(), m_media.end(), [&](const StarScoreMedia& m) { return m.id == mediaId; });
    if (it == m_media.end() || !m_mainProject) {
        return make_ret(Ret::Code::UnknownError);
    }
    const QFileInfo fi(it->fileName);
    const io::path_t target = interactive()->selectSavingFileSync(
        muse::trc("starscore", "Save a copy"), io::path_t(QDir::homePath() + "/Downloads/" + it->fileName),
        { "(*." + fi.suffix().toStdString() + ")" });
    if (target.empty()) {
        return make_ok();
    }
    QFile::remove(target.toQString());
    if (!QFile::copy(mediaWorkPath(*it), target.toQString())) {
        return make_ret(Ret::Code::UnknownError, muse::qtrc("starscore", "Couldn't write %1.").arg(target.toQString()).toStdString());
    }
    return make_ok();
}

Ret StarScoreService::openScoreAudio(const QString& scorePath, const QString& idOrPath)
{
    const QFileInfo f(idOrPath);
    if (f.isAbsolute() && f.exists()) {
        return interactive()->openUrl(QUrl::fromLocalFile(f.absoluteFilePath()));
    }
    const bool open = m_mainProject && (scorePath.isEmpty() || m_mainProject->path().toQString() == scorePath);
    if (open) {
        for (const StarScoreMedia& m : m_media) {
            if (m.id == idOrPath) {
                if (m.kind == "transcribe") {
                    m_mediaOpenedOutside = true;
                }
                return interactive()->openUrl(QUrl::fromLocalFile(mediaWorkPath(m)));
            }
        }
        return make_ret(Ret::Code::UnknownError);
    }

    // a song that isn't open
    const starscore::zip::Reader zip(scorePath);
    const std::vector<StarScoreMedia> list = readMediaList(zip);
    const auto it = std::find_if(list.begin(), list.end(), [&](const StarScoreMedia& m) { return m.id == idOrPath; });
    if (it == list.end()) {
        return make_ret(Ret::Code::UnknownError);
    }
    if (it->kind == "transcribe") {
        // the song is opened first, so what's changed in Transcribe! goes into it when it's saved
        dispatcher()->dispatch("file-open", muse::actions::ActionData::make_arg1<QUrl>(QUrl::fromLocalFile(scorePath)));
        const QString id = it->id;
        QTimer::singleShot(1500, &m_timerGuard, [this, scorePath, id]() {
            if (m_mainProject && m_mainProject->path().toQString() == scorePath) {
                openScoreAudio(scorePath, id);
            }
        });
        return make_ok();
    }
    const QString dir = QDir::tempPath() + "/StarScore Media/" + QFileInfo(scorePath).completeBaseName() + "/" + it->id;
    QDir().mkpath(dir);
    const QString target = dir + "/" + it->fileName;
    const starscore::zip::Entry* e = zip.find(it->entry());
    if (!QFileInfo::exists(target) || QFileInfo(target).size() != it->bytes) {
        if (!e || !zip.copyTo(*e, target)) {
            return make_ret(Ret::Code::UnknownError, muse::qtrc("starscore", "Couldn't unpack %1.").arg(it->name).toStdString());
        }
    }
    return interactive()->openUrl(QUrl::fromLocalFile(target));
}

// ---------------------------------------------------------------------------------------------------------------------
//  1.25.0's Audio folder: moved in
// ---------------------------------------------------------------------------------------------------------------------

void StarScoreService::offerAudioFolderMoveIn()
{
    if (!m_mainProject || m_workDir.isEmpty() || autotestRequested()) {
        return;
    }
    const QString scorePath = m_mainProject->path().toQString();
    const QFileInfoList files = audioFolderFiles(scorePath);
    if (files.isEmpty()) {
        return;
    }
    const QString key = "StarScore/mediaKeepFolder/" + load().fileId;
    if (QSettings().value(key, false).toBool()) {
        return;
    }
    QStringList names;
    for (const QFileInfo& f : files) {
        names << "• " + f.fileName();
    }
    constexpr int Move = static_cast<int>(IInteractive::Button::CustomButton) + 1;
    constexpr int Later = static_cast<int>(IInteractive::Button::CustomButton) + 2;
    constexpr int Keep = static_cast<int>(IInteractive::Button::CustomButton) + 3;
    const IInteractive::Result answer = interactive()->questionSync(
        muse::trc("starscore", "Move the Audio folder into the score?"),
        (muse::qtrc("starscore", "Recordings and Transcribe! files are now kept inside the .starscore. This score's Audio folder "
                                 "has:") + "\n\n" + names.join("\n") + "\n\n"
         + muse::qtrc("starscore", "Move them in? Once the score is saved with them inside, the folder's copies go to Version History "
                                   "(nothing is deleted).")).toStdString(),
        { IInteractive::ButtonData(Keep, muse::trc("starscore", "Keep the folder")),
          IInteractive::ButtonData(Later, muse::trc("starscore", "Not now")),
          IInteractive::ButtonData(Move, muse::trc("starscore", "Move them in"), true) }, Move);
    if (answer.button() == Keep) {
        QSettings().setValue(key, true);
        return;
    }
    if (answer.button() != Move) {
        return;
    }
    std::vector<MediaSource> sources;
    for (const QFileInfo& f : files) {
        const auto [kind, name] = guessKind(f);
        sources.push_back({ f.absoluteFilePath(), kind, name });
    }
    const RetVal<QStringList> added = addMediaToOpenSong(sources);
    if (!added.ret) {
        interactive()->error(muse::trc("starscore", "Couldn't move them in"),
                             added.ret.text().empty() ? added.ret.toString() : added.ret.text());
        return;
    }
    for (const QFileInfo& f : files) {
        m_mediaFolderMoves << f.absoluteFilePath();
    }
}

void StarScoreService::finishAudioFolderMoveIn()
{
    if (m_mediaFolderMoves.isEmpty() || !m_mainProject) {
        return;
    }
    const QString dir = versionHistoryDir(m_mainProject->path().toQString(), "Moved into the .starscore");
    QDir().mkpath(dir);
    QString audioDir;
    for (const QString& p : m_mediaFolderMoves) {
        if (!QFileInfo::exists(p)) {
            continue;
        }
        audioDir = QFileInfo(p).absolutePath();
        if (!QFile::rename(p, freeTarget(dir, QFileInfo(p).fileName()))) {
            LOGW() << "[starscore] couldn't move " << p.toStdString() << " to Version History";
        }
    }
    m_mediaFolderMoves.clear();
    if (!audioDir.isEmpty() && QDir(audioDir).isEmpty(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden)) {
        QDir().rmdir(audioDir);
    }
    m_soloLearningChanged.notify();
    scheduleChanged();
}

// ---------------------------------------------------------------------------------------------------------------------
//  Saving: the song file rewritten with its solos, reference PDFs and media
// ---------------------------------------------------------------------------------------------------------------------

static const QString STARSCORE_SOLOS_PREFIX("StarScoreSolos/");
static const QString STARSCORE_REFS_PREFIX("StarScoreReferences/");

Ret StarScoreService::injectSolos(const io::path_t& starscorePath)
{
    // Save changed solos to their working files first
    for (auto& [id, project] : m_soloProjects) {
        if (project->needSave().val) {
            Ret ret = project->save(soloWorkPath(id), SaveMode::Save, false);
            if (!ret) {
                return ret;
            }
        }
    }

    const QString target = starscorePath.toQString();
    const QString tmp = target + ".solos-tmp";
    {
        const starscore::zip::Reader reader(target);
        if (!reader.ok()) {
            return make_ret(Ret::Code::UnknownError);
        }
        qint64 total = QFileInfo(target).size();
        for (StarScoreMedia& m : m_media) {
            m.bytes = QFileInfo(mediaWorkPath(m)).size();
            total += m.bytes;
        }
        if (total > starscore::zip::MAX_FILE_BYTES) {
            return tooBig(total);
        }
        starscore::zip::Writer writer(tmp);
        for (const starscore::zip::Entry& e : reader.entries()) {
            if (e.isDir() || e.name.startsWith(STARSCORE_SOLOS_PREFIX) || e.name.startsWith(STARSCORE_REFS_PREFIX)
                || e.name.startsWith(MEDIA_DIR)) {
                continue;
            }
            // copied as MuseScore wrote it; an entry that can't be read leaves the file exactly as saved
            if (!writer.addRaw(reader, e)) {
                LOGE() << "[starscore] could not copy " << e.name << " from " << starscorePath << "; the file is left as saved";
                writer.close();
                QFile::remove(tmp);
                return make_ret(Ret::Code::UnknownError);
            }
        }
        for (const StarScoreSolo& solo : solos()) {
            if (!QFileInfo::exists(soloWorkPath(solo.id).toQString())) {
                LOGW() << "[starscore] missing working copy of solo " << solo.id;
                continue;
            }
            writer.addFile(solo.file, soloWorkPath(solo.id).toQString());
        }
        for (const StarScoreReference& ref : references()) {
            if (!QFileInfo::exists(referencePath(ref.id).toQString())) {
                LOGW() << "[starscore] missing working copy of reference " << ref.id;
                continue;
            }
            writer.addFile(ref.file, referencePath(ref.id).toQString());
        }
        std::vector<StarScoreMedia> written;
        for (StarScoreMedia& m : m_media) {
            if (!QFileInfo::exists(mediaWorkPath(m))) {
                LOGE() << "[starscore] missing working copy of " << m.entry();
                continue;
            }
            if (writer.addFile(m.entry(), mediaWorkPath(m))) {
                m.crc = writer.lastCrc();
            }
            written.push_back(m);
        }
        if (!written.empty()) {
            writer.addData(MEDIA_INDEX, mediaIndex(written));
        }
        if (!writer.close()) {
            QFile::remove(tmp);
            return make_ret(Ret::Code::UnknownError);
        }
    }

    // The song file is never gone from disk: the saved file steps aside under a temporary name, the new one takes its
    // place, and only then is the old one removed. (Removing the target first and then renaming left no song file at
    // all if the rename failed.)
    const QString old = target + ".solos-old";
    QFile::remove(old);
    if (!QFile::rename(target, old)) {
        QFile::remove(tmp);
        return make_ret(Ret::Code::UnknownError);
    }
    if (!QFile::rename(tmp, target)) {
        QFile::rename(old, target);   // the saved file back where it was
        QFile::remove(tmp);
        return make_ret(Ret::Code::UnknownError);
    }
    QFile::remove(old);
    return make_ok();
}

// ---------------------------------------------------------------------------------------------------------------------
//  Autotest
// ---------------------------------------------------------------------------------------------------------------------

static void mediaTestLog(const QString& line)
{
    QFile f(qEnvironmentVariable("STARSCORE_AUTOTEST") + "/log.txt");
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write((line + "\n").toUtf8());
    }
}

bool StarScoreService::autotestMediaStep(const QString& step)
{
    //    mediaadd:<file>|<kind>[|<name>]   a file stored inside the open song (kind full, piano, nopiano, video, transcribe, audio)
    //    mediaaddclosed:<score>|<file>|<kind>   the same, written into a .starscore that isn't open
    //    medialist[:<score>]               the open song's (or that file's) media and Audio-folder files
    //    mediaremove:<name>                taken out of the open song (a copy to Version History)
    //    mediamovein                       the Audio folder beside the open song moved in (as the offer's "Move them in")
    //    mediafinish                       what a successful save does after (the Audio folder's copies to Version History)
    //    mediacheck:<name>|<file>          the working copy is byte for byte the file
    //    mediaxsc:<name>                   the Transcribe! file's recording path
    //    mediatouch:<name>                 the working copy changed outside StarScore (as Transcribe! saving it)
    //    mediazip:<score>                  the file's entries: name, method, size
    //    mediaunsaved                      whether the open song needs saving
    const QString scorePath = m_mainProject ? m_mainProject->path().toQString() : QString();
    auto byName = [this](const QString& name) -> const StarScoreMedia* {
        for (const StarScoreMedia& m : m_media) {
            if (m.name == name) {
                return &m;
            }
        }
        return nullptr;
    };
    if (step.startsWith("mediaadd:")) {
        const QStringList a = step.mid(9).split('|');
        const QString kind = a.value(1, "audio");
        QString name = a.value(2);
        if (name.isEmpty()) {
            name = kind == "full" ? "Full mix" : kind == "piano" ? "Piano" : kind == "nopiano" ? "No piano"
                   : kind == "video" ? "Video" : QFileInfo(a.value(0)).completeBaseName();
        }
        std::vector<MediaSource> sources;
        if (kind == "transcribe") {
            sources = mediaSourcesFor({ a.value(0) }, m_media, false);
        } else {
            sources.push_back({ a.value(0), kind, name });
        }
        const RetVal<QStringList> r = addMediaToOpenSong(sources);
        mediaTestLog("  " + (r.ret ? "added " + r.val.join(", ") : QString::fromStdString(r.ret.toString())));
        return true;
    }
    if (step.startsWith("mediaaddclosed:")) {
        const QStringList a = step.mid(15).split('|');
        std::vector<MediaSource> sources { { a.value(1), a.value(2, "audio"), QFileInfo(a.value(1)).completeBaseName() } };
        const RetVal<QStringList> r = addMediaToClosedFile(a.value(0), sources);
        mediaTestLog("  " + (r.ret ? "added " + r.val.join(", ") : QString::fromStdString(r.ret.toString())));
        return true;
    }
    if (step == "medialist" || step.startsWith("medialist:")) {
        const QString path = step.startsWith("medialist:") ? step.mid(10) : scorePath;
        for (const QJsonValue& v : scoreAudioFiles(path)) {
            const QJsonObject o = v.toObject();
            mediaTestLog(QString("  %1 | %2 | %3 | %4").arg(o.value("inside").toBool() ? "inside" : "folder", o.value("kind").toString(),
                                                           o.value("name").toString(),
                                                           o.value("path").toString().isEmpty() ? QString("-")
                                                           : QString::number(QFileInfo(o.value("path").toString()).size())));
        }
        return true;
    }
    if (step.startsWith("mediaremove:")) {
        const StarScoreMedia* m = byName(step.mid(12));
        const Ret r = m ? removeScoreAudio(m->id) : make_ret(Ret::Code::UnknownError);
        mediaTestLog(QString("  removed: %1").arg(r ? "ok" : QString::fromStdString(r.toString())));
        return true;
    }
    if (step == "mediamovein") {
        std::vector<MediaSource> sources;
        const QFileInfoList files = audioFolderFiles(scorePath);
        for (const QFileInfo& f : files) {
            const auto [kind, name] = guessKind(f);
            sources.push_back({ f.absoluteFilePath(), kind, name });
        }
        const RetVal<QStringList> r = addMediaToOpenSong(sources);
        for (const QFileInfo& f : files) {
            m_mediaFolderMoves << f.absoluteFilePath();
        }
        mediaTestLog("  moved in: " + (r.ret ? r.val.join(", ") : QString::fromStdString(r.ret.toString())));
        return true;
    }
    if (step == "mediafinish") {
        finishAudioFolderMoveIn();
        mediaTestLog("  Audio folder: " + QString(QDir(QFileInfo(scorePath).absolutePath() + "/Audio").exists() ? "still there" : "gone"));
        return true;
    }
    if (step.startsWith("mediacheck:")) {
        const StarScoreMedia* m = byName(step.mid(11).section('|', 0, 0));
        QFile a(m ? mediaWorkPath(*m) : QString());
        QFile b(step.mid(11).section('|', 1));
        const bool same = a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly) && a.readAll() == b.readAll();
        mediaTestLog(QString("  %1: %2").arg(step.mid(11).section('|', 0, 0), same ? "identical" : "DIFFERENT"));
        return true;
    }
    if (step.startsWith("mediaxsc:")) {
        const StarScoreMedia* m = byName(step.mid(9));
        const QString sound = m ? transcribeSoundPath(mediaWorkPath(*m), false) : QString();
        mediaTestLog(QString("  points at: %1 (%2)").arg(sound.isEmpty() ? QString("-") : QFileInfo(sound).fileName(),
                                                         !sound.isEmpty() && sound.startsWith(m_workDir) ? "working copy" : "elsewhere"));
        return true;
    }
    if (step.startsWith("mediatouch:")) {
        const StarScoreMedia* m = byName(step.mid(11));
        QFile f(m ? mediaWorkPath(*m) : QString());
        if (f.open(QIODevice::Append)) {
            f.write("\r\n");
        }
        mediaTestLog("  touched");
        return true;
    }
    if (step.startsWith("mediazip:")) {
        const starscore::zip::Reader zip(step.mid(9));
        mediaTestLog(QString("  %1 entries, %2 bytes").arg(zip.entries().size()).arg(QFileInfo(step.mid(9)).size()));
        for (const starscore::zip::Entry& e : zip.entries()) {
            if (e.name.startsWith(MEDIA_DIR) || e.name.startsWith(STARSCORE_REFS_PREFIX) || e.name.startsWith(STARSCORE_SOLOS_PREFIX)) {
                mediaTestLog(QString("  %1 method %2 size %3").arg(e.name).arg(e.method).arg(e.size));
            }
        }
        return true;
    }
    if (step == "mediaunsaved") {
        mediaTestLog(QString("  needs saving: %1").arg(m_mainProject && m_mainProject->needSave().val ? "yes" : "no"));
        return true;
    }
    return false;
}
