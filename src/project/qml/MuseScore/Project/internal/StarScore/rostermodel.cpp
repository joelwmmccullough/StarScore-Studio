/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the band roster
 */
#include "rostermodel.h"

#include <QFileInfo>

#include "translation.h"

using namespace mu::project;
namespace org = mu::project::starscore::org;

RosterModel::RosterModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void RosterModel::load()
{
    const QString band = starScore()->bandFolder();
    if (band.isEmpty() || !QFileInfo(band).isDir()) {
        m_roster = org::Roster::defaults();
        m_status = muse::qtrc("starscore", "Sheets and Demos wasn't found, so this list can't be saved yet.");
    } else {
        const org::Paths paths = org::Paths::make(band, QString());
        m_roster.load(paths);
        m_status = QFileInfo::exists(paths.toolkit + "/roster.json")
                   ? muse::qtrc("starscore", "Changes rebuild every song's changelogs and Horn Part Guides at the next organization.")
                   : muse::qtrc("starscore", "This is the band as of October 2026. Save to keep it in Sheets and Demos.");
    }
    m_dirty = false;
    emit changed();
}

QVariantList RosterModel::players() const
{
    QVariantList out;
    for (const org::Player& p : m_roster.players) {
        QVariantMap m { { "name", p.name }, { "instruments", p.instruments.join(", ") }, { "blurb", p.blurb }, { "current", p.current },
                        { "horn", p.horn } };
        for (int n : { 2, 3 }) {
            auto it = p.chairs.find(n);
            m[QString("chair%1").arg(n)] = it == p.chairs.end() ? 0 : it->second.chair;
            m[QString("key%1").arg(n)] = it == p.chairs.end() ? QString() : it->second.key;
            m[QString("inst%1").arg(n)] = it == p.chairs.end() ? QString() : it->second.instrument;
        }
        out << m;
    }
    return out;
}

void RosterModel::setField(int index, const QString& key, const QVariant& value)
{
    if (index < 0 || index >= int(m_roster.players.size())) {
        return;
    }
    org::Player& p = m_roster.players[index];
    if (key == "name") {
        p.name = value.toString().trimmed();
    } else if (key == "instruments") {
        p.instruments.clear();
        for (const QString& i : value.toString().split(',', Qt::SkipEmptyParts)) {
            p.instruments << i.trimmed();
        }
    } else if (key == "blurb") {
        p.blurb = value.toString();
    } else if (key == "current") {
        p.current = value.toBool();
    } else if (key == "horn") {
        p.horn = value.toBool();
    } else if (key.startsWith("chair") || key.startsWith("key") || key.startsWith("inst")) {
        const int n = key.right(1).toInt();
        org::Chair& c = p.chairs[n];
        if (key.startsWith("chair")) {
            c.chair = value.toInt();
        } else if (key.startsWith("key")) {
            c.key = value.toString();
        } else {
            c.instrument = value.toString().toLower();
        }
        if (c.chair <= 0) {
            p.chairs.erase(n);
        } else if (c.instrument.isEmpty() && !p.instruments.isEmpty()) {
            c.instrument = p.instruments.first().toLower();
        }
    }
    m_dirty = true;
    emit changed();
}

void RosterModel::addPlayer()
{
    org::Player p;
    p.name = muse::qtrc("starscore", "New player");
    m_roster.players.push_back(p);
    m_dirty = true;
    emit changed();
}

bool RosterModel::save()
{
    const QString band = starScore()->bandFolder();
    if (band.isEmpty() || !QFileInfo(band).isDir()) {
        return false;
    }
    const bool ok = m_roster.save(org::Paths::make(band, QString()));
    m_status = ok ? muse::qtrc("starscore", "Saved. The next organization rebuilds the changelogs and Horn Part Guides.")
               : muse::qtrc("starscore", "Couldn't save roster.json.");
    m_dirty = !ok;
    emit changed();
    return ok;
}
