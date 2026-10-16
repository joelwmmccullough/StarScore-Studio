/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Recordings window
 */
#include "recordingsmodel.h"

#include <QDate>
#include <QDateTime>
#include <QJsonDocument>
#include <QFileInfo>
#include <QJsonArray>
#include <QRegularExpression>

#include "project/internal/starscore/organizer/orgcore.h"
#include "project/internal/starscore/organizer/orghtml.h"
#include "project/internal/starscore/organizer/orgonline.h"
#include "project/internal/starscore/organizer/orgplatform.h"
#include "project/internal/starscore/organizer/orgrecordings.h"
#include "project/internal/starscore/organizer/orgstores.h"
#include "translation.h"

using namespace mu::project;
namespace org = mu::project::starscore::org;

RecordingsModel::RecordingsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QString RecordingsModel::recordingsFile() const
{
    const QString band = starScore()->bandFolder();
    return band.isEmpty() || !QFileInfo(band).isDir() ? QString() : band + "/6 Inbox/.organizer/recordings.json";
}

void RecordingsModel::load()
{
    m_code = starScore()->songCode();
    const QString file = recordingsFile();
    QString unreadable;
    m_all = QJsonObject();
    if (!file.isEmpty()) {
        // a file that is there but can't be read (broken JSON, cloud-only placeholder) must not be taken as empty:
        // save() would then write this song's copy over everyone else's recordings
        const org::JsonRead read = org::readJsonChecked(file);
        unreadable = org::unreadableMessage("recordings.json", read);
        m_all = read.doc.object();
    }
    const QJsonObject copy = starScore()->songRecordings();
    if (!m_code.isEmpty() && !copy.isEmpty()) {
        org::mergeSongRecordings(m_all, m_code, copy);    // edits made while Sheets and Demos wasn't there
    }
    if (m_code.isEmpty()) {
        m_status = muse::qtrc("starscore", "This song has no code yet: export it to Sheets and Demos first.");
    } else if (!unreadable.isEmpty()) {
        m_status = muse::qtrc("starscore", "%1 Changes are kept in this file only until it is fixed.").arg(unreadable);
    } else if (file.isEmpty()) {
        m_status = muse::qtrc("starscore", "Sheets and Demos wasn't found: changes are kept in this file and copied over later.");
    } else {
        m_status = muse::qtrc("starscore", "Stars order the live takes on the Recordings pages, best first. Changes show in the PDFs "
                                           "after the next folder organization.");
    }
    m_dirty = false;
    emit changed();
}

static QJsonObject showById(const QJsonObject& all, const QString& id)
{
    for (const QJsonValue& v : all.value("shows").toArray()) {
        if (v.toObject().value("id").toString() == id) {
            return v.toObject();
        }
    }
    return QJsonObject();
}

QVariantList RecordingsModel::performances() const
{
    QVariantList out;
    for (const QJsonValue& v : m_all.value("performances").toArray()) {
        const QJsonObject p = v.toObject();
        if (p.value("song").toString() != m_code || m_code.isEmpty()) {
            continue;
        }
        const QJsonObject s = showById(m_all, p.value("show").toString());
        const int sec = p.value("seconds").toInt();
        out << QVariantMap { { "id", p.value("id").toString() }, { "date", s.value("date").toString() }, { "venue", s.value("venue").toString() },
                             { "time", org::hhmmss(sec) }, { "variant", p.value("variant").toString() }, { "rating", p.value("rating").toInt() },
                             { "link", QString("https://www.youtube.com/watch?v=%1&t=%2s").arg(s.value("video").toString()).arg(sec) } };
    }
    std::sort(out.begin(), out.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value("date").toString() > b.toMap().value("date").toString();
    });
    return out;
}

QVariantList RecordingsModel::releases() const
{
    QVariantList out;
    for (const QJsonValue& v : m_all.value("releases").toArray()) {
        const QJsonObject r = v.toObject();
        if (r.value("song").toString() != m_code || m_code.isEmpty()) {
            continue;
        }
        out << QVariantMap { { "id", r.value("id").toString() }, { "kind", r.value("kind").toString() }, { "label", r.value("label").toString() },
                             { "date", r.value("date").toString() }, { "note", r.value("note").toString() },
                             { "link", r.value("video").toString().isEmpty() ? QString()
                               : "https://www.youtube.com/watch?v=" + r.value("video").toString() } };
    }
    return out;
}

QVariantList RecordingsModel::showChoices() const
{
    QVariantList out;
    out << QVariantMap { { "text", muse::qtrc("starscore", "A new show…") }, { "value", "" } };
    std::vector<QJsonObject> shows;
    for (const QJsonValue& v : m_all.value("shows").toArray()) {
        if (v.toObject().value("band").toString() == "Starsign") {
            shows.push_back(v.toObject());
        }
    }
    std::sort(shows.begin(), shows.end(), [](const QJsonObject& a, const QJsonObject& b) {
        return a.value("date").toString() > b.value("date").toString();
    });
    for (const QJsonObject& s : shows) {
        out << QVariantMap { { "text", s.value("date").toString() + " · " + s.value("venue").toString() }, { "value", s.value("id").toString() } };
    }
    return out;
}

void RecordingsModel::touch(QJsonArray& list, const QString& id, const std::function<void(QJsonObject&)>& edit)
{
    for (int i = 0; i < list.size(); ++i) {
        QJsonObject o = list[i].toObject();
        if (o.value("id").toString() == id) {
            edit(o);
            o["modified"] = QDateTime::currentDateTime().toString(Qt::ISODate);
            list[i] = o;
            m_dirty = true;
            return;
        }
    }
}

void RecordingsModel::setRating(const QString& id, int stars)
{
    QJsonArray perfs = m_all.value("performances").toArray();
    touch(perfs, id, [stars](QJsonObject& o) { o["rating"] = stars > 0 ? QJsonValue(std::min(stars, 5)) : QJsonValue(); });
    m_all["performances"] = perfs;
    emit changed();
}

static int parseTime(const QString& t)
{
    static const QRegularExpression re("^\\s*(?:(\\d+):)?(\\d{1,2}):(\\d{2})\\s*$");
    const QRegularExpressionMatch m = re.match(t);
    if (!m.hasMatch()) {
        return -1;
    }
    return m.captured(1).toInt() * 3600 + m.captured(2).toInt() * 60 + m.captured(3).toInt();
}

void RecordingsModel::setPerformanceField(const QString& id, const QString& key, const QString& value)
{
    QJsonArray perfs = m_all.value("performances").toArray();
    touch(perfs, id, [&](QJsonObject& o) {
        if (key == "seconds") {
            const int s = parseTime(value);
            if (s >= 0) {
                o["seconds"] = s;
            }
        } else if (key == "variant") {
            o["variant"] = value.trimmed().isEmpty() ? QJsonValue() : QJsonValue(value.trimmed());
        }
    });
    m_all["performances"] = perfs;
    emit changed();
}

static void removeById(QJsonObject& all, const char* key, const QString& id)
{
    QJsonArray kept;
    for (const QJsonValue& v : all.value(key).toArray()) {
        if (v.toObject().value("id").toString() != id) {
            kept.append(v);
        }
    }
    all[key] = kept;
    QJsonArray deleted = all.value("deleted").toArray();
    deleted.append(id);
    all["deleted"] = deleted;
}

void RecordingsModel::removePerformance(const QString& id)
{
    removeById(m_all, "performances", id);
    m_dirty = true;
    emit changed();
}

QString RecordingsModel::addPerformance(const QString& showIdIn, const QString& time, const QString& variant, const QString& date,
                                        const QString& venue, const QString& link)
{
    if (m_code.isEmpty()) {
        return muse::qtrc("starscore", "This song has no code yet.");
    }
    const int seconds = parseTime(time.isEmpty() ? QString("0:00") : time);
    if (seconds < 0) {
        return muse::qtrc("starscore", "Write the time as 1:06:25 or 12:40.");
    }
    QString showId = showIdIn;
    if (showId.isEmpty()) {
        const QDate d = QDate::fromString(date.trimmed(), Qt::ISODate);
        const QString vid = org::youTubeId(link);
        if (!d.isValid() || venue.trimmed().isEmpty() || vid.isEmpty()) {
            return muse::qtrc("starscore", "A new show needs its date (yyyy-mm-dd), venue and YouTube link.");
        }
        // the same show again (same day, same video) is reused; another show that day gets its own id
        // ("ss-<date>-2"), where one id per date used to merge two shows into one
        QJsonArray shows = m_all.value("shows").toArray();
        for (const QJsonValue& v : shows) {
            const QJsonObject s = v.toObject();
            if (s.value("date").toString() == d.toString(Qt::ISODate) && s.value("video").toString() == vid) {
                showId = s.value("id").toString();
            }
        }
        if (showId.isEmpty()) {
            showId = org::newShowId(d, org::showIds(shows));
            shows.append(QJsonObject { { "id", showId }, { "band", "Starsign" }, { "date", d.toString(Qt::ISODate) }, { "approx", false },
                                       { "venue", venue.trimmed() }, { "video", vid }, { "timestamps", true }, { "status", "filmed" },
                                       { "added", QDate::currentDate().toString(Qt::ISODate) } });
            m_all["shows"] = shows;
        }
    }
    QJsonArray perfs = m_all.value("performances").toArray();
    perfs.append(QJsonObject { { "id", org::newRecordingId(m_all, "p") }, { "song", m_code }, { "show", showId }, { "seconds", seconds },
                               { "variant", variant.trimmed().isEmpty() ? QJsonValue() : QJsonValue(variant.trimmed()) },
                               { "rating", QJsonValue() }, { "modified", QDateTime::currentDateTime().toString(Qt::ISODate) } });
    m_all["performances"] = perfs;
    m_dirty = true;
    emit changed();
    return QString();
}

RecordingsModel::~RecordingsModel()
{
    *m_alive = false;   // (a YouTube page still loading finds the model gone)
}

void RecordingsModel::setReleaseField(const QString& id, const QString& key, const QString& value)
{
    QJsonArray rels = m_all.value("releases").toArray();
    QString video;
    touch(rels, id, [&](QJsonObject& o) {
        if (key == "link") {
            video = org::youTubeId(value);
            o["video"] = video;
        } else {
            o[key] = value;
        }
    });
    m_all["releases"] = rels;
    // typed by hand: kept from now on
    if (key == "label") {
        m_autoLabel.erase(id);
    } else if (key == "date") {
        m_autoDate.erase(id);
    }
    emit changed();
    if (key == "link" && !video.isEmpty()) {
        lookUpRelease(id, video);
    } else if (key == "kind") {
        fillRelease(id);   // an album is named after the album, the original after its artist
    }
}

void RecordingsModel::releaseLinkTyped(const QString& id, const QString& text)
{
    const QString video = org::youTubeId(text);
    if (video.isEmpty()) {
        return;
    }
    for (const QJsonValue& v : m_all.value("releases").toArray()) {
        if (v.toObject().value("id").toString() == id && v.toObject().value("video").toString() == video) {
            return;   // already this video
        }
    }
    setReleaseField(id, "link", text);
}

void RecordingsModel::lookUpRelease(const QString& id, const QString& videoId)
{
    const auto known = m_looked.find(id);
    if (known != m_looked.end() && known->second.first == videoId) {
        fillRelease(id);
        return;
    }
    const QString key = id + " " + videoId;
    if (m_lookingUp.count(key)) {
        return;   // (the link pasted, then the field left: read once)
    }
    m_lookingUp.insert(key);
    m_status = muse::qtrc("starscore", "Reading the YouTube video…");
    emit changed();
    const QString url = "https://www.youtube.com/watch?v=" + videoId;
    std::weak_ptr<bool> alive = m_alive;
    auto finish = [this, alive, id, videoId, key](const QString& html) {
        if (alive.expired() || !*alive.lock()) {
            return;
        }
        m_lookingUp.erase(key);
        org::YouTubeVideo video;
        if (!org::parseYouTubeWatchPage(html, video)) {
            m_status = muse::qtrc("starscore", "Couldn't read that YouTube video: fill in the album / who and the date by hand.");
            emit changed();
            return;
        }
        m_looked[id] = { videoId, org::youTubeRecording(video) };
        m_status.clear();
        fillRelease(id);
    };
    // the plain page first; YouTube sometimes only gives the details to a browser
    org::fetchPage(url, false, [url, finish, alive](const QString& html, int) {
        if (alive.expired() || !*alive.lock()) {
            return;
        }
        org::YouTubeVideo probe;
        if (org::parseYouTubeWatchPage(html, probe)) {
            finish(html);
        } else {
            org::fetchPage(url, true, [finish](const QString& html2, int) { finish(html2); });
        }
    });
}

void RecordingsModel::fillRelease(const QString& id)
{
    const auto known = m_looked.find(id);
    if (known == m_looked.end()) {
        return;
    }
    const org::YouTubeRecording& r = known->second.second;
    QJsonArray rels = m_all.value("releases").toArray();
    bool filled = false;
    for (int i = 0; i < rels.size(); ++i) {
        QJsonObject o = rels[i].toObject();
        if (o.value("id").toString() != id || o.value("video").toString() != known->second.first) {
            continue;
        }
        // album / who: an album by its name; the original (a cover's) by its artist, and the album when the video
        // names one; a session or a mix by the video's title
        const QString kind = o.value("kind").toString();
        QString label;
        if (kind == "original") {
            label = !r.album.isEmpty() && r.album.compare(r.artist, Qt::CaseInsensitive) != 0
                    ? r.artist + QString::fromUtf8(" \xe2\x80\x94 ") + r.album : r.artist;
        } else if (kind == "album") {
            label = !r.album.isEmpty() ? r.album : r.title;
        } else {
            label = r.title;
        }
        if (!label.isEmpty() && (m_autoLabel.count(id) || o.value("label").toString().trimmed().isEmpty())
            && o.value("label").toString() != label) {
            o["label"] = label;
            m_autoLabel.insert(id);   // (still the video's, so a change of kind names it again)
            filled = true;
        }
        if (r.released.isValid() && (m_autoDate.count(id) || o.value("date").toString().trimmed().isEmpty())
            && o.value("date").toString() != r.released.toString(Qt::ISODate)) {
            o["date"] = r.released.toString(Qt::ISODate);
            m_autoDate.insert(id);
            filled = true;
        }
        if (filled) {
            o["modified"] = QDateTime::currentDateTime().toString(Qt::ISODate);
            rels[i] = o;
            m_dirty = true;
        }
    }
    if (filled) {
        m_all["releases"] = rels;
    }
    emit changed();
}

void RecordingsModel::removeRelease(const QString& id)
{
    removeById(m_all, "releases", id);
    m_dirty = true;
    emit changed();
}

void RecordingsModel::addRelease()
{
    if (m_code.isEmpty()) {
        return;
    }
    QJsonArray rels = m_all.value("releases").toArray();
    const QString newId = org::newRecordingId(m_all, "r");
    // its album / who and date are placeholders until the YouTube link fills them in
    m_autoLabel.insert(newId);
    m_autoDate.insert(newId);
    rels.append(QJsonObject { { "id", newId }, { "song", m_code }, { "kind", "session" }, { "label", "Live session" },
                              { "date", QDate::currentDate().toString(Qt::ISODate) }, { "video", "" },
                              { "modified", QDateTime::currentDateTime().toString(Qt::ISODate) } });
    m_all["releases"] = rels;
    m_dirty = true;
    emit changed();
}

bool RecordingsModel::save()
{
    if (m_code.isEmpty()) {
        return false;
    }
    QJsonObject copy = org::songRecordings(m_all, m_code);
    copy["deleted"] = m_all.value("deleted");
    starScore()->setSongRecordings(copy);
    const QString file = recordingsFile();
    bool ok = true;
    QString why;
    if (!file.isEmpty()) {
        // merge into the file as it is now (the organizer may have added shows meanwhile); a file that is there but
        // can't be read is left alone rather than replaced by this song's copy
        const org::JsonRead read = org::readJsonChecked(file);
        if (read.unreadable()) {
            ok = false;
            why = org::unreadableMessage("recordings.json", read);
        } else {
            QJsonObject current = read.doc.object();
            org::mergeSongRecordings(current, m_code, copy);
            ok = org::writeJson(file, QJsonDocument(current));
            if (ok) {
                m_all = current;
            }
        }
    }
    m_status = ok ? muse::qtrc("starscore", "Saved. Save the .starscore too to keep its copy.")
               : !why.isEmpty() ? muse::qtrc("starscore", "Not saved to recordings.json: %1 The .starscore keeps the changes.").arg(why)
               : muse::qtrc("starscore", "Couldn't write recordings.json; the .starscore keeps the changes.");
    m_dirty = !ok;
    emit changed();
    return ok;
}
