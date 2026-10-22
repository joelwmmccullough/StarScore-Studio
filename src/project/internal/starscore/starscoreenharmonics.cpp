/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Spelling borrowed from the sheets written by hand (see starscoreenharmonics.h).
 */
#include "starscoreenharmonics.h"

#include <algorithm>
#include <map>
#include <set>

#include <QStringList>

#include "engraving/dom/accidental.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/tie.h"
#include "engraving/editing/editproperty.h"
#include "engraving/style/style.h"

#include "log.h"

using namespace mu::engraving;

namespace mu::project::starscore {
namespace {
//! a note's place in its bar: its beat (in ticks) times 64, plus the grace note's place before it (0 = the chord)
using NoteKey = std::pair<int, int>;   // (place, pitch class)

struct NoteSpell {
    int tpc = Tpc::TPC_INVALID;
    AccidentalType acc = AccidentalType::NONE;
    AccidentalBracket bracket = AccidentalBracket::NONE;
    bool user = false;     // an accidental put there by hand (one the key and the bar don't call for)
};

struct BarSpell {
    int tick = 0;
    QString sig;           // key, rhythm and pitch classes
    int places = 0;        // how many beats (and grace notes) have notes
    QString prevSig;       // the bars either side, as context
    QString nextSig;
    QString spelling;      // the spellings, to count which most matches agree on
    std::map<NoteKey, NoteSpell> notes;
};

int pitchClass(const Note* n)
{
    return ((n->epitch() % 12) + 12) % 12;
}

//! The bar of the score's first staff as it's written (the score at written pitch)
BarSpell readBar(const Measure* m)
{
    BarSpell bar;
    bar.tick = m->tick().ticks();
    const Staff* staff = m->score()->staff(0);
    std::map<int, QStringList> events;   // place -> "duration:pc,pc"
    auto take = [&](const Chord* c, int place) {
        QStringList pcs;
        for (const Note* n : c->notes()) {
            const int pc = pitchClass(n);
            pcs << QString::number(pc);
            NoteSpell s;
            s.tpc = n->tpc();
            if (const Accidental* a = n->accidental()) {
                s.acc = a->accidentalType();
                s.bracket = a->bracket();
                s.user = a->role() == AccidentalRole::USER;
            }
            bar.notes.emplace(NoteKey { place, pc }, s);   // (an octave doubling keeps the first)
        }
        pcs.sort();
        pcs.removeDuplicates();
        events[place] << QString("%1:%2").arg(c->actualTicks().ticks()).arg(pcs.join(','));
    };
    for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
        for (track_idx_t t = 0; t < VOICES; ++t) {
            const EngravingItem* e = s->element(t);
            if (!e || !e->isChord()) {
                continue;
            }
            const Chord* c = toChord(e);
            const int place = s->rtick().ticks() * 64;
            const auto& graces = c->graceNotesBefore();
            for (size_t i = 0; i < graces.size(); ++i) {
                take(graces[i], place - int(graces.size() - i));
            }
            take(c, place);
        }
    }
    if (events.empty()) {
        return bar;
    }
    QStringList sig { QString("k%1").arg(int(staff->key(m->tick()))) };
    for (auto& [place, list] : events) {
        list.sort();
        sig << QString("%1=%2").arg(place).arg(list.join('/'));
    }
    bar.sig = sig.join(' ');
    bar.places = int(events.size());
    QStringList spelling;
    for (const auto& [key, s] : bar.notes) {
        spelling << QString("%1.%2=%3%4%5").arg(key.first).arg(key.second).arg(s.tpc)
            .arg(s.user ? "u" : "").arg(s.bracket != AccidentalBracket::NONE ? "p" : "");
    }
    bar.spelling = spelling.join(' ');
    return bar;
}

bool writtenPitch(const Score* score)
{
    return !score->style().styleB(Sid::concertPitch);
}

//! every bar of the score, each with its neighbours' signatures
std::vector<BarSpell> readBars(const Score* score)
{
    std::vector<BarSpell> bars;
    for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        bars.push_back(readBar(m));
    }
    for (size_t i = 0; i < bars.size(); ++i) {
        bars[i].prevSig = i > 0 ? bars[i - 1].sig : QString();
        bars[i].nextSig = i + 1 < bars.size() ? bars[i + 1].sig : QString();
    }
    return bars;
}

//! a bar found elsewhere in the song is the same phrase when a bar beside it matches too (or it's long enough alone)
bool samePhrase(const BarSpell& a, const BarSpell& b)
{
    if (a.places >= 6) {
        return true;
    }
    return (!a.prevSig.isEmpty() && a.prevSig == b.prevSig) || (!a.nextSig.isEmpty() && a.nextSig == b.nextSig);
}
}

SpellingResult copyReferenceSpellings(Score* target, const std::vector<const Score*>& refs)
{
    SpellingResult result;
    if (!target || target->nstaves() == 0) {
        return result;
    }

    // every bar of the references, by its music
    std::map<QString, std::vector<BarSpell> > bySig;
    for (const Score* ref : refs) {
        if (!ref || ref == target || ref->nstaves() == 0 || ref->staff(0)->isDrumStaff(Fraction(0, 1))) {
            continue;
        }
        // (a part score shown at concert pitch spells the concert notes, not the written ones)
        if (!writtenPitch(ref) && !ref->staff(0)->transpose(Fraction(0, 1)).isZero()) {
            continue;
        }
        for (BarSpell& bar : readBars(ref)) {
            if (!bar.sig.isEmpty()) {
                bySig[bar.sig].push_back(std::move(bar));
            }
        }
    }

    // each bar of the target spelled as its matches spell it
    std::vector<std::pair<Note*, NoteSpell> > accidentalsToCopy;
    std::set<Note*> respelled;
    const std::vector<BarSpell> targetBars = readBars(target);
    size_t barIdx = 0;
    for (Measure* m = target->firstMeasure(); m; m = m->nextMeasure(), ++barIdx) {
        const BarSpell& bar = targetBars[barIdx];
        if (bar.sig.isEmpty()) {
            continue;
        }
        ++result.bars;
        auto found = bySig.find(bar.sig);
        if (found == bySig.end()) {
            continue;
        }
        // the matches at the same place in the song, else the same phrase elsewhere; of those, the spelling most agree on
        std::vector<const BarSpell*> pool;
        for (const BarSpell& b : found->second) {
            if (b.tick == bar.tick) {
                pool.push_back(&b);
            }
        }
        const bool sameBeat = !pool.empty();
        if (pool.empty()) {
            for (const BarSpell& b : found->second) {
                if (samePhrase(bar, b)) {
                    pool.push_back(&b);
                }
            }
        }
        if (pool.empty()) {
            continue;
        }
        std::map<QString, int> votes;
        for (const BarSpell* b : pool) {
            ++votes[b->spelling];
        }
        const BarSpell* chosen = pool.front();
        int best = 0;
        for (const BarSpell* b : pool) {
            if (votes[b->spelling] > best) {
                best = votes[b->spelling];
                chosen = b;
            }
        }
        ++result.matched;
        result.sameBeat += sameBeat ? 1 : 0;

        // the notes, by their place and pitch class
        for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            for (track_idx_t t = 0; t < VOICES; ++t) {
                EngravingItem* e = s->element(t);
                if (!e || !e->isChord()) {
                    continue;
                }
                Chord* c = toChord(e);
                const int place = s->rtick().ticks() * 64;
                std::vector<std::pair<Chord*, int> > chords;
                const auto& graces = c->graceNotesBefore();
                for (size_t i = 0; i < graces.size(); ++i) {
                    chords.push_back({ graces[i], place - int(graces.size() - i) });
                }
                chords.push_back({ c, place });
                for (const auto& [ch, pl] : chords) {
                    for (Note* n : ch->notes()) {
                        auto it = chosen->notes.find(NoteKey { pl, pitchClass(n) });
                        if (it == chosen->notes.end()) {
                            continue;
                        }
                        const NoteSpell& want = it->second;
                        // (the same sound: a spelling of this pitch class)
                        if (tpcIsValid(want.tpc) && want.tpc != n->tpc() && tpc2pitch(want.tpc) % 12 == pitchClass(n)) {
                            n->undoSetTpc(want.tpc);
                            respelled.insert(n);
                            ++result.respelled;
                        }
                        if (want.user || want.bracket != AccidentalBracket::NONE) {
                            accidentalsToCopy.push_back({ n, want });
                        }
                    }
                }
            }
        }
    }

    // notes tied to a respelled note have its spelling (a tie between a matched bar and one that isn't)
    for (Segment* s = target->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (track_idx_t t = 0; t < VOICES; ++t) {
            EngravingItem* e = s->element(t);
            if (!e || !e->isChord()) {
                continue;
            }
            for (Note* n : toChord(e)->notes()) {
                Tie* tie = n->tieFor();
                Note* next = tie ? tie->endNote() : nullptr;
                if (!next || next->tpc() == n->tpc() || tpc2pitch(next->tpc()) % 12 != tpc2pitch(n->tpc()) % 12) {
                    continue;
                }
                const bool from = respelled.count(n) > 0;
                const bool to = respelled.count(next) > 0;
                if (from && !to) {
                    next->undoSetTpc(n->tpc());
                    respelled.insert(next);   // (and on along the chain of ties)
                    ++result.tiesFixed;
                } else if (to && !from) {
                    n->undoSetTpc(next->tpc());
                    ++result.tiesFixed;
                }
            }
        }
    }

    // the accidentals as the new spellings have them, then the parentheses and hand-made accidentals copied
    target->setLayoutAll();
    target->doLayout();
    for (auto& [n, want] : accidentalsToCopy) {
        // only an accidental for the note as it's now spelled
        AccidentalType forNote = Accidental::value2subtype(tpc2alter(n->tpc()));
        if (forNote == AccidentalType::NONE) {
            forNote = AccidentalType::NATURAL;
        }
        if (want.acc != forNote) {
            continue;
        }
        Accidental* a = n->accidental();
        if (a && a->accidentalType() == want.acc) {
            if (a->bracket() != want.bracket) {
                target->undo(new ChangeProperty(a, Pid::ACCIDENTAL_BRACKET, int(want.bracket)));
                ++result.accidentalsCopied;
            }
        } else if (!a && want.user) {
            Accidental* added = Factory::createAccidental(n);
            added->setAccidentalType(want.acc);
            added->setRole(AccidentalRole::USER);
            added->setBracket(want.bracket);
            added->setParent(n);
            target->undoAddElement(added);
            ++result.accidentalsCopied;
        }
    }
    target->setLayoutAll();
    target->doLayout();
    return result;
}
}
