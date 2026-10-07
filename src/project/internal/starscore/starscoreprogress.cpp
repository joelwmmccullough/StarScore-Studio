/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Progress colors: marking measures finished / needs review / unfinished (Joel, 7 Oct 2026). The marks live in the
 * score meta tag described in engraving/dom/starscoreprogress.h and are drawn only by the score view.
 */
#include "starscoreservice.h"

#include <QSettings>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/select.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/starscoreprogress.h"
#include "engraving/editing/undo.h"

#include "translation.h"

using namespace mu::project;
using namespace mu::engraving;
using namespace muse;

namespace {
//! Changes the marks as one undo step. Nothing is laid out again: only the score view draws them.
class ChangeProgressColors : public UndoCommand
{
    OBJECT_ALLOCATOR(engraving, ChangeProgressColors)

    MasterScore* m_score = nullptr;
    String m_text;

    void flip(EditData*) override
    {
        const String old = m_score->metaTag(String(mu::engraving::starscore::PROGRESS_TAG));
        if (m_text.isEmpty()) {
            m_score->metaTags().erase(String(mu::engraving::starscore::PROGRESS_TAG));
        } else {
            m_score->setMetaTag(String(mu::engraving::starscore::PROGRESS_TAG), m_text);
        }
        m_text = old;
    }

public:
    ChangeProgressColors(MasterScore* s, const String& t)
        : m_score(s), m_text(t) {}

    UNDO_TYPE(CommandType::ChangeMetaInfo)
    UNDO_NAME("ChangeProgressColors")
};

const char* PROGRESS_SETTING = "StarScore/progressColorsShown";
}

bool StarScoreService::progressColorsShown() const
{
    return QSettings().value(PROGRESS_SETTING, false).toBool();
}

void StarScoreService::setProgressColorsShown(bool shown)
{
    mu::engraving::starscore::progressColorsShown() = shown;
    if (QSettings().value(PROGRESS_SETTING, false).toBool() != shown) {
        QSettings().setValue(PROGRESS_SETTING, shown);
    }
    if (notation::INotationPtr n = globalContext()->currentNotation()) {
        n->notationChanged().notify();   // redraws the view
    }
    m_progressColorsShownChanged.notify();
}

muse::async::Notification StarScoreService::progressColorsShownChanged() const
{
    return m_progressColorsShownChanged;
}

QString StarScoreService::markProgress(char code)
{
    notation::INotationPtr n = globalContext()->currentNotation();
    Score* score = n && n->elements() ? n->elements()->msScore() : nullptr;
    MasterScore* master = score ? score->masterScore() : nullptr;
    if (!master) {
        return muse::qtrc("starscore", "Open a song first.");
    }

    // the measures and staves selected, in the score on screen (the full score or a part)
    std::vector<std::pair<const Measure*, staff_idx_t> > cells;
    const Selection& sel = score->selection();
    if (sel.isRange()) {
        const Fraction start = sel.tickStart();
        const Fraction end = sel.tickEnd();
        for (const Measure* m = score->tick2measure(start); m && m->tick() < end; m = m->nextMeasure()) {
            for (staff_idx_t s = sel.staffStart(); s < sel.staffEnd(); ++s) {
                cells.emplace_back(m, s);
            }
        }
    } else {
        for (const EngravingItem* e : sel.elements()) {
            if (const Measure* m = e->findMeasure(); m && e->staffIdx() < score->nstaves()) {
                cells.emplace_back(m, e->staffIdx());
            }
        }
    }
    if (cells.empty()) {
        return muse::qtrc("starscore", "Select the measures first: click a measure, then Shift+click to select more, "
                                       "on one staff or several.");
    }

    mu::engraving::starscore::ProgressMap map = mu::engraving::starscore::parseProgress(master->metaTag(String(mu::engraving::starscore::PROGRESS_TAG)).toStdString());
    int changed = 0;
    for (const auto& [m, s] : cells) {
        const Measure* mm = score == master ? m : master->tick2measure(m->tick());
        const Staff* st = score->staff(s);
        const Staff* ms = st && score != master ? st->findLinkedInScore(master) : st;
        if (!mm || !ms || !ms->part()) {
            continue;
        }
        EID eid = mm->eid();
        if (!eid.isValid()) {
            eid = mm->assignNewEID();   // saved with the measure from now on
        }
        const std::string key = mu::engraving::starscore::progressKey(eid.toStdString(), ms->part()->id().toStdString(), ms->rstaff());
        if (code == 0) {
            changed += int(map.erase(key));
        } else if (map[key] != code) {
            map[key] = code;
            ++changed;
        }
    }

    if (code != 0 && !progressColorsShown()) {
        setProgressColorsShown(true);   // marking with the colors hidden would show nothing
    }
    if (changed == 0) {
        return QString();
    }

    const String text = String::fromStdString(mu::engraving::starscore::writeProgress(map));
    n->undoStack()->prepareChanges(TranslatableString("starscore", "Progress colors"));
    master->undo(new ChangeProgressColors(master, text));
    n->undoStack()->commitChanges();
    n->notationChanged().notify();
    return QString();
}
