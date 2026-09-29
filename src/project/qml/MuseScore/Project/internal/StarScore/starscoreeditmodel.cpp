/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "starscoreeditmodel.h"

#include <QRegularExpression>

#include "translation.h"

using namespace mu::project;

StarScoreEditModel::StarScoreEditModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QString StarScoreEditModel::dialogTitle() const
{
    if (m_mode == "section") {
        return m_itemId.isEmpty() ? muse::qtrc("starscore", "New section from existing instruments")
               : muse::qtrc("starscore", "Section instruments");
    } else if (m_mode == "arrangement") {
        return m_itemId.isEmpty() ? muse::qtrc("starscore", "New blank arrangement")
               : muse::qtrc("starscore", "Arrangement sections");
    } else if (m_mode == "rename-section") {
        return muse::qtrc("starscore", "Rename section");
    } else if (m_mode == "rename-arrangement") {
        return muse::qtrc("starscore", "Rename arrangement");
    } else if (m_mode == "rename-solo") {
        return muse::qtrc("starscore", "Rename solo");
    } else if (m_mode == "version") {
        return muse::qtrc("starscore", "Score version number (e.g. 4.0.1)");
    }
    return QString();
}

QString StarScoreEditModel::name() const
{
    return m_name;
}

void StarScoreEditModel::setName(const QString& name)
{
    if (m_name != name) {
        m_name = name;
        emit nameChanged();
    }
}

bool StarScoreEditModel::showList() const
{
    return m_mode == "section" || m_mode == "arrangement";
}

QString StarScoreEditModel::listTitle() const
{
    return m_mode == "section" ? muse::qtrc("starscore", "Instruments in this section")
           : muse::qtrc("starscore", "Sections in this arrangement");
}

QVariantList StarScoreEditModel::items() const
{
    return m_items;
}

void StarScoreEditModel::load(const QString& mode, const QString& itemId)
{
    m_mode = mode;
    m_itemId = itemId;
    m_items.clear();
    m_name.clear();

    const std::vector<StarScoreSection> sections = starScore()->sections();
    const std::vector<StarScoreArrangement> arrangements = starScore()->arrangements();

    if (mode == "version") {
        m_name = starScore()->scoreVersion();
    } else if (mode == "rename-solo") {
        for (const StarScoreSolo& solo : starScore()->solos()) {
            if (solo.id == itemId) {
                m_name = solo.name;
            }
        }
    } else if (mode == "section" || mode == "rename-section") {
        QStringList selected;
        for (const StarScoreSection& s : sections) {
            if (s.id == itemId) {
                m_name = s.name;
                selected = s.partIds;
            }
        }
        if (m_name.isEmpty()) {
            m_name = muse::qtrc("starscore", "New Section");
        }

        if (mode == "section") {
            for (const StarScorePartInfo& p : starScore()->parts()) {
                QStringList otherSections;
                for (const StarScoreSection& s : sections) {
                    if (s.id != itemId && p.sectionIds.contains(s.id)) {
                        otherSections << s.name;
                    }
                }
                m_items << QVariantMap {
                    { "id", p.partId },
                    { "title", p.name },
                    { "note", otherSections.isEmpty() ? QString() : muse::qtrc("starscore", "also in %1").arg(otherSections.join(", ")) },
                    { "checked", selected.contains(p.partId) }
                };
            }
        }
    } else if (mode == "arrangement" || mode == "rename-arrangement") {
        QStringList selected;
        for (const StarScoreArrangement& a : arrangements) {
            if (a.id == itemId) {
                m_name = a.name;
                selected = a.sectionIds;
            }
        }
        if (m_name.isEmpty()) {
            m_name = muse::qtrc("starscore", "New Arrangement");
        }

        if (mode == "arrangement") {
            for (const StarScoreSection& s : sections) {
                m_items << QVariantMap {
                    { "id", s.id },
                    { "title", s.name },
                    { "note", muse::qtrc("starscore", "%n instrument(s)", nullptr, int(s.partIds.size())) },
                    { "checked", selected.contains(s.id) }
                };
            }
        }
    }

    emit loaded();
    emit nameChanged();
    emit itemsChanged();
}

void StarScoreEditModel::setChecked(int index, bool checked)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }
    QVariantMap item = m_items[index].toMap();
    item["checked"] = checked;
    m_items[index] = item;
    emit itemsChanged();
}

bool StarScoreEditModel::apply()
{
    const QString name = m_name.trimmed();
    if (name.isEmpty()) {
        return false;
    }

    QStringList checked;
    for (const QVariant& v : m_items) {
        const QVariantMap item = v.toMap();
        if (item["checked"].toBool()) {
            checked << item["id"].toString();
        }
    }

    if (m_mode == "version") {
        // three numbers, like 4.0.1; printed in the footer of the score and every part
        if (!QRegularExpression("^\\d+\\.\\d+\\.\\d+$").match(name).hasMatch()) {
            return false;
        }
        starScore()->setScoreVersion(name);
    } else if (m_mode == "rename-solo") {
        starScore()->renameSolo(m_itemId, name);
    } else if (m_mode == "rename-section") {
        starScore()->renameSection(m_itemId, name);
    } else if (m_mode == "rename-arrangement") {
        starScore()->renameArrangement(m_itemId, name);
    } else if (m_mode == "section") {
        if (m_itemId.isEmpty()) {
            return bool(starScore()->createSectionFromParts(name, checked).ret);
        }
        starScore()->renameSection(m_itemId, name);
        starScore()->setSectionParts(m_itemId, checked);
    } else if (m_mode == "arrangement") {
        if (m_itemId.isEmpty()) {
            muse::RetVal<QString> ret = starScore()->createArrangement(name, checked);
            if (ret.ret && !checked.isEmpty()) {
                starScore()->showArrangement(ret.val);
            }
            return bool(ret.ret);
        }
        starScore()->renameArrangement(m_itemId, name);
        starScore()->setArrangementSections(m_itemId, checked);
    }

    return true;
}
