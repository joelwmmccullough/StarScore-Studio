/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "editsystemlocks.h"

#include <algorithm>

#include "undo.h"

#include "../dom/measurebase.h"
#include "../dom/score.h"
#include "../dom/system.h"
#include "../dom/systemlock.h"

using namespace mu::engraving;

//---------------------------------------------------------
//   AddSystemLock
//---------------------------------------------------------

class AddSystemLock : public UndoCommand
{
    OBJECT_ALLOCATOR(engraving, AddSystemLock)

    const SystemLock* m_systemLock;
public:
    AddSystemLock(const SystemLock* systemLock)
        : m_systemLock(systemLock) {}

    void undo(EditData*) override
    {
        Score* score = m_systemLock->startMB()->score();
        score->removeSystemLock(m_systemLock);
    }

    void redo(EditData*) override
    {
        Score* score = m_systemLock->startMB()->score();
        score->addSystemLock(m_systemLock);
    }

    void cleanup(bool undo) override
    {
        if (!undo) {
            delete m_systemLock;
            m_systemLock = nullptr;
        }
    }

    UNDO_NAME("AddSystemLock")

    std::vector<EngravingObject*> objectItems() const override
    {
        return { m_systemLock->startMB(), m_systemLock->endMB() };
    }
};

//---------------------------------------------------------
//   RemoveSystemLock
//---------------------------------------------------------

class RemoveSystemLock : public UndoCommand
{
    OBJECT_ALLOCATOR(engraving, RemoveSystemLock)

    const SystemLock* m_systemLock;
public:
    RemoveSystemLock(const SystemLock* systemLock)
        : m_systemLock(systemLock) {}

    void undo(EditData*) override
    {
        Score* score = m_systemLock->startMB()->score();
        score->addSystemLock(m_systemLock);
    }

    void redo(EditData*) override
    {
        Score* score = m_systemLock->startMB()->score();
        score->removeSystemLock(m_systemLock);
    }

    void cleanup(bool undo) override
    {
        if (undo) {
            delete m_systemLock;
            m_systemLock = nullptr;
        }
    }

    UNDO_NAME("RemoveSystemLock")
    std::vector<EngravingObject*> objectItems() const override
    {
        return { m_systemLock->startMB(), m_systemLock->endMB() };
    }
};

//---------------------------------------------------------
//   EditSystemLocks
//---------------------------------------------------------

void EditSystemLocks::undoAddSystemLock(Score* score, const SystemLock* lock)
{
    removeLayoutBreaksOnAddSystemLock(score, lock);
    score->undo(new AddSystemLock(lock));
}

void EditSystemLocks::undoRemoveSystemLock(Score* score, const SystemLock* lock)
{
    score->undo(new RemoveSystemLock(lock));
}

void EditSystemLocks::undoRemoveAllLocks(Score* score)
{
    std::vector<const SystemLock*> allLocks = score->systemLocks()->allLocks(); // copy
    for (const SystemLock* lock : allLocks) {
        undoRemoveSystemLock(score, lock);
    }
}

void EditSystemLocks::toggleSystemLock(Score* score, const std::vector<System*>& systems)
{
    bool unlockAll = true;
    for (const System* system : systems) {
        if (!system->isLocked()) {
            unlockAll = false;
            break;
        }
    }

    for (System* system : systems) {
        MeasureBase* startMeas = system->first();
        const SystemLock* currentLock = score->systemLocks()->lockStartingAt(startMeas);
        if (currentLock && unlockAll) {
            undoRemoveSystemLock(score, currentLock);
            continue;
        } else if (!currentLock && !unlockAll) {
            SystemLock* newSystemLock = new SystemLock(startMeas, system->last());
            undoAddSystemLock(score, newSystemLock);
        }
    }
}

void EditSystemLocks::toggleScoreLock(Score* score)
{
    bool unlockAll = true;
    for (const System* system : score->systems()) {
        const MeasureBase* first = system->first();
        if (!(first->isMeasure() || first->isHBox())) {
            continue;
        }
        if (!system->isLocked()) {
            unlockAll = false;
            break;
        }
    }

    for (System* system : score->systems()) {
        MeasureBase* startMeas = system->first();
        if (!(startMeas->isMeasure() || startMeas->isHBox())) {
            continue;
        }
        const SystemLock* currentLock = score->systemLocks()->lockStartingAt(startMeas);
        if (currentLock && unlockAll) {
            undoRemoveSystemLock(score, currentLock);
            continue;
        } else if (!currentLock && !unlockAll) {
            SystemLock* newSystemLock = new SystemLock(startMeas, system->last());
            undoAddSystemLock(score, newSystemLock);
        }
    }
}

void EditSystemLocks::addRemoveSystemLocks(Score* score, int interval, bool lock)
{
    const bool mmrests = score->style().styleB(Sid::createMultiMeasureRests);

    MeasureBase* startMeasure = score->selection().startMeasureBase();
    MeasureBase* endMeasure = score->selection().endMeasureBase();
    if (!endMeasure) {
        endMeasure = mmrests ? score->lastMeasureMM() : score->lastMeasure();
    }

    if (!startMeasure || !endMeasure) {
        return;
    }

    if (lock) {
        for (const System* system : score->systems()) {
            if (system->last()->isBefore(startMeasure)) {
                continue;
            }
            if (system->first()->isAfter(endMeasure)) {
                break;
            }
            if (!system->isLocked()) {
                undoAddSystemLock(score, new SystemLock(system->first(), system->last()));
            }
        }
        return;
    }

    std::vector<const SystemLock*> currentLocks = score->systemLocks()->locksContainedInRange(startMeasure, endMeasure);
    for (const SystemLock* l : currentLocks) {
        undoRemoveSystemLock(score, l);
    }

    if (interval == 0) {
        return;
    }

    int count = 0;
    MeasureBase* lockStart = nullptr;
    for (MeasureBase* mb = startMeasure; mb; mb = mmrests ? mb->nextMM() : mb->next()) {
        if (count == 0) {
            lockStart = mb;
        }
        count++;
        if (count == interval || mb == endMeasure) {
            undoAddSystemLock(score, new SystemLock(lockStart, mb));
            lockStart = nullptr;
            count = 0;
        }
        if (mb == endMeasure) {
            break;
        }
    }
}

void EditSystemLocks::makeIntoSystem(Score* score, MeasureBase* first, MeasureBase* last)
{
    bool mmrests = score->style().styleB(Sid::createMultiMeasureRests);

    const SystemLock* lockContainingfirst = score->systemLocks()->lockContaining(first);
    const SystemLock* lockContaininglast = score->systemLocks()->lockContaining(last);

    if (lockContainingfirst) {
        undoRemoveSystemLock(score, lockContainingfirst);
        if (lockContainingfirst->startMB()->isBefore(first)) {
            MeasureBase* oneBeforeFirst = mmrests ? first->prevMM() : first->prev();
            SystemLock* newLockBefore = new SystemLock(lockContainingfirst->startMB(), oneBeforeFirst);
            undoAddSystemLock(score, newLockBefore);
        }
    }

    if (lockContaininglast) {
        if (lockContaininglast != lockContainingfirst) {
            undoRemoveSystemLock(score, lockContaininglast);
        }
        if (last->isBefore(lockContaininglast->endMB())) {
            MeasureBase* oneAfterLast = mmrests ? last->nextMM() : last->next();
            SystemLock* newLockAfter = new SystemLock(oneAfterLast, lockContaininglast->endMB());
            undoAddSystemLock(score, newLockAfter);
        }
    }

    std::vector<const SystemLock*> locksContainedInRange = score->systemLocks()->locksContainedInRange(first, last);
    for (const SystemLock* lock : locksContainedInRange) {
        if (lock != lockContainingfirst && lock != lockContaininglast) {
            undoRemoveSystemLock(score, lock);
        }
    }

    SystemLock* newLock = new SystemLock(first, last);
    undoAddSystemLock(score, newLock);
}

void EditSystemLocks::moveMeasureToPrevSystem(Score* score, MeasureBase* m)
{
    const System* prevSystem = m->prevNonVBoxSystem();
    if (!prevSystem) {
        return;
    }

    MeasureBase* prevSystemFirstMeas = prevSystem->first();

    const SystemLock* prevSystemLock = score->systemLocks()->lockStartingAt(prevSystemFirstMeas);
    if (prevSystemLock) {
        undoRemoveSystemLock(score, prevSystemLock);
    }

    const System* curSystem = m->system();
    const SystemLock* curSystemLock = score->systemLocks()->lockStartingAt(curSystem->first());
    if (curSystemLock) {
        undoRemoveSystemLock(score, curSystemLock);
        if (curSystemLock->endMB() != m) {
            const bool mmrests = score->style().styleB(Sid::createMultiMeasureRests);
            MeasureBase* nextMB = mmrests ? m->nextMM() : m->next();
            SystemLock* newLockOnCurSystem = new SystemLock(nextMB, curSystemLock->endMB());
            undoAddSystemLock(score, newLockOnCurSystem);
        }
    }

    SystemLock* sysLock = new SystemLock(prevSystemFirstMeas, m);
    undoAddSystemLock(score, sysLock);
}

void EditSystemLocks::moveMeasureToNextSystem(Score* score, MeasureBase* m)
{
    const System* curSystem = m->system();
    MeasureBase* startMeas = curSystem->first();
    bool refMeasureIsStartOfSystem = m == startMeas;

    const SystemLock* curLock = score->systemLocks()->lockStartingAt(startMeas);
    if (curLock) {
        undoRemoveSystemLock(score, curLock);
    }

    if (!refMeasureIsStartOfSystem) {
        bool mmrests = score->style().styleB(Sid::createMultiMeasureRests);
        MeasureBase* prevMeas = mmrests ? m->prevMM() : m->prev();
        SystemLock* sysLock = new SystemLock(startMeas, prevMeas);
        undoAddSystemLock(score, sysLock);
    }

    const System* nextSystem = m->nextNonVBoxSystem();
    if (!nextSystem) {
        return;
    }

    const SystemLock* nextSysLock = score->systemLocks()->lockStartingAt(nextSystem->first());
    if (nextSysLock) {
        undoRemoveSystemLock(score, nextSysLock);
    }

    if (nextSysLock || refMeasureIsStartOfSystem) {
        SystemLock* newNextSysLock = new SystemLock(m, nextSystem->last());
        undoAddSystemLock(score, newNextSysLock);
    }
}

void EditSystemLocks::applyLockToSelection(Score* score)
{
    MeasureBase* first = nullptr;
    MeasureBase* last = nullptr;

    if (score->selection().isRange()) {
        first = score->selection().startMeasureBase();
        last = score->selection().endMeasureBase();
    } else {
        for (EngravingItem* el : score->selection().elements()) {
            if (el->isSystemLockIndicator()) {
                const SystemLock* lock = toSystemLockIndicator(el)->systemLock();
                first = lock->startMB();
                last = lock->endMB();
                break;
            }
            MeasureBase* mb = el->findMeasureBase();
            if (!mb) {
                continue;
            }
            if (!first || mb->isBefore(first)) {
                first = mb;
            }
            if (!last || mb->isAfter(last)) {
                last = mb;
            }
        }
    }

    if (!first || !last) {
        return;
    }

    const SystemLock* lockOnLast = score->systemLocks()->lockContaining(last);
    if (lockOnLast && lockOnLast->endMB() == last) {
        undoRemoveSystemLock(score, lockOnLast);
    } else if (first != last) {
        makeIntoSystem(score, first, last);
    } else {
        makeIntoSystem(score, first->system()->first(), last);
    }
}

void EditSystemLocks::removeSystemLocksOnAddLayoutBreak(Score* score, LayoutBreakType breakType, const MeasureBase* measure)
{
    IF_ASSERT_FAILED(breakType != LayoutBreakType::NOBREAK) {
        return; // NOBREAK not allowed on locked measures
    }

    const SystemLock* lock = score->systemLocks()->lockContaining(measure);
    if (lock && (breakType == LayoutBreakType::LINE || measure != lock->endMB())) {
        undoRemoveSystemLock(score, lock);
    }
}

void EditSystemLocks::removeLayoutBreaksOnAddSystemLock(Score* score, const SystemLock* lock)
{
    bool mmrests = score->style().styleB(Sid::createMultiMeasureRests);
    for (MeasureBase* mb = lock->startMB(); mb && mb->isBeforeOrEqual(lock->endMB()); mb = mmrests ? mb->nextMM() : mb->next()) {
        mb->undoSetBreak(false, LayoutBreakType::LINE);
        mb->undoSetBreak(false, LayoutBreakType::NOBREAK);
        if (mb != lock->endMB()) {
            mb->undoSetBreak(false, LayoutBreakType::SECTION);
            mb->undoSetBreak(false, LayoutBreakType::PAGE);
        }
    }
}

void EditSystemLocks::removeSystemLocksOnRemoveMeasures(Score* score, const MeasureBase* m1, const MeasureBase* m2)
{
    std::vector<const SystemLock*> allSysLocks = score->systemLocks()->allLocks();
    for (const SystemLock* lock : allSysLocks) {
        MeasureBase* lockStart = lock->startMB();
        MeasureBase* lockEnd = lock->endMB();
        bool lockStartIsInRange = lockStart->isAfterOrEqual(m1) && lockStart->isBeforeOrEqual(m2);
        bool lockEndIsInRange = lockEnd->isAfterOrEqual(m1) && lockEnd->isBeforeOrEqual(m2);
        if (lockStartIsInRange || lockEndIsInRange) {
            undoRemoveSystemLock(score, lock);
        }
        if (lockStartIsInRange && !lockEndIsInRange) {
            MeasureBase* newLockStart = m2->nextMeasure();
            if (newLockStart) {
                undoAddSystemLock(score, new SystemLock(newLockStart, lockEnd));
            }
        } else if (!lockStartIsInRange && lockEndIsInRange) {
            MeasureBase* newLockEnd = m1->prevMeasure();
            if (newLockEnd) {
                undoAddSystemLock(score, new SystemLock(lockStart, newLockEnd));
            }
        }
    }
}

//! StarScore: the bar where a lock really starts or ends: a multimeasure rest's first or last bar
static MeasureBase* starscoreUnderlying(MeasureBase* mb, bool start)
{
    if (mb && mb->isMeasure() && toMeasure(mb)->isMMRest()) {
        return start ? toMeasure(mb)->mmRestFirst() : toMeasure(mb)->mmRestLast();
    }
    return mb;
}

//---------------------------------------------------------
//   StarScore (Joel, 13 Oct 2026): where multimeasure rests moved or hid system locks, so switching them off puts the
//   locks back where they were. Kept in the score's meta tag "starscoreLockMemory" (saved with the file), one line per
//   lock: "M <start> <end> <original start> <original end>" for a lock moved to <start>-<end> (the ticks of its first
//   and last bar or multimeasure rest), "H <original start> <original end>" for a lock of rests a multimeasure rest
//   swallowed. Stale lines are harmless: a line is used only when its bars are still there.
//---------------------------------------------------------

static const muse::String LOCK_MEMORY_TAG = u"starscoreLockMemory";

struct StarscoreLockMemory {
    struct Moved {
        Fraction start, end, origStart, origEnd;
    };
    std::vector<Moved> moved;
    std::vector<std::pair<Fraction, Fraction> > hidden;
};

static StarscoreLockMemory starscoreReadLockMemory(const Score* score)
{
    StarscoreLockMemory mem;
    for (const muse::String& line : score->metaTag(LOCK_MEMORY_TAG).split(u'\n')) {
        const muse::StringList w = line.split(u' ');
        if (w.size() == 5 && w.at(0) == u"M") {
            mem.moved.push_back({ Fraction::fromString(w.at(1)), Fraction::fromString(w.at(2)),
                                  Fraction::fromString(w.at(3)), Fraction::fromString(w.at(4)) });
        } else if (w.size() == 3 && w.at(0) == u"H") {
            mem.hidden.emplace_back(Fraction::fromString(w.at(1)), Fraction::fromString(w.at(2)));
        }
    }
    return mem;
}

static void starscoreWriteLockMemory(Score* score, const StarscoreLockMemory& mem)
{
    muse::StringList lines;
    for (const auto& m : mem.moved) {
        lines << (u"M " + m.start.toString() + u" " + m.end.toString() + u" " + m.origStart.toString() + u" "
                  + m.origEnd.toString());
    }
    for (const auto& [a, b] : mem.hidden) {
        lines << (u"H " + a.toString() + u" " + b.toString());
    }
    const muse::String value = lines.join(u"\n");
    if (value != score->metaTag(LOCK_MEMORY_TAG)) {
        score->setMetaTag(LOCK_MEMORY_TAG, value);
    }
}

//! A lock moved from `old` to `moved`: remembered at the place it had before its first move
static void starscoreRememberMove(Score* score, const SystemLock* old, const SystemLock* moved)
{
    StarscoreLockMemory mem = starscoreReadLockMemory(score);
    Fraction origStart = starscoreUnderlying(old->startMB(), true)->tick();
    Fraction origEnd = starscoreUnderlying(old->endMB(), false)->tick();
    for (auto it = mem.moved.begin(); it != mem.moved.end(); ++it) {
        if (it->start == old->startMB()->tick() && it->end == old->endMB()->tick()) {
            origStart = it->origStart;   // moved before: still from where it first was
            origEnd = it->origEnd;
            mem.moved.erase(it);
            break;
        }
    }
    // (a lock that only now starts or ends at the multimeasure rest on its own first or last bar is where it was)
    if (origStart != moved->startMB()->tick() || origEnd != moved->endMB()->tick()) {
        mem.moved.push_back({ moved->startMB()->tick(), moved->endMB()->tick(), origStart, origEnd });
    }
    starscoreWriteLockMemory(score, mem);
}

void EditSystemLocks::removeSystemLocksContainingMMRests(Score* score)
{
    // StarScore (Joel, 13 Oct 2026): switching multimeasure rests off keeps the system locks. MuseScore removed every
    // lock holding a multimeasure rest, so pressing M twice lost the locked systems around rests. A lock that
    // multimeasure rests moved goes back to where it was, one they swallowed comes back; a lock that otherwise starts
    // or ends at a multimeasure rest starts at its first bar or ends at its last. The breaks are left as they are.
    const StarscoreLockMemory mem = starscoreReadLockMemory(score);
    auto barAt = [&](const Fraction& tick) -> MeasureBase* {
        Measure* m = score->tick2measure(tick);
        return m && m->tick() == tick ? m : nullptr;
    };
    std::vector<const SystemLock*> allLocks = score->systemLocks()->allLocks(); // copy
    std::vector<std::pair<MeasureBase*, MeasureBase*> > restored;
    for (const SystemLock* lock : allLocks) {
        MeasureBase* start = nullptr;
        MeasureBase* end = nullptr;
        for (const auto& m : mem.moved) {
            if (m.start == lock->startMB()->tick() && m.end == lock->endMB()->tick()) {
                start = barAt(m.origStart);
                end = barAt(m.origEnd);
                break;
            }
        }
        if (!start || !end || end->isBefore(start)) {
            start = starscoreUnderlying(lock->startMB(), true);
            end = starscoreUnderlying(lock->endMB(), false);
        }
        if (!start || !end) {
            undoRemoveSystemLock(score, lock);
            continue;
        }
        if (start != lock->startMB() || end != lock->endMB()) {
            undoRemoveSystemLock(score, lock);
            restored.emplace_back(start, end);
        }
    }
    for (const auto& [a, b] : mem.hidden) {
        MeasureBase* start = barAt(a);
        MeasureBase* end = barAt(b);
        if (start && end && !end->isBefore(start)) {
            restored.emplace_back(start, end);
        }
    }
    // (added after all the removals: a lock put back may reach where a moved neighbour started)
    for (const auto& [start, end] : restored) {
        bool overlaps = false;
        for (const SystemLock* other : score->systemLocks()->allLocks()) {
            overlaps |= !(other->endMB()->isBefore(start) || end->isBefore(other->startMB()));
        }
        if (!overlaps) {
            score->undo(new AddSystemLock(new SystemLock(start, end)));
        }
    }
    if (!score->metaTag(LOCK_MEMORY_TAG).isEmpty()) {
        score->setMetaTag(LOCK_MEMORY_TAG, muse::String());
    }
}

void EditSystemLocks::updateSystemLocksOnCreateMMRests(Score* score, Measure* first, Measure* last)
{
    // NOTE: this must be done during layout as the mmRests get created.

    for (const SystemLock* lock : score->systemLocks()->locksContainedInRange(first, last)) {
        // StarScore: a locked system of just these bars of rest becomes a locked system of the multimeasure rest
        // (MuseScore removed it, and it was gone when multimeasure rests were switched off again); any other lock
        // inside the multimeasure rest is removed but remembered, and put back when they are switched off
        const bool whole = lock->startMB()->tick() == first->tick() && lock->endMB()->endTick() == last->endTick();
        if (whole && first->mmRest()) {
            SystemLock* moved = new SystemLock(first->mmRest(), first->mmRest());
            starscoreRememberMove(score, lock, moved);
            undoRemoveSystemLock(score, lock);
            score->undo(new AddSystemLock(moved));
            return;
        }
        StarscoreLockMemory mem = starscoreReadLockMemory(score);
        const std::pair<Fraction, Fraction> h { starscoreUnderlying(lock->startMB(), true)->tick(),
                                                starscoreUnderlying(lock->endMB(), false)->tick() };
        if (std::find(mem.hidden.begin(), mem.hidden.end(), h) == mem.hidden.end()) {
            mem.hidden.push_back(h);
            starscoreWriteLockMemory(score, mem);
        }
        undoRemoveSystemLock(score, lock);
    }

    const SystemLock* lockOnFirst = score->systemLocks()->lockContaining(first);
    const SystemLock* lockOnLast = score->systemLocks()->lockContaining(last);

    if (lockOnFirst) {
        MeasureBase* startMB = lockOnFirst->startMB();
        MeasureBase* endMB = lockOnFirst->endMB();

        if (startMB->isBefore(first)) {
            if (endMB->isBeforeOrEqual(last)) {
                endMB = first->mmRest();
            } else {
                return;
            }
        } else {
            startMB = first->mmRest();
        }

        if (startMB != lockOnFirst->startMB() || endMB != lockOnFirst->endMB()) {
            SystemLock* moved = new SystemLock(startMB, endMB);
            starscoreRememberMove(score, lockOnFirst, moved);
            undoRemoveSystemLock(score, lockOnFirst);
            undoAddSystemLock(score, moved);
        }
    }

    if (!lockOnLast || lockOnLast == lockOnFirst) {
        return;
    }

    MeasureBase* startMB = lockOnLast->startMB();
    MeasureBase* endMB = lockOnLast->endMB();
    assert(startMB->isAfter(first) && endMB->isAfter(last));

    startMB = last->nextMM();
    SystemLock* moved = new SystemLock(startMB, endMB);
    starscoreRememberMove(score, lockOnLast, moved);
    undoRemoveSystemLock(score, lockOnLast);
    undoAddSystemLock(score, moved);
}
