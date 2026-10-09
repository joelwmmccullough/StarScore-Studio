/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Player annotations (Joel, 9 Oct 2026): band members annotate their sheets, and a sheet update used to lose the
 * annotations. Now an annotation is kept in the song, in that one part score, marked as a player's; Export to
 * Sheets and Demos prints the sheet clean and, per player, a copy with their notes (see
 * engraving/dom/starscoreannotations.h).
 */
#include "starscoreannotations.h"
#include "starscoreservice.h"

#include <functional>

#include <QRegularExpression>
#include <QSettings>

#include "engraving/dom/articulation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/starscoreannotations.h"
#include "engraving/editing/undo.h"

#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"

#include "organizer/orgcore.h"
#include "organizer/orgstores.h"

#include "translation.h"
#include "log.h"

using namespace mu::project;
using namespace mu::engraving;
using namespace muse;

namespace mu::project::starscore {
//! Every item in the score that could be an annotation, once each
static void forEachMarking(Score* score, const std::function<void(EngravingItem*)>& fn)
{
    std::set<const EngravingItem*> seen;
    auto check = [&](EngravingItem* e) {
        if (engraving::starscore::canBeAnnotation(e) && seen.insert(e).second) {
            fn(e);
        }
    };
    for (Segment* seg = score->firstSegment(SegmentType::All); seg; seg = seg->next1()) {
        for (EngravingItem* a : seg->annotations()) {
            check(a);
        }
        for (EngravingItem* e : seg->elist()) {
            if (!e) {
                continue;
            }
            if (e->isBreath()) {
                check(e);
            } else if (e->isChord()) {
                Chord* c = toChord(e);
                for (Articulation* a : c->articulations()) {
                    check(a);
                }
                for (Note* n : c->notes()) {
                    for (EngravingItem* x : n->el()) {
                        check(x);
                    }
                }
            }
        }
    }
    for (const auto& [tick, sp] : score->spanner()) {
        check(sp);
    }
}

std::vector<AnnotationItem> annotationItems(Score* score)
{
    std::vector<AnnotationItem> out;
    MasterScore* master = score ? score->masterScore() : nullptr;
    if (!master || score == master) {
        return out;
    }
    const engraving::starscore::AnnotationMap map
        = engraving::starscore::parseAnnotations(master->metaTag(String(engraving::starscore::ANNOTATIONS_TAG)).toStdString());
    if (map.empty()) {
        return out;
    }
    forEachMarking(score, [&](EngravingItem* e) {
        auto it = map.find(engraving::starscore::annotationKey(e));
        if (it != map.end()) {
            out.push_back({ e, QString::fromStdString(it->second) });
        }
    });
    return out;
}

std::set<std::string> liveAnnotationKeys(MasterScore* master)
{
    std::set<std::string> keys;
    if (!master) {
        return keys;
    }
    for (Excerpt* ex : master->excerpts()) {
        if (Score* es = ex ? ex->excerptScore() : nullptr) {
            forEachMarking(es, [&](EngravingItem* e) {
                const std::string key = engraving::starscore::annotationKey(e);
                if (!key.empty()) {
                    keys.insert(key);
                }
            });
        }
    }
    return keys;
}

AnnotationDetacher::AnnotationDetacher(Score* score)
    : m_score(score)
{
}

AnnotationDetacher::~AnnotationDetacher()
{
    attachAll();
}

void AnnotationDetacher::detach(const std::vector<EngravingItem*>& items)
{
    for (EngravingItem* e : items) {
        const bool out = std::any_of(m_detached.begin(), m_detached.end(), [e](const Detached& d) { return d.item == e; });
        if (!e || out) {
            continue;
        }
        Segment* seg = e->explicitParent() && e->explicitParent()->isSegment() ? toSegment(e->explicitParent()) : nullptr;
        m_score->removeElement(e);
        Detached d { e, nullptr };
        // a breath's segment left empty goes too, as when a breath is deleted
        if (seg && e->isBreath() && seg->empty() && !seg->header() && !seg->trailer()) {
            m_score->removeElement(seg);
            d.segment = seg;
        }
        m_detached.push_back(d);
    }
}

void AnnotationDetacher::attach(const std::vector<EngravingItem*>& items)
{
    // back in the reverse order (a segment before its item)
    for (auto it = m_detached.end(); it != m_detached.begin();) {
        --it;
        if (std::find(items.begin(), items.end(), it->item) == items.end()) {
            continue;
        }
        if (it->segment) {
            m_score->addElement(it->segment);
        }
        m_score->addElement(it->item);
        it = m_detached.erase(it);
    }
}

void AnnotationDetacher::attachAll()
{
    std::vector<EngravingItem*> all;
    for (const Detached& d : m_detached) {
        all.push_back(d.item);
    }
    attach(all);
}
}

namespace {
const char* LAST_PLAYER_SETTING = "StarScore/lastAnnotationPlayer";

//! Changes the annotation marks as one undo step (with the edits that go with them)
class ChangeAnnotations : public UndoCommand
{
    OBJECT_ALLOCATOR(engraving, ChangeAnnotations)

    MasterScore* m_score = nullptr;
    String m_text;

    void flip(EditData*) override
    {
        const String old = m_score->metaTag(String(mu::engraving::starscore::ANNOTATIONS_TAG));
        if (m_text.isEmpty()) {
            m_score->metaTags().erase(String(mu::engraving::starscore::ANNOTATIONS_TAG));
        } else {
            m_score->setMetaTag(String(mu::engraving::starscore::ANNOTATIONS_TAG), m_text);
        }
        m_text = old;
    }

public:
    ChangeAnnotations(MasterScore* s, const String& t)
        : m_score(s), m_text(t) {}

    UNDO_TYPE(CommandType::ChangeMetaInfo)
    UNDO_NAME("ChangeAnnotations")
};

mu::engraving::starscore::AnnotationMap annotationMapOf(const MasterScore* master)
{
    return mu::engraving::starscore::parseAnnotations(master->metaTag(String(mu::engraving::starscore::ANNOTATIONS_TAG)).toStdString());
}

//! The map kept from now on: entries whose annotation is gone (deleted, or its part score) dropped
void storeAnnotationMap(MasterScore* master, mu::engraving::starscore::AnnotationMap map)
{
    const std::set<std::string> live = mu::project::starscore::liveAnnotationKeys(master);
    for (auto it = map.begin(); it != map.end();) {
        it = live.count(it->first) ? std::next(it) : map.erase(it);
    }
    const String text = String::fromStdString(mu::engraving::starscore::writeAnnotations(map));
    if (text != master->metaTag(String(mu::engraving::starscore::ANNOTATIONS_TAG))) {
        master->undo(new ChangeAnnotations(master, text));
    }
}

//! The markings selected in a part score that can be annotations (a line's segment as its line)
std::vector<EngravingItem*> selectedMarkings(Score* score)
{
    std::vector<EngravingItem*> out;
    for (EngravingItem* e : score->selection().elements()) {
        // (a text in a multimeasure rest is MuseScore's copy of the one in the bar underneath)
        EngravingItem* owner = const_cast<EngravingItem*>(
            mu::engraving::starscore::mmRestOriginal(mu::engraving::starscore::annotationOwner(e)));
        if (mu::engraving::starscore::canBeAnnotation(owner) && owner->score() == score
            && std::find(out.begin(), out.end(), owner) == out.end()) {
            out.push_back(owner);
        }
    }
    return out;
}

//! Whether the item is also in another score (the full score, another part score)
bool inOtherScores(const EngravingItem* e)
{
    for (const EngravingObject* l : e->linkList()) {
        if (l != e && l->score() != e->score()) {
            return true;
        }
    }
    return false;
}
}

StarScoreAnnotationTarget StarScoreService::annotationTarget() const
{
    StarScoreAnnotationTarget t;
    notation::INotationPtr n = globalContext()->currentNotation();
    Score* score = n && n->elements() ? n->elements()->msScore() : nullptr;
    MasterScore* master = score ? score->masterScore() : nullptr;
    if (!master) {
        t.problem = muse::qtrc("starscore", "Open a song first.");
        return t;
    }
    const mu::engraving::starscore::AnnotationMap map = annotationMapOf(master);

    // the band's current players first, then anyone else with annotations in the song
    const QString band = bandFolder();
    starscore::org::Roster roster;
    if (!band.isEmpty()) {
        roster.load(starscore::org::Paths::make(band, QString()));
    }
    for (const starscore::org::Player& p : roster.players) {
        if (p.current && !p.name.trimmed().isEmpty() && !t.players.contains(p.name.trimmed())) {
            t.players << p.name.trimmed();
        }
    }
    std::map<QString, std::map<QString, int> > byPlayer;
    for (Excerpt* ex : master->excerpts()) {
        Score* es = ex ? ex->excerptScore() : nullptr;
        for (const starscore::AnnotationItem& a : starscore::annotationItems(es)) {
            byPlayer[a.player][ex->name().toQString()]++;
        }
    }
    for (const auto& [player, sheets] : byPlayer) {
        if (!t.players.contains(player)) {
            t.players << player;
        }
        QStringList parts;
        int total = 0;
        for (const auto& [sheet, count] : sheets) {
            parts << QString("%1 (%2)").arg(sheet).arg(count);
            total += count;
        }
        t.summary << QString("%1: %2").arg(player, parts.join(", "));
    }

    if (score == master) {
        t.problem = muse::qtrc("starscore", "Open the part score (the sheet) first: an annotation goes on one player's "
                                            "sheet, not the full score.");
        return t;
    }
    t.sheet = score->excerpt() ? score->excerpt()->name().toQString() : QString();

    // who reads this sheet: the roster player with this part ("7H: Tenor Sax 1" -> "Tenor Sax 1")
    QStringList names;
    static const QRegularExpression sectionPrefix("^[^:]+:\\s*");
    names << QString(t.sheet).remove(sectionPrefix).trimmed();
    if (!score->parts().empty()) {
        for (const Staff* st : score->parts().front()->staves()) {
            if (const Staff* ms = st->findLinkedInScore(master)) {
                names << ms->part()->partName().toQString().remove(sectionPrefix).trimmed();
                break;
            }
        }
    }
    for (const QString& name : names) {
        for (const starscore::org::Player& p : roster.players) {
            if (!p.current || !t.defaultPlayer.isEmpty()) {
                continue;
            }
            for (const QString& inst : p.instruments) {
                if (inst.compare(name, Qt::CaseInsensitive) == 0) {
                    t.defaultPlayer = p.name.trimmed();
                    break;
                }
            }
        }
    }
    if (t.defaultPlayer.isEmpty()) {
        t.defaultPlayer = QSettings().value(LAST_PLAYER_SETTING).toString();
    }

    const Selection& sel = score->selection();
    t.canAdd = sel.isSingle() && sel.element() && (sel.element()->isNote() || sel.element()->isRest() || sel.element()->isChord());
    if (!sel.isRange()) {
        for (EngravingItem* e : selectedMarkings(score)) {
            ++t.count;
            t.inOtherScores += inOtherScores(e) ? 1 : 0;
            auto it = map.find(mu::engraving::starscore::annotationKey(e));
            if (it != map.end() && !t.owners.contains(QString::fromStdString(it->second))) {
                t.owners << QString::fromStdString(it->second);
            }
        }
    }
    return t;
}

QString StarScoreService::addPlayerAnnotation(const QString& playerName, const QString& text)
{
    const std::string player = mu::engraving::starscore::cleanPlayerName(playerName.toStdString());
    if (player.empty()) {
        return muse::qtrc("starscore", "Choose whose annotation this is.");
    }
    if (text.trimmed().isEmpty()) {
        return muse::qtrc("starscore", "Type the annotation first.");
    }
    notation::INotationPtr n = globalContext()->currentNotation();
    Score* score = n && n->elements() ? n->elements()->msScore() : nullptr;
    MasterScore* master = score ? score->masterScore() : nullptr;
    if (!master || score == master) {
        return muse::qtrc("starscore", "Open the part score (the sheet) first.");
    }
    const Selection& sel = score->selection();
    EngravingItem* e = sel.isSingle() ? sel.element() : nullptr;
    ChordRest* cr = !e ? nullptr : e->isNote() ? toNote(e)->chord() : e->isChordRest() ? toChordRest(e) : nullptr;
    // a multimeasure rest: its first bar
    if (cr && cr->measure() && cr->measure()->isMMRest()) {
        Measure* under = score->tick2measure(cr->tick());
        Segment* first = under && !under->isMMRest() ? under->first(SegmentType::ChordRest) : nullptr;
        cr = first ? first->cr(cr->track()) : nullptr;
    }
    if (!cr || !cr->segment()) {
        return muse::qtrc("starscore", "Click the note or rest the annotation goes above first.");
    }

    n->undoStack()->prepareChanges(TranslatableString("starscore", "Player annotation"));
    StaffText* st = Factory::createStaffText(cr->segment());
    st->setTrack(cr->track());
    st->setParent(cr->segment());
    st->setPlainText(text.trimmed());
    score->undoAddElement(st, /*addToLinkedStaves*/ false);   // on this sheet only
    st->assignNewEID();
    mu::engraving::starscore::AnnotationMap map = annotationMapOf(master);
    map[mu::engraving::starscore::annotationKey(st)] = player;
    storeAnnotationMap(master, map);
    n->undoStack()->commitChanges();
    QSettings().setValue(LAST_PLAYER_SETTING, QString::fromStdString(player));
    n->interaction()->select({ st }, SelectType::SINGLE);
    n->notationChanged().notify();
    return QString();
}

QString StarScoreService::markPlayerAnnotations(const QString& playerName)
{
    const std::string player = mu::engraving::starscore::cleanPlayerName(playerName.toStdString());
    if (player.empty()) {
        return muse::qtrc("starscore", "Choose whose annotations these are.");
    }
    notation::INotationPtr n = globalContext()->currentNotation();
    Score* score = n && n->elements() ? n->elements()->msScore() : nullptr;
    MasterScore* master = score ? score->masterScore() : nullptr;
    if (!master || score == master) {
        return muse::qtrc("starscore", "Open the part score (the sheet) first.");
    }
    if (score->selection().isRange()) {
        return muse::qtrc("starscore", "Click the markings themselves (Ctrl+click or Cmd+click to add more), "
                                       "not a range of bars.");
    }
    const std::vector<EngravingItem*> items = selectedMarkings(score);
    if (items.empty()) {
        return muse::qtrc("starscore", "Select the markings first: staff text, expressions, dynamics, fingerings, "
                                       "articulations, breath marks, symbols, fermatas, text lines or hairpins.");
    }

    n->undoStack()->prepareChanges(TranslatableString("starscore", "Mark as player annotation"));
    mu::engraving::starscore::AnnotationMap map = annotationMapOf(master);
    for (EngravingItem* e : items) {
        // on this sheet only: taken out of the full score and the other part scores (MuseScore's "exclude from
        // other parts", which every kind of item gets here)
        if (inOtherScores(e)) {
            e->manageExclusionFromParts(true);
        }
        if (!e->eid().isValid()) {
            e->assignNewEID();
        }
        map[mu::engraving::starscore::annotationKey(e)] = player;
    }
    storeAnnotationMap(master, map);
    n->undoStack()->commitChanges();
    QSettings().setValue(LAST_PLAYER_SETTING, QString::fromStdString(player));
    n->notationChanged().notify();
    return QString();
}

QString StarScoreService::unmarkPlayerAnnotations()
{
    notation::INotationPtr n = globalContext()->currentNotation();
    Score* score = n && n->elements() ? n->elements()->msScore() : nullptr;
    MasterScore* master = score ? score->masterScore() : nullptr;
    if (!master || score == master) {
        return muse::qtrc("starscore", "Open the part score (the sheet) first.");
    }
    mu::engraving::starscore::AnnotationMap map = annotationMapOf(master);
    int taken = 0;
    for (EngravingItem* e : selectedMarkings(score)) {
        taken += int(map.erase(mu::engraving::starscore::annotationKey(e)));
    }
    if (taken == 0) {
        return muse::qtrc("starscore", "Select the annotations first (click them; Ctrl+click or Cmd+click to add more).");
    }
    n->undoStack()->prepareChanges(TranslatableString("starscore", "Unmark player annotation"));
    storeAnnotationMap(master, map);
    n->undoStack()->commitChanges();
    n->notationChanged().notify();
    return QString();
}
