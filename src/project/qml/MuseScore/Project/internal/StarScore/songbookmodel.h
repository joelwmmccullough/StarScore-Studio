/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Songbooks: builds album songbooks (one per instrument) and song charts from the parts
 * that are done, and says what's still missing.
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "iinteractive.h"
#include "project/istarscoreservice.h"

namespace mu::project {
class SongbookModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList books READ books CONSTANT)
    Q_PROPERTY(QString bookId READ bookId NOTIFY changed)
    Q_PROPERTY(bool isChart READ isChart NOTIFY changed)
    Q_PROPERTY(QStringList albums READ albums NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString tracklist READ tracklist NOTIFY changed)
    Q_PROPERTY(QVariantList librarySongs READ librarySongs NOTIFY changed)
    Q_PROPERTY(QString songPath READ songPath NOTIFY changed)
    Q_PROPERTY(QVariantList plan READ plan NOTIFY changed)
    Q_PROPERTY(int readyCount READ readyCount NOTIFY changed)
    Q_PROPERTY(int songCount READ songCount NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString report READ report NOTIFY changed)
    Q_PROPERTY(QString outputFolder READ outputFolder NOTIFY changed)
    Q_PROPERTY(QString lastOutput READ lastOutput NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit SongbookModel(QObject* parent = nullptr);

    QVariantList books() const;
    QString bookId() const;
    bool isChart() const;
    QStringList albums() const;
    QString album() const;
    QString tracklist() const;
    QVariantList librarySongs() const;
    QString songPath() const;
    QVariantList plan() const;
    int readyCount() const;
    int songCount() const;
    bool busy() const;
    QString status() const;
    QString report() const;
    QString outputFolder() const;
    QString lastOutput() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void setBookId(const QString& id);
    Q_INVOKABLE void setAlbum(const QString& album);
    Q_INVOKABLE void setTracklist(const QString& text);
    Q_INVOKABLE void addAlbum(const QString& name);
    Q_INVOKABLE void setSongPath(const QString& path);
    Q_INVOKABLE QString notes(const QString& song) const;
    Q_INVOKABLE void setNotes(const QString& song, const QString& text);
    Q_INVOKABLE void chooseOutputFolder();
    Q_INVOKABLE void openOutput();
    //! Reads the songs that changed since they were last read, then checks again what's ready
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void build();
    Q_INVOKABLE void cancel();

signals:
    void changed();

private:
    struct Song {
        QString title;          // as in the tracklist
        QString path;           // the .starscore, empty if not found
        bool legacy = false;
        bool scanned = false;
        StarScoreAuditFileSummary summary;
    };
    struct SheetPlan {
        StarScoreSongbookSheet sheet;
        QString label;          // "Duo · Bottom line"
        QString chapterKind;    // "SOLO", "DUO", "TRIO", "PART"
        QString chapterTitle;
        bool ready = false;
        bool optional = false;  // left out quietly when missing (a piano part where keys read the lead sheet)
        QString why;
    };

    void recheck();
    std::vector<SheetPlan> sheetsFor(const Song& song) const;
    void step();
    void finishBuild();
    QString defaultOutput() const;
    void saveAlbums() const;
    Song songFor(const QString& title) const;

    QString m_bookId = "tenor";
    QString m_album = "Ichiban";
    std::map<QString, QStringList> m_albums;
    QStringList m_albumOrder;
    QString m_songPath;
    QString m_library;
    QStringList m_libraryFiles;

    std::vector<Song> m_songs;              // album songs (or the one chart song)
    std::vector<std::vector<SheetPlan> > m_sheets;
    QVariantList m_plan;
    int m_ready = 0;

    // building / refreshing
    bool m_busy = false;
    bool m_refreshing = false;
    QStringList m_scanQueue;
    size_t m_buildIndex = 0;
    QString m_workDir;
    QString m_outFile;
    QString m_status;
    QString m_report;
    QString m_lastOutput;
    std::vector<size_t> m_buildSongs;       // indexes into m_songs that go in
    std::vector<std::vector<StarScoreSongbookSheet> > m_rendered;
};
}
