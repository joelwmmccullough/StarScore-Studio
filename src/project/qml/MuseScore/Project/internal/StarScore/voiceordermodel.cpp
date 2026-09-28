/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "voiceordermodel.h"

using namespace mu::project;

VoiceOrderModel::VoiceOrderModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariantList VoiceOrderModel::sections() const
{
    return m_sections;
}

void VoiceOrderModel::load()
{
    m_sections.clear();
    for (const StarScoreVoiceSection& s : starScore()->checkVoiceOrder()) {
        QVariantList rules;
        for (const StarScoreVoiceRule& r : s.rules) {
            QVariantList bars;
            for (int b : r.bars) {
                bars << b;
            }
            rules << QVariantMap {
                { "upperPartId", r.upperPartId },
                { "label", r.label },
                { "summary", r.summary },
                { "strict", r.strict },
                { "bars", bars },
            };
        }
        m_sections << QVariantMap { { "section", s.section }, { "barCount", s.barCount }, { "rules", rules } };
    }
    emit changed();
}

void VoiceOrderModel::selectBar(const QString& partId, int bar)
{
    starScore()->selectBar(partId, bar);
}
