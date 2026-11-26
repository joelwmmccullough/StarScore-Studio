/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "songsettingsmodel.h"

#include <QDate>
#include <QRegularExpression>
#include <QVariantMap>

#include "project/internal/starscore/organizer/orgfiling.h"
#include "translation.h"

using namespace mu::project;

SongSettingsModel::SongSettingsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void SongSettingsModel::load()
{
    m_version = starScore()->scoreVersion();
    const int year = starScore()->copyrightYear();
    m_hasYear = year > 0;   // a cover has none
    m_year = m_hasYear ? QString::number(year) : QString();
    m_groups.clear();
    m_groupKeys.clear();
    const QString current = starScore()->songGroup();
    m_originalGroup = -1;
    for (const QString& g : starscore::org::starsignSongGroups()) {
        const QStringList seg = g.split('/');
        auto plain = [](QString n) { return n.mid(n.indexOf(' ') + 1); };   // "1 Originals" -> "Originals"
        if (g == current) {
            m_originalGroup = int(m_groupKeys.size());
        }
        m_groups << QVariantMap { { "text", plain(seg.value(1)) + " › " + plain(seg.value(2)) }, { "value", int(m_groupKeys.size()) } };
        m_groupKeys << g;
    }
    m_groupIndex = m_originalGroup;
    m_problem.clear();
    emit changed();
}

void SongSettingsModel::setVersion(const QString& v)
{
    m_version = v.trimmed();
    m_problem.clear();
    emit changed();
}

void SongSettingsModel::setCopyrightYear(const QString& y)
{
    m_year = y.trimmed();
    m_problem.clear();
    emit changed();
}

void SongSettingsModel::setGroupIndex(int i)
{
    m_groupIndex = i;
    emit changed();
}

bool SongSettingsModel::apply()
{
    // the same rules as the old separate dialogs: three numbers for the version; a four-digit year, not after this one
    if (!QRegularExpression("^\\d+\\.\\d+\\.\\d+$").match(m_version).hasMatch()) {
        m_problem = muse::qtrc("starscore", "The version number is three numbers, like 4.0.1.");
        emit changed();
        return false;
    }
    int year = 0;
    if (m_hasYear) {
        year = m_year.toInt();
        if (!QRegularExpression("^\\d{4}$").match(m_year).hasMatch() || year < 1900 || year > QDate::currentDate().year()) {
            m_problem = muse::qtrc("starscore", "The copyright year is a four-digit year, not after this one.");
            emit changed();
            return false;
        }
    }
    if (m_version != starScore()->scoreVersion()) {
        starScore()->setScoreVersion(m_version);
    }
    if (m_hasYear && year != starScore()->copyrightYear()) {
        starScore()->setCopyrightYear(year);
    }
    if (canMove() && m_groupIndex >= 0 && m_groupIndex != m_originalGroup && m_groupIndex < m_groupKeys.size()) {
        const muse::RetVal<QString> r = starScore()->moveSongTo(m_groupKeys.at(m_groupIndex));
        if (!r.ret) {
            m_problem = muse::qtrc("starscore", "The song wasn't moved: %1").arg(QString::fromStdString(r.ret.toString()));
            emit changed();
            return false;
        }
    }
    return true;
}
