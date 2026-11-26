/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "project/istarscoreservice.h"

namespace mu::project {
//! (1.40.0) The Song settings window: the song's version number, copyright year and where it's kept in Projects and
//! Sheets (they were three items in the panel's "…" menu)
class SongSettingsModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString version READ version WRITE setVersion NOTIFY changed)
    Q_PROPERTY(QString copyrightYear READ copyrightYear WRITE setCopyrightYear NOTIFY changed)
    Q_PROPERTY(bool hasCopyrightYear READ hasCopyrightYear NOTIFY changed)
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(int groupIndex READ groupIndex WRITE setGroupIndex NOTIFY changed)
    Q_PROPERTY(bool canMove READ canMove NOTIFY changed)
    Q_PROPERTY(QString problem READ problem NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };

public:
    explicit SongSettingsModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE bool apply();

    QString version() const { return m_version; }
    void setVersion(const QString& v);
    QString copyrightYear() const { return m_year; }
    void setCopyrightYear(const QString& y);
    bool hasCopyrightYear() const { return m_hasYear; }
    QVariantList groups() const { return m_groups; }
    int groupIndex() const { return m_groupIndex; }
    void setGroupIndex(int i);
    bool canMove() const { return !m_groupKeys.isEmpty() && m_originalGroup >= 0; }
    QString problem() const { return m_problem; }

signals:
    void changed();

private:
    QString m_version;
    QString m_year;
    bool m_hasYear = false;
    QVariantList m_groups;
    QStringList m_groupKeys;
    int m_groupIndex = -1;
    int m_originalGroup = -1;
    QString m_problem;
};
}
