/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "starscoreengraving.h"

#include <map>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/systemlock.h"
#include "engraving/editing/editsystemlocks.h"
#include "engraving/style/style.h"

using namespace mu::engraving;

namespace mu::project::starscore {
static Measure* starscoreMeasureStartingAt(Score* score, const Fraction& tick)
{
    Measure* m = score->tick2measure(tick);
    if (m && m->tick() == tick) {
        return m;
    }
    return nullptr;
}

static Measure* starscoreMeasureEndingAt(Score* score, const Fraction& endTick)
{
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->endTick() == endTick) {
            return m;
        }
        if (m->tick() >= endTick) {
            break;
        }
    }
    return nullptr;
}

LayoutCopyResult copyLayout(const Score* source, const std::vector<Score*>& targets, const LayoutCopyOptions& options)
{
    LayoutCopyResult result;
    if (!source) {
        return result;
    }

    std::map<int, const Measure*> sourceByTick;
    for (const Measure* m = source->firstMeasure(); m; m = m->nextMeasure()) {
        sourceByTick[m->tick().ticks()] = m;
    }

    for (Score* target : targets) {
        if (!target || target == source) {
            continue;
        }

        bool changed = false;

        auto applyBreak = [&](Measure* m, bool want, bool have, LayoutBreakType type) {
            if (want && !have) {
                m->undoSetBreak(true, type);
                ++result.breaksAdded;
                changed = true;
            } else if (!want && have && options.replaceExisting) {
                m->undoSetBreak(false, type);
                ++result.breaksRemoved;
                changed = true;
            }
        };

        for (Measure* m = target->firstMeasure(); m; m = m->nextMeasure()) {
            auto it = sourceByTick.find(m->tick().ticks());
            if (it == sourceByTick.end()) {
                continue;
            }
            const Measure* sm = it->second;

            if (options.pageBreaks) {
                applyBreak(m, sm->pageBreak(), m->pageBreak(), LayoutBreakType::PAGE);
            }
            if (options.lineBreaks) {
                applyBreak(m, sm->lineBreak(), m->lineBreak(), LayoutBreakType::LINE);
            }
            if (options.keepTogether) {
                applyBreak(m, sm->noBreak(), m->noBreak(), LayoutBreakType::NOBREAK);
            }
        }

        if (options.systemLocks) {
            const std::vector<const SystemLock*> sourceLocks = source->systemLocks()->allLocks();
            const bool targetHasLocks = !target->systemLocks()->allLocks().empty();

            if (options.replaceExisting && targetHasLocks) {
                EditSystemLocks::undoRemoveAllLocks(target);
                changed = true;
            }

            const bool mmrests = target->style().styleB(Sid::createMultiMeasureRests);

            for (const SystemLock* lock : sourceLocks) {
                if (!lock->startMB()->isMeasure() || !lock->endMB()->isMeasure()) {
                    continue;
                }

                Measure* a = starscoreMeasureStartingAt(target, lock->startMB()->tick());
                Measure* b = starscoreMeasureEndingAt(target, lock->endMB()->endTick());
                if (!a || !b) {
                    continue;
                }
                if (mmrests) {
                    a = a->coveringMMRestOrThis();
                    b = b->coveringMMRestOrThis();
                }
                if (!(a == b || a->isBefore(b))) {
                    continue;
                }
                if (target->systemLocks()->lockContaining(a) || target->systemLocks()->lockContaining(b)) {
                    continue;
                }

                EditSystemLocks::undoAddSystemLock(target, new SystemLock(a, b));
                ++result.locksCopied;
                changed = true;
            }
        }

        if (changed) {
            target->setLayoutAll();
            ++result.scoresChanged;
        }
    }

    return result;
}
}
