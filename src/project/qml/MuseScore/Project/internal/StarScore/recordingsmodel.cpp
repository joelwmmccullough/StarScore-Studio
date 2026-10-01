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
#include "project/internal/starscore/organizer/orgrecordings.h"
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
    m_all = file.isEmpty() ? QJsonObject() : org::readJsonObject(file);
    const QJsonObject copy = starScore()->songRecordings();
    if (!m_code.isEmpty() && !copy.isEmpty()) {
        org::mergeSongRecordings(m_all, m_code, copy);    // edits made while Sheets and Demos wasn't there
    }
    if (m_code.isEmpty()) {
        m_status = muse::qtrc("starscore", "This song has no code yet: export it to Sheets and Demos first.");
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
    const QRegularExpressionMatch m = QRegularExpression("^\\s*(?:(\\d+):)?(\\d{1,2}):(\\d{2})\\s*$").match(t);
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
        showId = "ss-" + d.toString(Qt::ISODate);
        if (showById(m_all, showId).isEmpty()) {
            QJsonArray shows = m_all.value("shows").toArray();
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

void RecordingsModel::setReleaseField(const QString& id, const QString& key, const QString& value)
{
    QJsonArray rels = m_all.value("releases").toArray();
    touch(rels, id, [&](QJsonObject& o) {
        if (key == "link") {
            o["video"] = org::youTubeId(value);
        } else {
            o[key] = value;
        }
    });
    m_all["releases"] = rels;
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
    rels.append(QJsonObject { { "id", org::newRecordingId(m_all, "r") }, { "song", m_code }, { "kind", "session" }, { "label", "Live session" },
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
    if (!file.isEmpty()) {
        // merge into the file as it is now (the organizer may have added shows meanwhile)
        QJsonObject current = org::readJsonObject(file);
        org::mergeSongRecordings(current, m_code, copy);
        ok = org::writeJson(file, QJsonDocument(current));
        m_all = current;
    }
    m_status = ok ? muse::qtrc("starscore", "Saved. Save the .starscore too to keep its copy.")
               : muse::qtrc("starscore", "Couldn't write recordings.json; the .starscore keeps the changes.");
    m_dirty = !ok;
    emit changed();
    return ok;
}
