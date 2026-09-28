/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "bandexportmodel.h"

#include "translation.h"

using namespace mu::project;

BandExportModel::BandExportModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
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

QVariantList BandExportModel::items() const
{
    return m_items;
}

int BandExportModel::checkedCount() const
{
    int n = 0;
    for (const QVariant& v : m_items) {
        const QVariantMap item = v.toMap();
        n += (!item.value("header").toBool() && item.value("checked").toBool()) ? 1 : 0;
    }
    return n;
}

void BandExportModel::load()
{
    m_items.clear();
    m_error.clear();
    m_notes.clear();
    m_heading.clear();
    m_code.clear();

    muse::RetVal<StarScoreBandExportPlan> plan = starScore()->planBandExport();
    if (!plan.ret) {
        m_error = QString::fromStdString(plan.ret.text().empty() ? plan.ret.toString() : plan.ret.text());
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
            m_items << QVariantMap { { "header", true }, { "folder", folder }, { "name", folder }, { "checked", true } };
            for (const StarScoreBandFile& f : plan.val.files) {
                if (f.relativePath.section('/', 0, -2) != folder) {
                    continue;
                }
                m_items << QVariantMap {
                    { "header", false },
                    { "path", f.relativePath },
                    { "folder", folder },
                    { "name", f.relativePath.section('/', -1) },
                    { "checked", !unticked.contains(f.relativePath) },
                };
            }
        }
        refreshHeaders();
    }

    emit loaded();
    emit itemsChanged();
}

void BandExportModel::setChecked(int index, bool checked)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    QVariantMap item = m_items[index].toMap();
    if (item.value("header").toBool()) {
        setFolderChecked(item.value("folder").toString(), checked);
        return;
    }
    item["checked"] = checked;
    m_items[index] = item;
    refreshHeaders();
    emit itemsChanged();
}

void BandExportModel::refreshHeaders()
{
    for (int i = 0; i < m_items.size(); ++i) {
        QVariantMap item = m_items[i].toMap();
        if (item.value("header").toBool()) {
            item["checked"] = isFolderChecked(item.value("folder").toString());
            m_items[i] = item;
        }
    }
}

void BandExportModel::setFolderChecked(const QString& folder, bool checked)
{
    for (int i = 0; i < m_items.size(); ++i) {
        QVariantMap item = m_items[i].toMap();
        if (item.value("folder").toString() == folder) {
            item["checked"] = checked;
            m_items[i] = item;
        }
    }
    refreshHeaders();
    emit itemsChanged();
}

void BandExportModel::setAllChecked(bool checked)
{
    for (int i = 0; i < m_items.size(); ++i) {
        QVariantMap item = m_items[i].toMap();
        item["checked"] = checked;
        m_items[i] = item;
    }
    emit itemsChanged();
}

bool BandExportModel::isFolderChecked(const QString& folder) const
{
    for (const QVariant& v : m_items) {
        const QVariantMap item = v.toMap();
        if (!item.value("header").toBool() && item.value("folder").toString() == folder && !item.value("checked").toBool()) {
            return false;
        }
    }
    return true;
}

void BandExportModel::saveTicks()
{
    QStringList unticked;
    for (const QVariant& v : m_items) {
        const QVariantMap item = v.toMap();
        if (!item.value("header").toBool() && !item.value("checked").toBool()) {
            unticked << item.value("path").toString();
        }
    }
    starScore()->setBandExportUnticked(m_code, unticked);
}

QString BandExportModel::exportNow()
{
    saveTicks();

    QStringList paths;
    for (const QVariant& v : m_items) {
        const QVariantMap item = v.toMap();
        if (!item.value("header").toBool() && item.value("checked").toBool()) {
            paths << item.value("path").toString();
        }
    }
    if (paths.isEmpty()) {
        return muse::qtrc("starscore", "Nothing is ticked.");
    }

    muse::RetVal<QString> summary = starScore()->exportToBandFolder(paths);
    if (!summary.ret) {
        return muse::qtrc("starscore", "Export failed: %1").arg(QString::fromStdString(summary.ret.toString()));
    }
    return summary.val;
}
