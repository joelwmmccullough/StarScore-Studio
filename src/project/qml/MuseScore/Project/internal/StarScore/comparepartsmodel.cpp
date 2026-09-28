/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "comparepartsmodel.h"

using namespace mu::project;

ComparePartsModel::ComparePartsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariantList ComparePartsModel::groups() const
{
    return m_groups;
}

void ComparePartsModel::load()
{
    m_groups.clear();
    for (const StarScoreComparison& c : starScore()->compareParts(m_refs)) {
        QVariantList parts;
        for (const StarScoreComparedPart& p : c.parts) {
            QVariantList bars;
            for (int b : p.bars) {
                bars << b;
            }
            parts << QVariantMap {
                { "partId", p.partId },
                { "label", p.label },
                { "summary", p.summary },
                { "isReference", p.isReference },
                { "bars", bars },
            };
        }
        m_groups << QVariantMap {
            { "instrument", c.instrument },
            { "barCount", c.barCount },
            { "parts", parts },
        };
    }
    emit changed();
}

void ComparePartsModel::setReference(const QString& instrument, const QString& partId)
{
    m_refs[instrument] = partId;
    load();
}

void ComparePartsModel::selectBar(const QString& partId, int bar)
{
    starScore()->selectBar(partId, bar);
}
