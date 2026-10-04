/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <vector>

#include <QAbstractListModel>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "iinteractive.h"
#include "project/istarscoreservice.h"

namespace mu::project {
//! "Export to Sheets and Demos": every planned sheet with a tick box, grouped under a header row per folder.
//! Un-ticked sheets are remembered per song (by its code) for the next export.
//! The rows are a list model (roles: header, path, folder, name, checked), so a tick changes one row in place;
//! until Oct 2026 every tick replaced the whole list and the view rebuilt every row.
class BandExportModel : public QAbstractListModel, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString heading READ heading NOTIFY loaded)
    Q_PROPERTY(QString errorText READ errorText NOTIFY loaded)
    Q_PROPERTY(QString notes READ notes NOTIFY loaded)
    Q_PROPERTY(int checkedCount READ checkedCount NOTIFY checkedCountChanged)
    Q_PROPERTY(QString currentVersion READ currentVersion NOTIFY loaded)
    Q_PROPERTY(int bump READ bump WRITE setBump NOTIFY bumpChanged)   // 0 none, 1 first number, 2 second, 3 third
    Q_PROPERTY(QString exportVersion READ exportVersion NOTIFY bumpChanged)
    Q_PROPERTY(bool newSong READ newSong NOTIFY loaded)                 // not in Sheets and Demos yet
    Q_PROPERTY(QString songTitle READ songTitle NOTIFY loaded)
    Q_PROPERTY(QString suggestedCode READ suggestedCode NOTIFY loaded)
    Q_PROPERTY(bool runOrganizer READ runOrganizer WRITE setRunOrganizer NOTIFY runOrganizerChanged)
    Q_PROPERTY(QString result READ result NOTIFY resultChanged)         // what exportNow() reported

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit BandExportModel(QObject* parent = nullptr);

    QVariant data(const QModelIndex& index, int role) const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString heading() const;
    QString errorText() const;
    QString notes() const;
    int checkedCount() const;
    QString currentVersion() const;
    int bump() const;
    void setBump(int bump);
    QString exportVersion() const;
    bool newSong() const;
    QString songTitle() const;
    QString suggestedCode() const;
    bool runOrganizer() const;
    void setRunOrganizer(bool on);
    QString result() const;

    //! After an export: opens the organizer window, which runs the folder organization for this export
    Q_INVOKABLE void openOrganizer();

    //! Adds the song to Sheets and Demos (category 1–4) and plans the export. Returns an error, or "" when done.
    Q_INVOKABLE QString createSong(const QString& title, int category, const QString& code);

    Q_INVOKABLE void load();
    Q_INVOKABLE void setChecked(int index, bool checked);
    Q_INVOKABLE void setFolderChecked(const QString& folder, bool checked);
    Q_INVOKABLE void setAllChecked(bool checked);
    Q_INVOKABLE bool isFolderChecked(const QString& folder) const;

    //! Remembers the ticks, writes the ticked sheets and returns a summary (also kept in `result`)
    Q_INVOKABLE QString exportNow();

signals:
    void loaded();
    void checkedCountChanged();
    void bumpChanged();
    void runOrganizerChanged();
    void resultChanged();

private:
    enum Roles {
        HeaderRole = Qt::UserRole + 1,
        PathRole,
        FolderRole,
        NameRole,
        CheckedRole,
    };
    struct Item {
        bool header = false;
        QString path;      // relative to the song folder ("" for a header)
        QString folder;
        QString name;
        bool checked = true;
    };

    void saveTicks();
    //! A header row is ticked when every sheet of its folder is; tells the view about the headers that changed
    void refreshHeaders();
    void setResult(const QString& text);

    QString m_heading;
    QString m_error;
    QString m_notes;
    QString m_code;
    std::vector<Item> m_items;
    QString m_version;
    int m_bump = 0;
    bool m_newSong = false;
    QString m_songTitle;
    QString m_suggestedCode;
    QString m_result;
};
}
