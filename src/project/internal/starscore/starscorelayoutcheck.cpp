/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — checks on a laid-out sheet before it's printed (see starscorelayoutcheck.h)
 */
#include "starscorelayoutcheck.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include <QRegularExpression>

#include "engraving/dom/score.h"
#include "engraving/dom/page.h"
#include "engraving/dom/system.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/textbase.h"
#include "engraving/dom/measurenumber.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/beam.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/note.h"
#include "engraving/dom/accidental.h"
#include "engraving/editing/editproperty.h"

#include "log.h"

using namespace mu::engraving;

namespace mu::project::starscore {
namespace {
struct PlacedText {
    EngravingItem* item = nullptr;
    const Page* page = nullptr;
    int pageNo = 0;
    int bar = 0;
    RectF rect;   // page coordinates
};

//! A text written above or below the music, shown, with words in it
bool checkedText(const EngravingItem* e)
{
    if (!e || !e->visible() || !e->isTextBase() || !e->ldata()) {
        return false;
    }
    if (!(e->isStaffText() || e->isSystemText() || e->isExpression() || e->isRehearsalMark() || e->isTempoText()
          || e->isHarmony() || e->isMarker() || e->isJump() || e->isPlayTechAnnotation() || e->isInstrumentChange())) {
        return false;
    }
    return !toTextBase(e)->plainText().trimmed().isEmpty();
}

std::vector<PlacedText> placedTexts(const Score* score)
{
    std::vector<PlacedText> out;
    int pageNo = 0;
    for (const Page* page : score->pages()) {
        ++pageNo;
        for (const System* system : page->systems()) {
            for (MeasureBase* mb : system->measures()) {
                if (!mb->isMeasure()) {
                    continue;
                }
                Measure* m = toMeasure(mb);
                const int bar = m->no() + 1;
                auto add = [&](EngravingItem* e) {
                    if (checkedText(e) && e->staff() && e->staff()->show()) {
                        out.push_back({ e, page, pageNo, bar, e->pageBoundingRect() });
                    }
                };
                for (EngravingItem* e : m->el()) {
                    add(e);
                }
                // bar numbers: never moved, but a text must stay clear of them
                for (staff_idx_t si = 0; si < score->nstaves(); ++si) {
                    EngravingItem* no = m->measureNumber(si);
                    if (no && no->visible() && no->ldata() && no->staff() && no->staff()->show()
                        && !toTextBase(no)->plainText().isEmpty()) {
                        out.push_back({ no, page, pageNo, bar, no->pageBoundingRect() });
                    }
                }
                for (Segment* seg = m->first(); seg; seg = seg->next()) {
                    for (EngravingItem* e : seg->annotations()) {
                        add(e);
                    }
                }
            }
        }
    }
    return out;
}

//! Each page's texts as a run [first, end) of indices into `texts`: placedTexts lists them page by page, and the
//! checks below only ever compare two texts on the same page, so each page is scanned on its own (the same pairs
//! in the same order as one scan over everything, at a fraction of the comparisons on a big Score)
std::vector<std::pair<size_t, size_t> > pageRuns(const std::vector<PlacedText>& texts)
{
    std::vector<std::pair<size_t, size_t> > runs;
    for (size_t i = 0; i < texts.size();) {
        size_t j = i + 1;
        while (j < texts.size() && texts[j].page == texts[i].page) {
            ++j;
        }
        runs.emplace_back(i, j);
        i = j;
    }
    return runs;
}

QString wordsOf(const EngravingItem* e)
{
    QString w = toTextBase(e)->plainText().toQString().simplified();
    w = w.size() > 40 ? w.left(39) + QString::fromUtf8("…") : w;
    // where it hangs, for the log: staff, above/below, autoplace, offset (spatium)
    const double sp = e->spatium();
    return w + QString(" [st%1 %2 %3 off %4,%5 pos %6,%7]").arg(e->staffIdx()).arg(e->placeBelow() ? "below" : "above")
           .arg(e->autoplace() ? "auto" : "fixed").arg(e->offset().x() / sp, 0, 'f', 1).arg(e->offset().y() / sp, 0, 'f', 1)
           .arg(e->ldata()->pos().x() / sp, 0, 'f', 1).arg(e->ldata()->pos().y() / sp, 0, 'f', 1);
}

//! The same text twice in the same place (a rehearsal mark shown on two staves at one spot): prints as one
bool sameText(const PlacedText& a, const PlacedText& b)
{
    return a.item->type() == b.item->type() && a.rect == b.rect
           && toTextBase(a.item)->plainText() == toTextBase(b.item)->plainText();
}

//! How far a text runs off the page: negative to move it right, positive to move it left, 0 when inside
double offPageBy(const PlacedText& t)
{
    const double width = t.page->ldata()->bbox().width();
    const double left = t.page->lm() * 0.4;
    const double right = width - t.page->rm() * 0.4;
    if (t.rect.left() < left) {
        return t.rect.left() - t.page->lm();
    }
    if (t.rect.right() > right) {
        return t.rect.right() - (width - t.page->rm());
    }
    return 0.0;
}
}

//! A rest inside a beam printed over the next or previous note's accidental or head, as laid out now
struct BeamedRestHit {
    int pageNo = 0;
    Measure* measure = nullptr;
    QString what;   // "accidental", "note"
    Rest* rest = nullptr;
    double moveBy = 0.0;    // sideways, to clear what it is over (negative: to the left)
    double room = 0.0;      // free space on that side, up to the neighbour there
    RectF over;             // what it is over (the lowest top / highest bottom of them, for a move up or down)
};

static std::vector<BeamedRestHit> beamedRestHits(const Score* score)
{
    std::vector<BeamedRestHit> hits;
    const double sp = score->style().spatium();
    int pageNo = 0;
    for (const Page* page : score->pages()) {
        ++pageNo;
        for (const System* sys : page->systems()) {
            for (MeasureBase* mb : sys->measures()) {
                if (!mb->isMeasure()) {
                    continue;
                }
                Measure* m = toMeasure(mb);
                for (const Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                    for (EngravingItem* e : seg->elist()) {
                        if (!e || !e->isRest() || !e->visible() || !toRest(e)->beam() || !e->ldata()) {
                            continue;
                        }
                        const RectF full = e->pageBoundingRect();
                        const RectF r = full.adjusted(0.05 * sp, 0.05 * sp, -0.05 * sp, -0.05 * sp);
                        QString hit;
                        double moveBy = 0.0;
                        RectF over;
                        // the nearest right edge of the previous chord, the nearest left edge of the next one
                        double leftLimit = -1e9, rightLimit = 1e9;
                        bool hitBefore = false;
                        for (const Segment* nb : { seg->prev1(SegmentType::ChordRest), seg->next1(SegmentType::ChordRest) }) {
                            if (!nb || nb->system() != seg->system()) {
                                continue;
                            }
                            const bool before = nb->tick() < seg->tick();
                            for (const EngravingItem* o : nb->elist()) {
                                if (!o || !o->isChord() || o->vStaffIdx() != e->vStaffIdx()) {
                                    continue;
                                }
                                for (const Note* n : toChord(o)->notes()) {
                                    std::vector<RectF> parts;
                                    if (n->visible() && n->ldata()) {
                                        parts.push_back(n->pageBoundingRect());
                                    }
                                    if (n->accidental() && n->accidental()->visible() && n->accidental()->ldata()) {
                                        parts.push_back(n->accidental()->pageBoundingRect());
                                    }
                                    for (const RectF& pr : parts) {
                                        if (before) {
                                            leftLimit = std::max(leftLimit, pr.right());
                                        } else {
                                            rightLimit = std::min(rightLimit, pr.left());
                                        }
                                        if (!r.intersects(pr)) {
                                            continue;
                                        }
                                        const bool acc = pr == parts.back() && n->accidental() && parts.size() > 1;
                                        if (acc || hit.isEmpty()) {
                                            hit = acc ? QString("accidental") : QString("note");
                                        }
                                        hitBefore = before;
                                        over = over.isNull() ? pr : over.united(pr);
                                        moveBy = before ? std::max(moveBy, pr.right() - full.left() + 0.15 * sp)
                                                 : std::min(moveBy, pr.left() - full.right() - 0.15 * sp);
                                    }
                                }
                            }
                        }
                        if (!hit.isEmpty()) {
                            const double room = hitBefore ? rightLimit - full.right() - 0.15 * sp
                                                : full.left() - leftLimit - 0.15 * sp;
                            hits.push_back({ pageNo, m, hit, toRest(e), moveBy, room, over });
                        }
                    }
                }
            }
        }
    }
    return hits;
}

std::vector<LayoutIssue> layoutIssues(const Score* score)
{
    std::vector<LayoutIssue> issues;
    if (!score) {
        return issues;
    }
    const std::vector<PlacedText> texts = placedTexts(score);
    const double sp = score->style().spatium();
    for (const PlacedText& t : texts) {
        if (offPageBy(t) != 0.0) {
            issues.push_back({ t.pageNo, t.bar, QString("off page"), QString("%1 \"%2\"").arg(t.item->typeName(), wordsOf(t.item)) });
        }
    }
    for (const auto& [first, end] : pageRuns(texts)) {
        for (size_t i = first; i < end; ++i) {
            for (size_t j = i + 1; j < end; ++j) {
                const PlacedText& a = texts[i];
                const PlacedText& b = texts[j];
                if (a.page != b.page || a.item == b.item || sameText(a, b)) {
                    continue;
                }
                const RectF in = a.rect.intersected(b.rect);
                if (in.isEmpty() || in.width() < 0.3 * sp || in.height() < 0.3 * std::min(a.rect.height(), b.rect.height())) {
                    continue;
                }
                issues.push_back({ a.pageNo, a.bar, QString("overlap"),
                                   QString("%1 \"%2\" / %3 \"%4\"").arg(a.item->typeName(), wordsOf(a.item), b.item->typeName(),
                                                                        wordsOf(b.item)) });
            }
        }
    }
    // a rest inside a beam printed over the next or previous note's accidental or head (Joel, 17 Oct 2026)
    for (const BeamedRestHit& h : beamedRestHits(score)) {
        issues.push_back({ h.pageNo, h.measure->no() + 1, QString("rest on a note"), QString("beamed rest over a %1").arg(h.what) });
    }
    return issues;
}

int keepTextsOnPage(Score* score)
{
    if (!score) {
        return 0;
    }
    int moved = 0;
    auto here = [](EngravingItem* e, Pid pid, const PropertyValue& v) {
        // on this one text only: through undoChangeProperty a part score's change would go to the main score and every
        // other part score too
        e->score()->undo(new ChangeProperty(e, pid, v, PropertyFlags::UNSTYLED));
        if (!e->score()->isMaster()) {
            e->unlinkPropertyFromMaster(pid);
        }
    };
    // 0. a text wider than the page's printed width: broken over lines of about equal length (Joel, 16 Oct 2026:
    // February's "Jam on the same groove for 16 bars! I've provided the melody line…" ran into both margins). Only
    // plain texts, without formatting, and only between words.
    int wrapped = 0;
    for (const PlacedText& t : placedTexts(score)) {
        if (t.item->isHarmony() || t.item->isMeasureNumber() || t.item->isRehearsalMark()) {
            continue;
        }
        const double usable = t.page->ldata()->bbox().width() - t.page->lm() - t.page->rm();
        TextBase* tb = toTextBase(t.item);
        const QString xml = tb->xmlText().toQString();
        if (t.rect.width() <= usable || xml.contains('<') || xml.contains('\n') || !xml.contains(' ')) {
            continue;
        }
        const int lines = int(std::ceil(t.rect.width() / (usable * 0.95)));
        const QStringList words = xml.split(' ', Qt::SkipEmptyParts);
        const int target = int(std::ceil(double(xml.size()) / lines));
        QStringList out;
        QString line;
        for (const QString& w : words) {
            if (!line.isEmpty() && line.size() + 1 + w.size() > target && out.size() < lines - 1) {
                out << line;
                line = w;
            } else {
                line = line.isEmpty() ? w : line + " " + w;
            }
        }
        out << line;
        if (out.size() > 1) {
            here(tb, Pid::TEXT, PropertyValue(String::fromQString(out.join('\n'))));
            ++wrapped;
        }
    }
    if (wrapped) {
        score->setLayoutAll();
        score->doLayout();
    }
    // 1. a text centred on a note at the start of a line runs left off the page: it starts at the note instead (and one
    // at the end of a line ends at it), so MuseScore still keeps it clear of the chord symbols and texts there; an
    // offset to the side would leave autoplace looking at the wrong place (Joel, 16 Oct 2026: Amplitudes' "Play as
    // written or improvise, your choice" moved onto the page landed on the chord symbols)
    std::set<EngravingItem*> realigned;
    for (const PlacedText& t : placedTexts(score)) {
        const double dx = offPageBy(t);
        if (dx == 0.0 || t.item->isHarmony() || t.item->isMeasureNumber()) {
            continue;
        }
        TextBase* tb = toTextBase(t.item);
        const AlignH side = dx < 0 ? AlignH::LEFT : AlignH::RIGHT;
        if (tb->position() == side && tb->align().horizontal == side) {
            continue;
        }
        here(tb, Pid::POSITION, PropertyValue::fromValue(side));
        here(tb, Pid::ALIGN, PropertyValue::fromValue(Align(side, tb->align().vertical)));
        if (tb->offset().x() != 0.0) {
            here(tb, Pid::OFFSET, PropertyValue(PointF(0.0, tb->offset().y())));
        }
        realigned.insert(tb);
        ++moved;
    }
    if (!realigned.empty()) {
        score->setLayoutAll();
        score->doLayout();
    }
    // 2. still off the page (an offset set by hand, a very long text): moved in to the page margin
    int shifted = 0;
    for (const PlacedText& t : placedTexts(score)) {
        const double dx = offPageBy(t);
        if (dx == 0.0 || t.item->isMeasureNumber()) {   // (a chord symbol too: moved along its line)
            continue;
        }
        here(t.item, Pid::OFFSET, PropertyValue(t.item->offset() - PointF(dx, 0.0)));
        ++shifted;
    }
    if (shifted) {
        score->setLayoutAll();
        score->doLayout();
    }
    return wrapped + moved + shifted;
}

int resetTextPositions(Score* score)
{
    if (!score) {
        return 0;
    }
    int changed = 0;
    auto reset = [&](EngravingItem* e) {
        if (!e || !e->isTextBase() || !(e->isStaffText() || e->isSystemText() || e->isExpression() || e->isRehearsalMark()
                                         || e->isTempoText() || e->isHarmony() || e->isPlayTechAnnotation()
                                         || e->isDynamic())) {
            return;
        }
        bool any = false;
        for (Pid pid : { Pid::OFFSET, Pid::AUTOPLACE }) {
            const PropertyValue def = e->propertyDefault(pid);
            if (def.isValid() && e->getProperty(pid) != def) {
                e->score()->undo(new ChangeProperty(e, pid, def, PropertyFlags::STYLED));
                if (!e->score()->isMaster()) {
                    e->unlinkPropertyFromMaster(pid);
                }
                any = true;
            }
        }
        changed += any ? 1 : 0;
    };
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (Segment* seg = m->first(); seg; seg = seg->next()) {
            for (EngravingItem* e : seg->annotations()) {
                reset(e);
            }
        }
    }
    return changed;
}

namespace {
//! Changes a property on this one item only (a part score's change would otherwise go to the main score and every
//! other part score)
void changeHere(EngravingItem* e, Pid pid, const PropertyValue& v, PropertyFlags flags = PropertyFlags::UNSTYLED)
{
    e->score()->undo(new ChangeProperty(e, pid, v, flags));
    if (!e->score()->isMaster()) {
        e->unlinkPropertyFromMaster(pid);
    }
}

bool isCombinedLabel(const EngravingItem* e)
{
    if (!e || !e->isStaffText()) {
        return false;
    }
    static const QRegularExpression label(QString::fromUtf8("^(All|\\d+H(–\\d+H)?([ ,/]+\\d+H(–\\d+H)?)*)$"));
    return label.match(toTextBase(e)->plainText().toQString().simplified()).hasMatch();
}
}

int alignCombinedLabels(Score* score)
{
    if (!score) {
        return 0;
    }
    // each line of music's labels above and below the staff: (system, below) -> labels
    std::map<std::pair<const System*, bool>, std::vector<PlacedText> > groups;
    for (const PlacedText& t : placedTexts(score)) {
        const System* sys = t.item->findMeasure() ? t.item->findMeasure()->system() : nullptr;
        if (sys && isCombinedLabel(t.item)) {
            groups[{ sys, t.item->placeBelow() }].push_back(t);
        }
    }
    // the height each label goes to: the highest label's top (the lowest one's bottom below the staff)
    std::map<EngravingItem*, std::pair<double, bool> > target;   // label -> (edge, below)
    for (const auto& [key, labels] : groups) {
        if (labels.size() < 2) {
            continue;
        }
        const bool below = key.second;
        double edge = below ? labels.front().rect.bottom() : labels.front().rect.top();
        for (const PlacedText& t : labels) {
            edge = below ? std::max(edge, t.rect.bottom()) : std::min(edge, t.rect.top());
        }
        for (const PlacedText& t : labels) {
            const double now = below ? t.rect.bottom() : t.rect.top();
            if (std::abs(now - edge) >= 0.05 * score->style().spatium()) {
                target[t.item] = { edge, below };
            }
        }
    }
    if (target.empty()) {
        return 0;
    }
    // held there, autoplace off: autoplace puts a label back at its own height whatever its offset
    for (const auto& [e, where] : target) {
        if (e->autoplace()) {
            changeHere(e, Pid::AUTOPLACE, PropertyValue(false));
        }
    }
    score->setLayoutAll();
    score->doLayout();
    int moved = 0;
    for (const PlacedText& t : placedTexts(score)) {
        auto it = target.find(t.item);
        if (it == target.end()) {
            continue;
        }
        const double dy = it->second.second ? it->second.first - t.rect.bottom() : it->second.first - t.rect.top();
        if (std::abs(dy) > 0.001) {
            changeHere(t.item, Pid::OFFSET, PropertyValue(t.item->offset() + PointF(0.0, dy)));
        }
        ++moved;
    }
    score->setLayoutAll();
    score->doLayout();
    return moved;
}

int separateOverlappingTexts(Score* score)
{
    if (!score) {
        return 0;
    }
    auto movable = [](const EngravingItem* e) {
        // (a tempo marking or rehearsal mark too: Joel's are often placed by hand, and one can sit on the first chord)
        return e->isStaffText() || e->isSystemText() || e->isExpression() || e->isPlayTechAnnotation() || e->isInstrumentChange()
               || e->isTempoText() || e->isRehearsalMark();
    };
    auto overlapping = [&](std::vector<std::pair<PlacedText, PlacedText> >& pairs) {
        pairs.clear();
        const std::vector<PlacedText> texts = placedTexts(score);
        const double sp = score->style().spatium();
        for (const auto& [first, end] : pageRuns(texts)) {
            for (size_t i = first; i < end; ++i) {
                if (!movable(texts[i].item)) {
                    continue;
                }
                for (size_t j = first; j < end; ++j) {
                    const PlacedText& a = texts[i];
                    const PlacedText& b = texts[j];
                    if (i == j || a.page != b.page) {
                        continue;
                    }
                    // Only a text over a chord symbol or a bar number, or a Combined Parts label over anything, is
                    // moved (the label moving). Two texts placed by hand over each other are left as they are:
                    // stacking them made them harder to read (Deep Speech's Keys sheet: "Solo #1", "Repeat to cue"
                    // and the F mark).
                    const bool overThing = b.item->isHarmony() || b.item->isMeasureNumber();
                    const bool labelA = isCombinedLabel(a.item);
                    const bool labelB = isCombinedLabel(b.item);
                    // Two texts MuseScore placed itself (autoplace on) that still overlap (it doesn't keep texts on
                    // one beat apart: Fish Oil's "Repeat to cue" and "Note: rhythm here is 8+6, not 7+7!"): the
                    // later one moves; a rehearsal mark stays where it is
                    const bool bothAuto = a.item->autoplace() && b.item->autoplace() && !a.item->isRehearsalMark()
                                          && (b.item->isRehearsalMark() || j > i);
                    if (!(overThing || (labelA && !labelB) || (labelA && labelB && j > i) || bothAuto)) {
                        continue;
                    }
                    if (sameText(a, b)) {
                        continue;
                    }
                    const RectF in = a.rect.intersected(b.rect);
                    if (!in.isEmpty() && in.width() >= 0.3 * sp
                        && in.height() >= 0.3 * std::min(a.rect.height(), b.rect.height())) {
                        pairs.emplace_back(a, b);
                    }
                }
            }
        }
    };
    int moved = 0;
    std::vector<std::pair<PlacedText, PlacedText> > pairs;
    // 0. the same words twice almost on top of each other (two copies of a system text on a keyboard sheet's two
    // staves): one is hidden on this sheet, or it prints smudged
    {
        const std::vector<PlacedText> texts = placedTexts(score);
        std::set<EngravingItem*> hidden;
        for (const auto& [first, end] : pageRuns(texts)) {
            for (size_t i = first; i < end; ++i) {
                for (size_t j = i + 1; j < end; ++j) {
                    const PlacedText& a = texts[i];
                    const PlacedText& b = texts[j];
                    if (a.page != b.page || a.item->type() != b.item->type() || hidden.count(a.item) || hidden.count(b.item)
                        || !movable(b.item) || toTextBase(a.item)->plainText() != toTextBase(b.item)->plainText()) {
                        continue;
                    }
                    const RectF in = a.rect.intersected(b.rect);
                    const double area = in.isEmpty() ? 0.0 : in.width() * in.height();
                    if (area > 0.5 * a.rect.width() * a.rect.height() && area > 0.5 * b.rect.width() * b.rect.height()
                        && a.rect != b.rect) {
                        changeHere(b.item, Pid::VISIBLE, PropertyValue(false));
                        hidden.insert(b.item);
                    }
                }
            }
        }
        if (!hidden.empty()) {
            moved += int(hidden.size());
            score->setLayoutAll();
            score->doLayout();
        }
    }
    // 1. a text placed by hand: back to the style's place, MuseScore's autoplace keeping it clear
    overlapping(pairs);
    std::set<EngravingItem*> reset;
    for (const auto& [a, b] : pairs) {
        EngravingItem* e = a.item;
        if (reset.count(e)) {
            continue;
        }
        // (autoplace only: the offset may be what keeps it on the page)
        if (!e->autoplace()) {
            changeHere(e, Pid::AUTOPLACE, PropertyValue(true));
            reset.insert(e);
            ++moved;
        }
    }
    if (!reset.empty()) {
        score->setLayoutAll();
        score->doLayout();
    }
    // 2. still over another text (on another staff, which autoplace doesn't look at; or a chord symbol lifted into it
    // when the line's chord symbols are levelled after autoplace): held where it is (autoplace off: it would put the
    // text back at the same height whatever its offset), then moved clear of the other text, away from its staff
    overlapping(pairs);
    std::set<EngravingItem*> held;
    for (const auto& [a, b] : pairs) {
        if (a.item->autoplace() && !held.count(a.item)) {
            held.insert(a.item);
        }
    }
    if (!held.empty()) {
        // the place autoplace gave it, as an offset: where it is minus where it is without autoplace
        std::map<EngravingItem*, double> placedY;
        for (const PlacedText& t : placedTexts(score)) {
            if (held.count(t.item)) {
                placedY[t.item] = t.rect.top();
            }
        }
        for (EngravingItem* e : held) {
            changeHere(e, Pid::AUTOPLACE, PropertyValue(false));
        }
        score->setLayoutAll();
        score->doLayout();
        for (const PlacedText& t : placedTexts(score)) {
            auto it = placedY.find(t.item);
            if (it != placedY.end() && std::abs(it->second - t.rect.top()) > 0.01) {
                changeHere(t.item, Pid::OFFSET, PropertyValue(t.item->offset() + PointF(0.0, it->second - t.rect.top())));
            }
        }
        score->setLayoutAll();
        score->doLayout();
    }
    std::set<EngravingItem*> everShifted;
    const double gap = 0.3 * score->style().spatium();
    for (int round = 0; round < 3; ++round) {
        overlapping(pairs);
        std::set<EngravingItem*> shifted;
        for (const auto& [a, b] : pairs) {
            EngravingItem* e = a.item;
            if (shifted.count(e)) {
                continue;
            }
            const double dy = e->placeBelow() ? b.rect.bottom() + gap - a.rect.top() : b.rect.top() - gap - a.rect.bottom();
            changeHere(e, Pid::OFFSET, PropertyValue(e->offset() + PointF(0.0, dy)));
            shifted.insert(e);
            everShifted.insert(e);
        }
        if (shifted.empty()) {
            break;
        }
        score->setLayoutAll();
        score->doLayout();
    }
    return moved + int(everShifted.size());
}

int spaceOutBeamedRests(Score* score)
{
    if (!score) {
        return 0;
    }
    // first sideways, into the free space on its other side, when there is enough of it; then, for those that still
    // touch, their bars a little wider, and sideways again
    int nudged = 0;
    auto nudge = [&]() {
        bool any = false;
        for (const BeamedRestHit& h : beamedRestHits(score)) {
            if (h.rest && std::abs(h.moveBy) <= h.room) {
                const PointF off = h.rest->offset() + PointF(h.moveBy, 0.0);
                score->undo(new ChangeProperty(h.rest, Pid::OFFSET, PropertyValue(off), PropertyFlags::UNSTYLED));
                if (!score->isMaster()) {
                    h.rest->unlinkPropertyFromMaster(Pid::OFFSET);
                }
                ++nudged;
                any = true;
            }
        }
        if (any) {
            score->setLayoutAll();
            score->doLayout();
        }
    };
    nudge();
    std::set<Measure*> widened;
    for (int round = 0; round < 4; ++round) {
        const std::vector<BeamedRestHit> hits = beamedRestHits(score);
        std::set<Measure*> now;
        for (const BeamedRestHit& h : hits) {
            now.insert(h.measure);
        }
        bool changed = false;
        for (Measure* m : now) {
            const double stretch = m->userStretch();
            if (stretch >= 2.0) {
                continue;
            }
            // on this bar of this score only (not through the links to the main score and the other part scores)
            score->undo(new ChangeProperty(m, Pid::USER_STRETCH, PropertyValue(std::min(2.0, stretch + 0.15)), PropertyFlags::UNSTYLED));
            widened.insert(m);
            changed = true;
        }
        if (!changed) {
            break;
        }
        score->setLayoutAll();
        score->doLayout();
    }
    if (!widened.empty()) {
        nudge();
    }
    // and those still touching (a bar too crowded for both): up or down, away from the beam, just clear of what they
    // are over
    int lifted = 0;
    for (const BeamedRestHit& h : beamedRestHits(score)) {
        if (!h.rest || h.over.isNull()) {
            continue;
        }
        const double sp = score->style().spatium();
        const RectF r = h.rest->pageBoundingRect();
        const bool beamAbove = h.rest->beam() && h.rest->beam()->up();
        const double dy = beamAbove ? h.over.bottom() + 0.15 * sp - r.top() : h.over.top() - 0.15 * sp - r.bottom();
        if (std::abs(dy) > 5.0 * sp) {
            continue;
        }
        // not onto a chord symbol or other text above or below the bar
        const RectF moved = r.translated(0.0, dy);
        bool blocked = false;
        for (const Segment* seg = h.measure->first(); seg && !blocked; seg = seg->next()) {
            for (const EngravingItem* a : seg->annotations()) {
                if (a && a->visible() && a->ldata() && a->staffIdx() == h.rest->staffIdx() && moved.intersects(a->pageBoundingRect())) {
                    blocked = true;
                }
            }
        }
        if (blocked) {
            continue;
        }
        const PointF off = h.rest->offset() + PointF(0.0, dy);
        score->undo(new ChangeProperty(h.rest, Pid::OFFSET, PropertyValue(off), PropertyFlags::UNSTYLED));
        if (!score->isMaster()) {
            h.rest->unlinkPropertyFromMaster(Pid::OFFSET);
        }
        ++lifted;
    }
    if (lifted > 0) {
        score->setLayoutAll();
        score->doLayout();
    }
    nudged += lifted;
    if (nudged > 0 || !widened.empty()) {
        LOGI() << "[starscore] layout check: beamed rests clear of the notes beside them: " << (nudged - lifted) << " moved sideways, "
               << widened.size() << " bar(s) widened, " << lifted << " moved up or down";
    }
    return nudged + int(widened.size());
}

QStringList describeIssues(const std::vector<LayoutIssue>& issues)
{
    QStringList out;
    for (const LayoutIssue& i : issues) {
        out << QString("p%1 b%2 %3: %4").arg(i.page).arg(i.bar).arg(i.kind, i.what);
    }
    return out;
}
}
