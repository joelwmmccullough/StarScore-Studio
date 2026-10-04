/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "bandexportmodel.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTimer>

#include "translation.h"

using namespace mu::project;

BandExportModel::BandExportModel(QObject* parent)
    : QAbstractListModel(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariant BandExportModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(m_items.size())) {
        return QVariant();
    }
    const Item& item = m_items[size_t(index.row())];
    switch (role) {
    case HeaderRole: return item.header;
    case PathRole: return item.path;
    case FolderRole: return item.folder;
    case NameRole: return item.name;
    case CheckedRole: return item.checked;
    default: return QVariant();
    }
}

int BandExportModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QHash<int, QByteArray> BandExportModel::roleNames() const
{
    return {
        { HeaderRole, "header" }, { PathRole, "path" }, { FolderRole, "folder" }, { NameRole, "name" }, { CheckedRole, "checked" },
    };
}

QString BandExportModel::heading() const
{
    return m_heading;
}

QString BandExportModel::errorText() const
{
    return m_error;
}

QString BandExportModel::notes() const
{
    return m_notes;
}

int BandExportModel::checkedCount() const
{
    int n = 0;
    for (const Item& item : m_items) {
        n += (!item.header && item.checked) ? 1 : 0;
    }
    return n;
}

QString BandExportModel::result() const
{
    return m_result;
}

void BandExportModel::setResult(const QString& text)
{
    if (m_result != text) {
        m_result = text;
        emit resultChanged();
    }
}

QString BandExportModel::currentVersion() const
{
    return m_version;
}

int BandExportModel::bump() const
{
    return m_bump;
}

void BandExportModel::setBump(int bump)
{
    if (m_bump != bump) {
        m_bump = bump;
        emit bumpChanged();
    }
}

QString BandExportModel::exportVersion() const
{
    QStringList bits = m_version.split('.');
    while (bits.size() < 3) {
        bits << "0";
    }
    int major = bits[0].toInt();
    int minor = bits[1].toInt();
    int patch = bits[2].toInt();
    if (m_bump == 1) {
        ++major;
        minor = 0;
        patch = 0;
    } else if (m_bump == 2) {
        ++minor;
        patch = 0;
    } else if (m_bump == 3) {
        ++patch;
    }
    return QString("%1.%2.%3").arg(major).arg(minor).arg(patch);
}

bool BandExportModel::newSong() const
{
    return m_newSong;
}

QString BandExportModel::songTitle() const
{
    return m_songTitle;
}

QString BandExportModel::suggestedCode() const
{
    return m_suggestedCode;
}

QString BandExportModel::createSong(const QString& title, int category, const QString& code)
{
    const muse::Ret ret = starScore()->registerBandSong(title, category, code);
    if (!ret) {
        return QString::fromStdString(ret.text().empty() ? ret.toString() : ret.text());
    }
    load();
    return QString();
}

void BandExportModel::load()
{
    m_version = starScore()->scoreVersion();
    m_bump = 0;
    emit bumpChanged();

    beginResetModel();
    m_items.clear();
    m_error.clear();
    m_notes.clear();
    m_heading.clear();
    m_code.clear();
    m_newSong = false;
    m_songTitle.clear();
    m_suggestedCode.clear();

    muse::RetVal<StarScoreBandExportPlan> plan = starScore()->planBandExport();
    if (!plan.ret) {
        m_error = QString::fromStdString(plan.ret.text().empty() ? plan.ret.toString() : plan.ret.text());
    } else if (plan.val.newSong) {
        m_newSong = true;
        m_songTitle = plan.val.title;
        m_suggestedCode = plan.val.suggestedCode;
        m_heading = muse::qtrc("starscore", "New song in Sheets and Demos");
    } else {
        m_code = plan.val.code;
        m_heading = muse::qtrc("starscore", "Sheets and Demos / %1").arg(plan.val.songFolder);
        m_notes = plan.val.notes.join("\n");
        const QStringList unticked = starScore()->bandExportUnticked(m_code);
        // Group by folder, keeping the plan's order of first appearance; a header row starts each folder
        QStringList folders;
        for (const StarScoreBandFile& f : plan.val.files) {
            const QString folder = f.relativePath.section('/', 0, -2);
            if (!folders.contains(folder)) {
                folders << folder;
            }
        }
        for (const QString& folder : folders) {
            m_items.push_back({ true, QString(), folder, folder, true });
            for (const StarScoreBandFile& f : plan.val.files) {
                if (f.relativePath.section('/', 0, -2) != folder) {
                    continue;
                }
                m_items.push_back({ false, f.relativePath, folder, f.relativePath.section('/', -1),
                                    !unticked.contains(f.relativePath) && !f.defaultUnchecked });
            }
        }
        for (Item& item : m_items) {
            if (item.header) {
                item.checked = isFolderChecked(item.folder);
            }
        }
    }
    endResetModel();

    emit loaded();
    emit checkedCountChanged();
}

void BandExportModel::setChecked(int index, bool checked)
{
    if (index < 0 || index >= int(m_items.size())) {
        return;
    }
    Item& item = m_items[size_t(index)];
    if (item.header) {
        setFolderChecked(item.folder, checked);
        return;
    }
    if (item.checked == checked) {
        return;
    }
    item.checked = checked;
    emit dataChanged(this->index(index), this->index(index), { CheckedRole });
    refreshHeaders();
    emit checkedCountChanged();
}

void BandExportModel::refreshHeaders()
{
    for (int i = 0; i < int(m_items.size()); ++i) {
        Item& item = m_items[size_t(i)];
        if (!item.header) {
            continue;
        }
        const bool want = isFolderChecked(item.folder);
        if (item.checked != want) {
            item.checked = want;
            emit dataChanged(index(i), index(i), { CheckedRole });
        }
    }
}

void BandExportModel::setFolderChecked(const QString& folder, bool checked)
{
    // the folder's rows are consecutive (its header, then its sheets): one change notice covers them
    int first = -1, last = -1;
    for (int i = 0; i < int(m_items.size()); ++i) {
        Item& item = m_items[size_t(i)];
        if (item.folder == folder && item.checked != checked) {
            item.checked = checked;
            first = first < 0 ? i : first;
            last = i;
        }
    }
    if (first >= 0) {
        emit dataChanged(index(first), index(last), { CheckedRole });
    }
    refreshHeaders();
    emit checkedCountChanged();
}

void BandExportModel::setAllChecked(bool checked)
{
    if (m_items.empty()) {
        return;
    }
    for (Item& item : m_items) {
        item.checked = checked;
    }
    emit dataChanged(index(0), index(int(m_items.size()) - 1), { CheckedRole });
    emit checkedCountChanged();
}

bool BandExportModel::isFolderChecked(const QString& folder) const
{
    for (const Item& item : m_items) {
        if (!item.header && item.folder == folder && !item.checked) {
            return false;
        }
    }
    return true;
}

void BandExportModel::saveTicks()
{
    QStringList unticked;
    for (const Item& item : m_items) {
        if (!item.header && !item.checked) {
            unticked << item.path;
        }
    }
    starScore()->setBandExportUnticked(m_code, unticked);
}

QString BandExportModel::exportNow()
{
    saveTicks();

    QStringList paths;
    for (const Item& item : m_items) {
        if (!item.header && item.checked) {
            paths << item.path;
        }
    }
    if (paths.isEmpty()) {
        setResult(muse::qtrc("starscore", "Nothing is ticked."));
        return m_result;
    }

    // The version printed on these sheets (and kept in the score for next time)
    const QString version = exportVersion();
    starScore()->setScoreVersion(version);
    m_version = version;
    m_bump = 0;
    emit loaded();
    emit bumpChanged();

    muse::RetVal<QString> summary = starScore()->exportToBandFolder(paths);
    if (!summary.ret) {
        setResult(muse::qtrc("starscore", "Export failed: %1").arg(QString::fromStdString(summary.ret.toString())));
        return m_result;
    }
    setResult(summary.val + "\n\n"
              + muse::qtrc("starscore", "Sheets are marked Version %1. Save the .starscore to keep this version number.").arg(version));
    return m_result;
}

bool BandExportModel::runOrganizer() const
{
    return QSettings().value("StarScore/runOrganizerAfterExport", true).toBool();
}

void BandExportModel::setRunOrganizer(bool on)
{
    QSettings().setValue("StarScore/runOrganizerAfterExport", on);
    emit runOrganizerChanged();
}

void BandExportModel::openOrganizer()
{
    // Opened once the export window has closed: a window opened from it takes it as its parent,
    // and the framework closes a window as soon as its parent hides (which stopped every run right away)
    std::shared_ptr<muse::IInteractive> ia = interactive();
    QTimer::singleShot(400, qApp, [ia]() {
        ia->open(muse::UriQuery("musescore://starscore/organizer?mode=export"));
    });
}
