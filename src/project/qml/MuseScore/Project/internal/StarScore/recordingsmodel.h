/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Recordings: this song's live takes, album tracks and sessions, with Joel's star ratings.
 * Saved to Sheets and Demos/6 Inbox/.organizer/recordings.json (all songs) and to the .starscore (this song's copy).
 * New shows usually arrive by themselves: the organizer asks about each new show on setlist.fm.
 */
#pragma once

#include <functional>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "project/istarscoreservice.h"

namespace mu::project {
class RecordingsModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString songTitle READ songTitle NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList performances READ performances NOTIFY changed)
    Q_PROPERTY(QVariantList releases READ releases NOTIFY changed)
    Q_PROPERTY(QVariantList showChoices READ showChoices NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit RecordingsModel(QObject* parent = nullptr);

    QString songTitle() const { return m_code; }
    QString status() const { return m_status; }
    QVariantList performances() const;
    QVariantList releases() const;
    QVariantList showChoices() const;
    bool dirty() const { return m_dirty; }

    Q_INVOKABLE void load();
    Q_INVOKABLE void setRating(const QString& id, int stars);
    //! key: "seconds" (as "1:06:25"), "variant"
    Q_INVOKABLE void setPerformanceField(const QString& id, const QString& key, const QString& value);
    Q_INVOKABLE void removePerformance(const QString& id);
    //! showId "" = a new show from date (yyyy-mm-dd), venue and YouTube link
    Q_INVOKABLE QString addPerformance(const QString& showId, const QString& time, const QString& variant, const QString& date,
                                       const QString& venue, const QString& link);
    //! key: kind (album, wip, session, original), label, date, link, note
    Q_INVOKABLE void setReleaseField(const QString& id, const QString& key, const QString& value);
    Q_INVOKABLE void removeRelease(const QString& id);
    Q_INVOKABLE void addRelease();
    Q_INVOKABLE bool save();

signals:
    void changed();

private:
    QString recordingsFile() const;
    void touch(QJsonArray& list, const QString& id, const std::function<void(QJsonObject&)>& edit);

    QString m_code;
    QString m_status;
    QJsonObject m_all;
    bool m_dirty = false;
};
}
