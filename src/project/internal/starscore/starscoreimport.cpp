/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — importing a MuseScore file: guess its sections and arrangements from the instruments,
 * let the user confirm them, optionally standardize the file, and save it as a new .starscore.
 */
#include "starscoreservice.h"
#include "starscoreengraving.h"

#include <algorithm>
#include <map>
#include <set>

#include <QRegularExpression>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/part.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/chord.h"
#include "engraving/rw/xmlreader.h"
#include "engraving/dom/select.h"

#include "notation/inotationparts.h"
#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "inotationproject.h"
#include "translation.h"

#include "log.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;

namespace {
const std::set<QString> IMPORT_TRUMPETS { "bb-trumpet", "c-trumpet", "trumpet", "d-trumpet", "eb-trumpet", "piccolo-trumpet",
                                          "flugelhorn", "cornet", "bb-cornet", "eb-cornet" };
const std::set<QString> IMPORT_SAXES { "soprano-saxophone", "alto-saxophone", "tenor-saxophone", "baritone-saxophone",
                                       "bass-saxophone", "melody-saxophone", "sopranino-saxophone" };
const std::set<QString> IMPORT_TROMBONES { "trombone", "bass-trombone", "alto-trombone", "tenor-trombone" };
const std::set<QString> IMPORT_OTHER_HORNS { "flute", "piccolo", "alto-flute", "bb-clarinet", "c-clarinet", "eb-clarinet",
                                             "a-clarinet", "bb-bass-clarinet", "bass-clarinet", "french-horn", "horn" };

bool importIsHorn(const QString& id)
{
    return IMPORT_TRUMPETS.count(id) || IMPORT_SAXES.count(id) || IMPORT_TROMBONES.count(id) || IMPORT_OTHER_HORNS.count(id);
}

//! A horn that normally plays the top line: a new horn section starts where one follows a lower instrument
bool importIsTopHorn(const QString& id)
{
    return IMPORT_TRUMPETS.count(id) || id == "flute" || id == "piccolo";
}

//! What a rhythm-section instrument does, so "Electric Piano" counts as the section's keyboard
QString importRhythmRole(const QString& id)
{
    if (id == "congas" || id == "bongos" || id == "percussion" || id == "timbales" || id == "cajon") {
        return "percussion";
    }
    if (id == "drumset" || id == "drum-kit") {
        return "drums";
    }
    if (id.contains("guitar") && !id.contains("bass")) {
        return "guitar";
    }
    if (id.contains("bass") || id == "contrabass") {
        return "bass";
    }
    static const QStringList keys { "piano", "clavinet", "accordion", "organ", "synth", "harpsichord", "keyboard",
                                    "vibraphone", "marimba" };
    for (const QString& k : keys) {
        if (id.contains(k)) {
            return "keys";
        }
    }
    return QString();
}

bool importIsLeadName(const QString& name)
{
    static const QRegularExpression re("^\\s*lead(\\s*sheet)?\\s*$", QRegularExpression::CaseInsensitiveOption);
    return re.match(name).hasMatch();
}

mu::engraving::Excerpt* importExcerptOf(const IExcerptNotationPtr& excerptNotation)
{
    if (!excerptNotation || !excerptNotation->isInited()) {
        return nullptr;
    }
    INotationPtr notation = excerptNotation->notation();
    if (!notation || !notation->elements()) {
        return nullptr;
    }
    mu::engraving::Score* score = notation->elements()->msScore();
    return score ? score->excerpt() : nullptr;
}

bool staffHasNotes(const mu::engraving::MasterScore* score, mu::engraving::staff_idx_t staffIdx)
{
    using namespace mu::engraving;
    for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            for (track_idx_t t = staffIdx * VOICES; t < (staffIdx + 1) * VOICES; ++t) {
                const EngravingItem* e = s->element(t);
                if (e && e->isChord()) {
                    return true;
                }
            }
        }
    }
    return false;
}
}

bool StarScoreService::needsImport() const
{
    INotationProjectPtr project = globalContext()->currentProject();
    const engraving::MasterScore* ms = masterScore();
    if (!project || !ms || isSoloProject(project.get())) {
        return false;
    }
    if (io::suffix(project->path()) == "starscore") {
        return false;
    }
    return loadFrom(ms).sections.empty();
}

StarScoreImportPlan StarScoreService::planImport() const
{
    StarScoreImportPlan plan;
    const engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return plan;
    }

    std::vector<engraving::Part*> parts(ms->parts().begin(), ms->parts().end());
    std::vector<QString> keys(parts.size());
    std::vector<QString> ids(parts.size());
    for (size_t i = 0; i < parts.size(); ++i) {
        ids[i] = parts[i]->instrumentId().toQString();
    }

    // 1. Lead sheet: a part called "Lead" / "Lead Sheet", else a piano at the very top of a score with other instruments
    int lead = -1;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (importIsLeadName(parts[i]->partName().toQString())) {
            lead = int(i);
            break;
        }
    }
    if (lead < 0 && parts.size() > 1 && importRhythmRole(ids[0]) == "keys" && ids[0].contains("piano")) {
        lead = 0;
    }

    // 2. Horn sections: runs of horns; a new section starts where a trumpet or flute follows a lower instrument
    std::vector<std::vector<int> > runs;
    int prev = -2;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (int(i) == lead || !importIsHorn(ids[i])) {
            continue;
        }
        const bool startNew = runs.empty() || prev != int(i) - 1
                              || (importIsTopHorn(ids[i]) && !importIsTopHorn(ids[prev]));
        if (startNew) {
            runs.push_back({});
        }
        runs.back().push_back(int(i));
        prev = int(i);
    }

    static const QRegularExpression anyHornRe("^\\s*horn\\s*\\d", QRegularExpression::CaseInsensitiveOption);
    std::set<QString> usedKeys;
    bool bigBand = false;
    for (const std::vector<int>& run : runs) {
        int tpts = 0, saxes = 0, tbns = 0, anyNamed = 0;
        for (int i : run) {
            tpts += IMPORT_TRUMPETS.count(ids[i]) ? 1 : 0;
            saxes += IMPORT_SAXES.count(ids[i]) ? 1 : 0;
            tbns += IMPORT_TROMBONES.count(ids[i]) ? 1 : 0;
            anyNamed += anyHornRe.match(parts[i]->partName().toQString()).hasMatch() ? 1 : 0;
        }

        if (run.size() >= 8 && tpts >= 3 && saxes >= 4 && tbns >= 3 && !usedKeys.count("bigband-saxes")) {
            for (int i : run) {
                keys[i] = IMPORT_TRUMPETS.count(ids[i]) ? "bigband-trumpets"
                          : IMPORT_TROMBONES.count(ids[i]) ? "bigband-trombones" : "bigband-saxes";
            }
            usedKeys.insert({ "bigband-saxes", "bigband-trumpets", "bigband-trombones" });
            bigBand = true;
            continue;
        }

        const int n = int(run.size());
        QString key;
        if (anyNamed == n && (n == 2 || n == 3 || n == 4)) {
            key = QString("%1-horn-any").arg(n == 4 ? 3 : n);    // 3-Horn Any has a hidden flute chair
        } else if (n >= 2 && n <= 7) {
            key = QString("%1-horn").arg(n);
        }
        if (key.isEmpty() || usedKeys.count(key)) {
            continue;
        }
        usedKeys.insert(key);
        for (int i : run) {
            keys[i] = key;
        }
    }

    if (lead >= 0 && !bigBand) {
        keys[lead] = "lead-sheet";
    }

    // 3. Rhythm section: the remaining rhythm instruments
    for (size_t i = 0; i < parts.size(); ++i) {
        if (keys[i].isEmpty() && !importRhythmRole(ids[i]).isEmpty()) {
            keys[i] = bigBand ? "bigband-rhythm" : "rhythm";
        }
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        StarScoreImportPart ip;
        ip.partId = idText(parts[i]);
        ip.name = parts[i]->partName().toQString();
        ip.instrumentId = ids[i];
        ip.suggestedSection = keys[i];
        ip.staves = int(parts[i]->nstaves());
        plan.parts.push_back(ip);
    }

    // Arrangements: each horn section with the lead sheet and rhythm section
    for (const StarScoreArrangementTemplate& a : arrangementTemplates()) {
        bool hasMain = false, complete = true;
        for (const QString& k : a.sectionKeys) {
            const bool have = std::find(keys.begin(), keys.end(), k) != keys.end();
            if (k != "lead-sheet" && k != "rhythm") {
                hasMain |= have;
            }
            complete &= have || k == "lead-sheet" || k == "rhythm";
        }
        if (hasMain && complete) {
            plan.suggestedArrangements << a.key;
        }
    }

    // Summary
    QStringList found;
    for (const StarScoreSectionTemplate& t : sectionTemplates()) {
        QStringList names;
        std::set<QString> roles;
        for (size_t i = 0; i < parts.size(); ++i) {
            if (keys[i] == t.key) {
                names << parts[i]->partName().toQString();
                roles.insert(importRhythmRole(ids[i]));
            }
        }
        if (names.isEmpty()) {
            continue;
        }
        QString line = QString("%1 (%2").arg(t.name, names.join(", "));
        QStringList missing;
        for (const StarScoreInstrument& inst : t.instruments) {
            if (t.key == "rhythm" && !roles.count(importRhythmRole(inst.instrumentId))) {
                missing << inst.partName;
            }
        }
        if (!missing.isEmpty()) {
            line += muse::qtrc("starscore", "; no %1").arg(missing.join(", "));
        }
        found << line + ")";
    }
    plan.summary = found.isEmpty() ? muse::qtrc("starscore", "StarScore couldn't recognise any sections. Choose them below.")
                   : muse::qtrc("starscore", "Found: %1.").arg(found.join("; "));
    return plan;
}

Ret StarScoreService::applyImport(const std::map<QString, QString>& sectionByPart, const QStringList& arrangementKeys,
                                  bool standardize)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return make_ret(Ret::Code::InternalError);
    }

    Data data = load();
    QStringList takenIds;
    for (const StarScoreSection& s : data.sections) {
        takenIds << s.id;
    }

    // Sections in the order their first instrument appears in the score
    std::map<QString, int> sectionIndexByKey;
    for (engraving::Part* p : ms->parts()) {
        const QString pid = idText(p);
        auto it = sectionByPart.find(pid);
        if (it == sectionByPart.end() || it->second.isEmpty()) {
            continue;
        }
        const QString& key = it->second;
        if (!sectionIndexByKey.count(key)) {
            const StarScoreSectionTemplate* t = sectionTemplate(key);
            StarScoreSection s;
            s.templateKey = key;
            s.name = t ? t->name : key;
            s.id = uniqueId(takenIds, key);
            s.status = StarScoreStatus::InProgress;
            takenIds << s.id;
            sectionIndexByKey[key] = int(data.sections.size());
            data.sections.push_back(s);
        }
        StarScoreSection& s = data.sections[sectionIndexByKey[key]];
        s.partIds << pid;
        if (p->show()) {
            s.shownPartIds << pid;
        }
    }

    // Arrangements
    QStringList takenArr;
    for (const StarScoreArrangement& a : data.arrangements) {
        takenArr << a.id;
    }
    for (const StarScoreArrangementTemplate& t : arrangementTemplates()) {
        if (!arrangementKeys.contains(t.key)) {
            continue;
        }
        StarScoreArrangement a;
        a.id = uniqueId(takenArr, "arr-" + t.key);
        a.name = t.name;
        a.templateKey = t.key;
        for (const QString& k : t.sectionKeys) {
            if (sectionIndexByKey.count(k)) {
                a.sectionIds << data.sections[sectionIndexByKey[k]].id;
            }
        }
        if (a.sectionIds.isEmpty()) {
            continue;
        }
        takenArr << a.id;
        data.arrangements.push_back(a);
    }
    store(data);

    if (standardize) {
        standardizeImported();
        QStringList all;
        for (const engraving::Part* p : ms->parts()) {
            all << idText(p);
        }
        addPartBooksFor(all);
        applyStyles();
    }

    setImportedRhythmStatus();
    syncArrangementScores();
    m_changed.notify();
    return make_ok();
}

void StarScoreService::standardizeImported()
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return;
    }

    auto rename = [&](engraving::Part* p, const QString& name) {
        if (name.isEmpty() || p->partName().toQString() == name) {
            return;
        }
        const QString old = p->partName().toQString();
        master->parts()->setInstrumentName(InstrumentKey { p->instrumentId(), p->id(), engraving::Fraction(0, 1) }, name);
        p->setPartName(String::fromQString(name));
        // A part book holding just this instrument follows the new name
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            engraving::Excerpt* ex = importExcerptOf(e);
            if (!ex) {
                continue;
            }
            const std::vector<engraving::Part*> ps = masterPartsOf(ex);
            if (ps.size() == 1 && ps.front() == p && e->name() == old) {
                e->setName(name);
            }
        }
    };

    auto partById = [&](const QString& id) -> engraving::Part* {
        for (engraving::Part* p : ms->parts()) {
            if (idText(p) == id) {
                return p;
            }
        }
        return nullptr;
    };

    Data data = load();
    std::vector<std::pair<QString, StarScoreInstrument> > toAdd;   // section id, instrument

    for (const StarScoreSection& s : data.sections) {
        const StarScoreSectionTemplate* t = sectionTemplate(s.templateKey);
        if (!t) {
            continue;
        }

        if (s.templateKey == "lead-sheet") {
            if (engraving::Part* p = s.partIds.isEmpty() ? nullptr : partById(s.partIds.front())) {
                rename(p, "Lead");
                // The lead sheet shows only the treble staff, unless the bass staff has music in it
                if (p->nstaves() > 1) {
                    const engraving::Staff* bass = p->staves().at(1);
                    if (bass->show() && !staffHasNotes(ms, bass->idx())) {
                        master->parts()->setStaffVisible(bass->id(), false);
                    }
                }
            }
            continue;
        }

        const bool rhythm = s.templateKey == "rhythm" || s.templateKey == "bigband-rhythm";
        std::vector<bool> used(t->instruments.size(), false);
        std::set<QString> roles;
        std::set<QString> presentIds;

        for (const QString& pid : s.partIds) {
            engraving::Part* p = partById(pid);
            if (!p) {
                continue;
            }
            const QString iid = p->instrumentId().toQString();
            roles.insert(importRhythmRole(iid));
            presentIds.insert(iid);
            // Same instrument as the template's next unused one: take its name (and chair ranges)
            for (size_t k = 0; k < t->instruments.size(); ++k) {
                const StarScoreInstrument& inst = t->instruments[k];
                if (used[k] || inst.instrumentId != iid) {
                    continue;
                }
                used[k] = true;
                rename(p, inst.partName);
                if (engraving::Instrument* ins = p->instrument()) {
                    if (inst.minPitchA >= 0) {
                        ins->setMinPitchA(inst.minPitchA);
                        ins->setMaxPitchA(inst.maxPitchA);
                        ins->setMinPitchP(inst.minPitchP);
                        ins->setMaxPitchP(inst.maxPitchP);
                    }
                }
                break;
            }
        }

        // Missing instruments the template keeps hidden (e.g. Congas) are added, hidden
        for (const StarScoreInstrument& inst : t->instruments) {
            if (!inst.hidden) {
                continue;
            }
            const bool present = rhythm ? roles.count(importRhythmRole(inst.instrumentId)) > 0
                                 : presentIds.count(inst.instrumentId) > 0;
            if (!present) {
                toAdd.emplace_back(s.id, inst);
            }
        }
    }

    for (const auto& [sectionId, inst] : toAdd) {
        const InstrumentTemplate& tpl = instrumentsRepository()->instrumentTemplate(String::fromQString(inst.instrumentId));
        if (tpl.id.isEmpty()) {
            LOGW() << "[starscore] unknown instrument " << inst.instrumentId;
            continue;
        }

        Data d = load();
        auto sit = std::find_if(d.sections.begin(), d.sections.end(), [&](const StarScoreSection& s) { return s.id == sectionId; });
        if (sit == d.sections.end()) {
            continue;
        }

        // Insert right after the section's last instrument
        std::set<QString> before;
        PartInstrumentList list;
        int insertAt = -1;
        for (const engraving::Part* p : ms->parts()) {
            before.insert(idText(p));
            PartInstrument pi;
            pi.isExistingPart = true;
            pi.partId = p->id();
            list << pi;
            if (sit->partIds.contains(idText(p))) {
                insertAt = int(list.size());
            }
        }
        PartInstrument added;
        added.isExistingPart = false;
        added.instrumentTemplate = tpl;
        if (insertAt < 0 || insertAt > int(list.size())) {
            list << added;
        } else {
            list.insert(insertAt, added);
        }

        engraving::ScoreOrder order = master->parts()->scoreOrder();
        order.customized = true;
        master->parts()->setParts(list, order);

        std::vector<engraving::Part*> newParts;
        for (engraving::Part* p : ms->parts()) {
            if (!before.count(idText(p))) {
                newParts.push_back(p);
            }
        }
        if (newParts.empty()) {
            continue;
        }
        StarScoreSection made = finishNewParts(newParts, { inst });

        d = load();
        for (StarScoreSection& s : d.sections) {
            if (s.id == sectionId) {
                s.partIds << made.partIds;
                s.shownPartIds << made.shownPartIds;
            }
        }
        store(d);
    }

    // Rhythm section in the usual order: keys, guitar, bass, drums, percussion (an electric piano takes the
    // piano's place, a bass synth the bass guitar's)
    static const QStringList ROLE_ORDER { "keys", "guitar", "bass", "drums", "percussion" };
    const Data after = load();
    for (const StarScoreSection& s : after.sections) {
        if (s.templateKey != "rhythm" && s.templateKey != "bigband-rhythm") {
            continue;
        }
        std::vector<engraving::Part*> current;
        for (engraving::Part* p : ms->parts()) {
            if (s.partIds.contains(idText(p))) {
                current.push_back(p);
            }
        }
        std::vector<engraving::Part*> wanted = current;
        std::stable_sort(wanted.begin(), wanted.end(), [](const engraving::Part* a, const engraving::Part* b) {
            auto rank = [](const engraving::Part* p) {
                const int r = int(ROLE_ORDER.indexOf(importRhythmRole(p->instrumentId().toQString())));
                return r < 0 ? 99 : r;
            };
            return rank(a) < rank(b);
        });
        if (wanted == current || wanted.size() < 2) {
            continue;
        }
        // put each one right after the previous, starting from where the section begins
        master->parts()->moveParts({ wanted.front()->id() }, current.front()->id(), INotationParts::InsertMode::Before);
        for (size_t i = 1; i < wanted.size(); ++i) {
            master->parts()->moveParts({ wanted[i]->id() }, wanted[i - 1]->id(), INotationParts::InsertMode::After);
        }
    }
}

void StarScoreService::setImportedRhythmStatus()
{
    engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return;
    }
    Data data = load();
    bool changed = false;
    for (StarScoreSection& s : data.sections) {
        if (s.templateKey != "rhythm" && s.templateKey != "bigband-rhythm") {
            continue;
        }
        bool coreDone = true;          // guitar and bass written out
        bool someUseLead = false;      // drums, percussion or keys not written out
        bool anyCore = false;
        for (const engraving::Part* p : ms->parts()) {
            if (!s.partIds.contains(idText(p))) {
                continue;
            }
            const QString role = importRhythmRole(p->instrumentId().toQString());
            const bool unfinished = starscore::partLooksUnfinished(ms, p);
            if (role == "guitar" || role == "bass") {
                anyCore = true;
                coreDone &= !unfinished;
            } else if (unfinished) {
                someUseLead = true;
            }
        }
        // Guitar and bass finished, drums / percussion / keys (some of them) left to the lead sheet
        if (anyCore && coreDone && someUseLead) {
            s.status = StarScoreStatus::FinishedLeadSheetParts;
            changed = true;
        }
    }
    if (changed) {
        store(data);
    }
}

void StarScoreService::saveAsNewStarScore()
{
    INotationProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return;
    }
    const io::path_t path = project->path();
    if (io::suffix(path) == "starscore") {
        return;
    }
    const io::path_t dir = io::dirpath(path);
    const io::path_t base = io::filename(path, false);
    project->markAsNewlyCreated();
    if (!dir.empty() && !base.empty()) {
        project->setPath(dir.appendingComponent(base).appendingSuffix("starscore"));
    }
    m_changed.notify();
}

void StarScoreService::fillAnyHornsFromStandard(const StarScoreSection& anySection)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms || !ms->firstMeasure()) {
        return;
    }

    // "3-horn-any" takes its music from "3-horn", "2-horn-any" from "2-horn"
    const QString standardKey = QString(anySection.templateKey).remove("-any");
    const Data data = load();
    const StarScoreSection* standard = nullptr;
    for (const StarScoreSection& s : data.sections) {
        if (s.templateKey == standardKey && !s.partIds.isEmpty()) {
            standard = &s;
            break;
        }
    }
    if (!standard) {
        return;
    }

    auto partsInOrder = [&](const QStringList& ids) {
        std::vector<engraving::Part*> out;
        for (engraving::Part* p : ms->parts()) {
            if (ids.contains(idText(p))) {
                out.push_back(p);
            }
        }
        return out;
    };
    const std::vector<engraving::Part*> sources = partsInOrder(standard->partIds);
    const std::vector<engraving::Part*> chairs = partsInOrder(anySection.partIds);

    // Chairs in order (Horn 1, Horn 2, Horn 3) take the standard section's parts in order (trumpet, alto, tenor);
    // the flute stand-in for Horn 1 gets Horn 1's music
    std::vector<std::pair<engraving::Part*, engraving::Part*> > copies;   // from, to
    size_t next = 0;
    for (engraving::Part* chair : chairs) {
        if (chair->instrumentId() == u"flute") {
            if (!sources.empty()) {
                copies.emplace_back(sources.front(), chair);
            }
            continue;
        }
        if (next < sources.size()) {
            copies.emplace_back(sources[next++], chair);
        }
    }
    if (copies.empty()) {
        return;
    }

    engraving::Segment* start = ms->firstMeasure()->first(engraving::SegmentType::ChordRest);
    master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Fill Any Horns from the standard section"));
    for (const auto& [from, to] : copies) {
        if (from->staves().empty() || to->staves().empty()) {
            continue;
        }
        const engraving::staff_idx_t src = from->staves().front()->idx();
        const engraving::staff_idx_t dst = to->staves().front()->idx();
        engraving::Selection sel(ms);
        sel.setRange(start, nullptr, src, src + 1);
        const ByteArray mime = sel.mimeData();
        if (mime.empty()) {
            continue;
        }
        engraving::XmlReader reader(mime);
        ms->pasteStaff(reader, start, dst);
    }
    master->notation()->undoStack()->commitChanges();
    master->notation()->notationChanged().notify();
}
