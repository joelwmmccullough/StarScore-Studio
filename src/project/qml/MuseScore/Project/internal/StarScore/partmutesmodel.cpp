/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "partmutesmodel.h"

#include "project/internal/starscore/starscoreengraving.h"

using namespace mu::project;

PartMutesModel::PartMutesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    if (starScore()) {
        m_target = starScore()->muteTarget();
    }
}

QStringList PartMutesModel::chosen() const
{
    QStringList out;
    for (const QString& m : m_target.chosen) {
        if (m_target.options.contains(m)) {
            out << m;
        }
    }
    return out;
}

QString PartMutesModel::custom() const
{
    QStringList out;
    for (const QString& m : m_target.chosen) {
        if (!m_target.options.contains(m)) {
            out << m;
        }
    }
    return out.join(", ");
}

QStringList PartMutesModel::mutesFor(const QStringList& ticked, const QString& custom) const
{
    QStringList out;
    for (const QString& m : ticked) {   // in the order they were ticked
        if (m_target.options.contains(m) && !out.contains(m)) {
            out << m;
        }
    }
    // typed in: "Wah-Wah" becomes "Wah-Wah Mute"; several separated by commas
    for (QString m : custom.split(',', Qt::SkipEmptyParts)) {
        m = m.simplified();
        if (m.isEmpty()) {
            continue;
        }
        if (!m.contains("mute", Qt::CaseInsensitive)) {
            m += " Mute";
        }
        if (!out.contains(m)) {
            out << m;
        }
    }
    return out;
}

QString PartMutesModel::preview(const QStringList& ticked, const QString& custom) const
{
    const QStringList mutes = mutesFor(ticked, custom);
    return mutes.isEmpty() ? QString() : starscore::muteLines(mutes);
}

void PartMutesModel::apply(const QStringList& ticked, const QString& custom)
{
    if (starScore() && !m_target.partId.isEmpty()) {
        starScore()->setPartMutes(m_target.partId, mutesFor(ticked, custom));
    }
}
