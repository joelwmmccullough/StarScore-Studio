/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "playerannotationmodel.h"

#include "translation.h"

using namespace mu::project;

PlayerAnnotationModel::PlayerAnnotationModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    if (starScore()) {
        m_target = starScore()->annotationTarget();
    }
}

QString PlayerAnnotationModel::selectionText() const
{
    if (m_target.count == 0) {
        return muse::qtrc("starscore", "No markings selected. Click staff text, a dynamic, a fingering, a breath mark, a line… "
                                       "(Ctrl+click or Cmd+click to add more), then come back here.");
    }
    QString text = muse::qtrc("starscore", "%n marking(s) selected on this sheet.", nullptr, m_target.count);
    if (!m_target.owners.isEmpty()) {
        text += " " + muse::qtrc("starscore", "Now marked as: %1.").arg(m_target.owners.join(", "));
    }
    if (m_target.inOtherScores > 0) {
        text += "\n\n" + muse::qtrc("starscore", "%n of them is also in the full score or other part scores. Marking takes "
                                                 "it out of those, so it is only on this sheet.", nullptr, m_target.inOtherScores);
    }
    return text;
}

QString PlayerAnnotationModel::add(const QString& player, const QString& text)
{
    return starScore() ? starScore()->addPlayerAnnotation(player, text) : QString();
}

QString PlayerAnnotationModel::mark(const QString& player)
{
    return starScore() ? starScore()->markPlayerAnnotations(player) : QString();
}
