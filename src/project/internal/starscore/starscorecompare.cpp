/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — "Compare parts": for each instrument that appears in more than one section,
 * which bars differ between its parts.
 */
#include "starscoreservice.h"

#include <map>
#include <set>

#include <QRegularExpression>
#include <QStringList>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/note.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/articulation.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/textbase.h"

#include "notation/inotationinteraction.h"
#include "notation/inotationelements.h"

#include "translation.h"
#include "log.h"

using namespace mu::project;
using namespace mu::engraving;
using namespace mu::notation;
using namespace muse;

//! What a part plays in one bar, as text: rhythm, pitches (concert), ties, articulations and dynamics.
//! Empty when the bar is only rests.
static std::string starscoreBarSignature(const Part* part, const Measure* m, bool ignoreOctave = false)
{
    std::string sig;
    bool hasNotes = false;
    for (const Staff* staff : part->staves()) {
        const track_idx_t startTrack = staff->idx() * VOICES;
        for (track_idx_t track = startTrack; track < startTrack + VOICES; ++track) {
            for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
                const EngravingItem* e = s->element(track);
                if (!e) {
                    continue;
                }
                sig += "|" + std::to_string(track - startTrack) + "@" + std::to_string(s->rtick().ticks());
                const ChordRest* cr = toChordRest(e);
                sig += "d" + std::to_string(cr->actualTicks().ticks());
                if (e->isChord()) {
                    hasNotes = true;
                    const Chord* c = toChord(e);
                    std::vector<int> pitches;
                    for (const Note* n : c->notes()) {
                        const int pitch = ignoreOctave ? n->pitch() % 12 : n->pitch();
                        pitches.push_back(pitch * 2 + (n->tieFor() ? 1 : 0));
                    }
                    std::sort(pitches.begin(), pitches.end());
                    for (int p : pitches) {
                        sig += "n" + std::to_string(p);
                    }
                    for (const Articulation* a : c->articulations()) {
                        sig += "a" + std::to_string(int(a->symId()));
                    }
                    sig += "g" + std::to_string(c->graceNotes().size());
                } else {
                    sig += "r";
                }
                for (const EngravingItem* ann : s->annotations()) {
                    if (ann->track() == track && ann->isDynamic()) {
                        sig += "D" + toDynamic(ann)->plainText().toStdString();
                    }
                }
            }
        }
    }
    return hasNotes ? sig : std::string();
}

//! Instruments count as the same whatever their key: bb-trumpet and c-trumpet are both "trumpet"
static QString starscoreInstrumentFamily(const Part* part)
{
    QString id = part->instrumentId().toQString();
    id.remove(QRegularExpression("^(bb|eb|ab|db|gb|c|f|a|d|g|e|b)-"));
    return id;
}

static QString starscoreBarRanges(const std::vector<int>& bars)
{
    QStringList out;
    size_t i = 0;
    while (i < bars.size()) {
        size_t j = i;
        while (j + 1 < bars.size() && bars[j + 1] == bars[j] + 1) {
            ++j;
        }
        out << (j > i ? QString("%1–%2").arg(bars[i]).arg(bars[j]) : QString::number(bars[i]));
        i = j + 1;
    }
    return out.join(", ");
}

std::vector<StarScoreComparison> StarScoreService::compareParts(const std::map<QString, QString>& referenceByInstrument) const
{
    std::vector<StarScoreComparison> result;
    const MasterScore* ms = masterScore();
    if (!ms) {
        return result;
    }
    const Data data = load();

    // Section names per part, in section order; parts in no section are left out
    std::map<QString, QStringList> sectionsOfPart;
    std::vector<QString> order;
    for (const StarScoreSection& s : data.sections) {
        for (const QString& pid : s.partIds) {
            if (!sectionsOfPart.count(pid)) {
                order.push_back(pid);
            }
            sectionsOfPart[pid] << s.name;
        }
    }

    std::vector<const Measure*> measures;
    for (const Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        measures.push_back(m);
    }

    // Group by instrument family, keeping section order
    std::vector<QString> families;
    std::map<QString, std::vector<const Part*> > byFamily;
    for (const QString& pid : order) {
        const Part* p = ms->partById(ID(pid));
        if (!p || p->instrument()->useDrumset()) {
            continue;
        }
        const QString fam = starscoreInstrumentFamily(p);
        if (!byFamily.count(fam)) {
            families.push_back(fam);
        }
        byFamily[fam].push_back(p);
    }

    for (const QString& fam : families) {
        const std::vector<const Part*>& parts = byFamily[fam];
        if (parts.size() < 2) {
            continue;
        }

        std::vector<std::vector<std::string> > sigs;
        std::vector<std::vector<std::string> > classSigs;   // pitch classes only, to spot octave-only differences
        for (const Part* p : parts) {
            std::vector<std::string> row;
            std::vector<std::string> classRow;
            row.reserve(measures.size());
            for (const Measure* m : measures) {
                row.push_back(starscoreBarSignature(p, m));
                classRow.push_back(starscoreBarSignature(p, m, true));
            }
            sigs.push_back(std::move(row));
            classSigs.push_back(std::move(classRow));
        }

        const QString instrumentName = parts.front()->instrument()->trackName().toQString();
        size_t ref = 0;
        auto refIt = referenceByInstrument.find(instrumentName);
        for (size_t i = 0; refIt != referenceByInstrument.end() && i < parts.size(); ++i) {
            if (idText(parts[i]) == refIt->second) {
                ref = i;
            }
        }

        auto labelOf = [&](size_t i) {
            return QString("%1 — %2").arg(parts[i]->partName().toQString(), sectionsOfPart[idText(parts[i])].join(", "));
        };
        auto shortOf = [&](size_t i) {
            return sectionsOfPart[idText(parts[i])].join(", ");
        };

        StarScoreComparison cmp;
        cmp.instrument = instrumentName;
        cmp.barCount = int(measures.size());

        for (size_t i = 0; i < parts.size(); ++i) {
            StarScoreComparedPart row;
            row.partId = idText(parts[i]);
            row.label = labelOf(i);
            row.isReference = i == ref;

            std::vector<int> differing;
            std::vector<int> octaveOnly;
            for (size_t b = 0; b < measures.size(); ++b) {
                const bool same = sigs[i][b] == sigs[ref][b];
                if (same) {
                    row.bars.push_back(sigs[i][b].empty() ? 2 : 0);
                } else if (!sigs[i][b].empty() && classSigs[i][b] == classSigs[ref][b]) {
                    row.bars.push_back(3);   // same notes and rhythm, different octave
                    octaveOnly.push_back(int(b) + 1);
                } else {
                    row.bars.push_back(1);
                    differing.push_back(int(b) + 1);
                }
            }

            if (i == ref) {
                row.summary = muse::qtrc("starscore", "Reference");
            } else if (differing.empty() && octaveOnly.empty()) {
                row.summary = muse::qtrc("starscore", "Same as %1").arg(shortOf(ref));
            } else if (differing.empty()) {
                row.summary = muse::qtrc("starscore", "Same as %1 apart from the octave in bars %2")
                              .arg(shortOf(ref), starscoreBarRanges(octaveOnly));
            } else {
                // Identical to another non-reference part?
                QString twin;
                for (size_t j = 0; j < i && twin.isEmpty(); ++j) {
                    if (j != ref && sigs[j] == sigs[i]) {
                        twin = shortOf(j);
                    }
                }
                row.summary = muse::qtrc("starscore", "%n bar(s) differ from %1: %2", nullptr, int(differing.size()))
                              .arg(shortOf(ref), starscoreBarRanges(differing));
                if (!octaveOnly.empty()) {
                    row.summary += "; " + muse::qtrc("starscore", "octave only: %1").arg(starscoreBarRanges(octaveOnly));
                }
                if (!twin.isEmpty()) {
                    row.summary += "  " + muse::qtrc("starscore", "(identical to %1)").arg(twin);
                }
            }
            cmp.parts.push_back(std::move(row));
        }
        result.push_back(std::move(cmp));
    }

    return result;
}

void StarScoreService::selectBar(const QString& partId, int bar)
{
    if (isSoloProject(globalContext()->currentProject().get())) {
        showMainScore();
    }
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    MasterScore* ms = masterScore();
    if (!master || !ms) {
        return;
    }
    const Part* part = ms->partById(ID(partId));
    Measure* m = ms->firstMeasure();
    for (int i = 1; m && i < bar; ++i) {
        m = m->nextMeasure();
    }
    if (!part || !m || part->staves().empty()) {
        return;
    }

    // Work in the main score so every section's staff is reachable; show the part if it is hidden
    globalContext()->setCurrentNotation(master->notation());
    if (!part->show()) {
        master->parts()->setPartsVisible({ { part->id(), true } }, TranslatableString::untranslatable("Show instrument"));
    }
    const staff_idx_t staffIdx = part->staves().front()->idx();
    INotationInteractionPtr interaction = master->notation()->interaction();
    interaction->select({ m }, SelectType::RANGE, staffIdx);
    interaction->showItem(m, int(staffIdx));
}
