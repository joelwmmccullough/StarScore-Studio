/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "additivetimesigmodel.h"

#include <QRegularExpression>

#include "translation.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "notation/inotationelements.h"
#include "project/internal/starscore/starscoreengraving.h"

using namespace mu::project;
using namespace mu::engraving;

AdditiveTimeSigModel::AdditiveTimeSigModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

static void starscoreSelectedMeasures(Score* score, Measure*& first, Measure*& last)
{
    first = nullptr;
    last = nullptr;
    const Selection& sel = score->selection();
    if (sel.isRange()) {
        first = sel.startSegment() ? sel.startSegment()->measure() : nullptr;
        Segment* end = sel.endSegment();
        last = end ? end->measure() : score->lastMeasure();
        if (end && last && end->tick() == last->tick() && last->prevMeasure()) {
            last = last->prevMeasure();   // range ends at the start of this bar
        }
    } else if (EngravingItem* e = sel.element()) {
        if (e->findMeasure()) {
            first = e->findMeasure();
        }
    }
    if (first && first->isMMRest()) {
        first = first->mmRestFirst();
    }
    if (last && last->isMMRest()) {
        last = last->mmRestLast();
    }
}

QString AdditiveTimeSigModel::rangeText() const
{
    notation::INotationPtr notation = globalContext()->currentNotation();
    if (!notation) {
        return QString();
    }
    Measure* first = nullptr;
    Measure* last = nullptr;
    starscoreSelectedMeasures(notation->elements()->msScore(), first, last);
    if (first && last) {
        return muse::qtrc("starscore", "Applies to bars %1–%2 (the selection); the old time signature resumes after.")
               .arg(first->no() + 1).arg(last->no() + 1);
    }
    if (first) {
        return muse::qtrc("starscore", "Applies from bar %1 until the next time signature change or the end.").arg(first->no() + 1);
    }
    return muse::qtrc("starscore", "Nothing selected: applies from bar 1 until the next time signature change or the end.");
}

QString AdditiveTimeSigModel::apply(const QString& numeratorsText, int denominator)
{
    notation::INotationPtr notation = globalContext()->currentNotation();
    notation::IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!notation || !master) {
        return muse::qtrc("starscore", "Open a score first.");
    }

    std::vector<int> numerators;
    for (const QString& part : numeratorsText.split(QRegularExpression("[+\\s,]+"), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int n = part.toInt(&ok);
        if (!ok) {
            return muse::qtrc("starscore", "Write the top as numbers joined by +, e.g. 4+4+4+3.");
        }
        numerators.push_back(n);
    }
    if (numerators.empty()) {
        return muse::qtrc("starscore", "Write the top as numbers joined by +, e.g. 4+4+4+3.");
    }

    MasterScore* ms = master->masterScore();
    Measure* first = nullptr;
    Measure* last = nullptr;
    starscoreSelectedMeasures(notation->elements()->msScore(), first, last);

    // Selections in a part book: use the same bars in the main score
    if (first && first->score() != ms) {
        Measure* m = ms->tick2measure(first->tick());
        Measure* l = last ? ms->tick2measure(last->tick()) : nullptr;
        first = m;
        last = l;
    }
    if (!first) {
        first = ms->firstMeasure();
    }

    master->notation()->undoStack()->prepareChanges(muse::TranslatableString::untranslatable("Additive time signature"));
    const QString err = starscore::applyAdditiveTimeSig(ms, first, last, numerators, denominator);
    master->notation()->undoStack()->commitChanges();
    master->notation()->notationChanged().notify();
    notation->notationChanged().notify();

    return err;
}
