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
#include <QTimer>
#include <QUuid>
#include <QFile>
#include <QDir>

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
#include "engraving/dom/box.h"
#include "engraving/dom/text.h"
#include "engraving/editing/editpart.h"

#include "notation/inotationparts.h"
#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationstyle.h"
#include "notation/inotation.h"
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

    // The score's title: when it's empty or "Untitled score", take the title frame's
    if (INotationProjectPtr project = globalContext()->currentProject()) {
        const QString current = project->metaInfo().title.trimmed().toLower();
        if (current.isEmpty() || current == "untitled score" || current == "untitled") {
            const engraving::MeasureBase* first = ms->first();
            if (first && first->isVBox()) {
                for (engraving::EngravingItem* e : first->el()) {
                    if (e && e->isText() && engraving::toText(e)->textStyleType() == engraving::TextStyleType::TITLE) {
                        const QString frameTitle = engraving::toText(e)->plainText().toQString().simplified();
                        if (!frameTitle.isEmpty()) {
                            ProjectMeta meta = project->metaInfo();
                            meta.title = frameTitle;
                            project->setMetaInfo(meta);
                        }
                        break;
                    }
                }
            }
        }
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
            const std::optional<StarScoreSectionTemplate> t = sectionTemplate(key);
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

    syncArrangementScores();
    scheduleChanged();
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
        const std::optional<StarScoreSectionTemplate> t = sectionTemplate(s.templateKey);
        if (!t) {
            continue;
        }

        if (s.templateKey == "lead-sheet") {
            if (engraving::Part* p = s.partIds.isEmpty() ? nullptr : partById(s.partIds.front())) {
                rename(p, "Lead");
                // The lead sheet's bass staff shows only in systems where it has music
                autoHideLeadBassStaff(master, p);
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
    scheduleChanged();
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

    // Each chair's part score takes the formatting of the part it came from (Horn 1 = Trumpet, Horn 2 = Alto
    // Sax, …): its style and its system and page breaks
    auto bookOf = [&](const engraving::Part* part) -> INotationPtr {
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            INotationPtr n = e ? e->notation() : nullptr;
            engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
            if (!es || es->parts().size() != 1) {
                continue;
            }
            const std::vector<engraving::Part*> ps = masterPartsOf(es, ms);
            if (ps.size() == 1 && ps.front() == part) {
                return n;
            }
        }
        return nullptr;
    };
    const QString mss = QDir::tempPath() + "/starscore-chair-" + QUuid::createUuid().toString(QUuid::Id128) + ".mss";
    for (const auto& [from, to] : copies) {
        INotationPtr src = bookOf(from);
        INotationPtr dst = bookOf(to);
        if (!src || !dst) {
            continue;
        }
        if (src->style()->saveStyle(io::path_t(mss))) {
            dst->style()->loadStyle(io::path_t(mss), true);
        }
        engraving::Score* srcScore = src->elements()->msScore();
        engraving::Score* dstScore = dst->elements()->msScore();
        if (srcScore && dstScore) {
            dst->undoStack()->prepareChanges(TranslatableString::untranslatable("Copy layout from the standard part"));
            starscore::copyLayout(srcScore, { dstScore }, starscore::LayoutCopyOptions());
            dst->undoStack()->commitChanges();
            dst->notationChanged().notify();
        }
    }
    QFile::remove(mss);
}

// ---------------------------------------------------------------------------
//  7-Horn: Baritone Saxophone, Bass Saxophone and Bassoon versions of the Bass Trombone line
// ---------------------------------------------------------------------------

//! The stand-in versions of the 7-Horn Bass Trombone line, in the order they're made
//! The 7-Horn Bass Trombone line's stand-in versions: instrument id, band name
static const std::vector<std::pair<QString, QString> > STARSCORE_LOW_VERSIONS {
    { "baritone-saxophone", "Bari Sax" }, { "bass-saxophone", "Bass Sax" }, { "bassoon", "Bassoon" },
    { "bb-bass-clarinet", "Bass Clarinet" }, { "contrabass-clarinet", "Contrabass Clarinet" }, { "contrabassoon", "Contrabassoon" },
    { "tuba", "Tuba" },
};

void StarScoreService::makeBassHornVersions(const QString& sectionId)
{
    engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return;
    }
    QStringList bassTrombones;
    for (const StarScoreSection& s : load().sections) {
        if (s.id != sectionId) {
            continue;
        }
        for (const QString& pid : s.partIds) {
            const engraving::Part* p = ms->partById(ID(pid));
            if (p && p->instrumentId().toQString().contains("bass-trombone") && !s.alternates.count(pid)) {
                bassTrombones << pid;
            }
        }
    }
    if (bassTrombones.isEmpty()) {
        interactive()->info(muse::trc("starscore", "No Bass Trombone"),
                            muse::trc("starscore", "This section has no Bass Trombone part to make the other versions from."));
        return;
    }
    offerLowAlternates(bassTrombones, true);
}

void StarScoreService::offerLowAlternates(const QStringList& partIds, bool asked)
{
    engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return;
    }
    const Data data = load();
    QString sectionId;
    QString mainId;
    QStringList missing;   // instrument ids of the versions the line doesn't have yet
    for (const StarScoreSection& s : data.sections) {
        if (s.templateKey != "7-horn") {
            continue;
        }
        for (const QString& pid : partIds) {
            const engraving::Part* p = ms->partById(ID(pid));
            if (!p || !s.partIds.contains(pid) || !p->instrumentId().toQString().contains("bass-trombone")) {
                continue;
            }
            // The versions there now: stand-ins of this line, and any other instrument of that kind in the section
            // (a deleted part is gone from both, so it's made again)
            QStringList have;
            for (const auto& [alt, main] : s.alternates) {
                if (main == pid) {
                    if (const engraving::Part* a = ms->partById(ID(alt))) {
                        have << a->instrumentId().toQString();
                    }
                }
            }
            for (const QString& other : s.partIds) {
                if (other != pid) {
                    if (const engraving::Part* a = ms->partById(ID(other))) {
                        have << a->instrumentId().toQString();
                    }
                }
            }
            QStringList want;
            for (const auto& [id, name] : STARSCORE_LOW_VERSIONS) {
                if (!have.contains(id)) {
                    want << id;
                }
            }
            if (!want.isEmpty()) {
                sectionId = s.id;
                mainId = pid;
                missing = want;
            }
        }
    }
    if (mainId.isEmpty()) {
        if (asked) {
            QTimer::singleShot(0, &m_timerGuard, [this]() {
                interactive()->info(muse::trc("starscore", "Nothing to make"),
                                    muse::trc("starscore", "The Bass Trombone already has every other version."));
            });
        }
        return;
    }

    QStringList names;
    for (const auto& [id, name] : STARSCORE_LOW_VERSIONS) {
        if (missing.contains(id)) {
            names << name;
        }
    }
    const QString list = names.size() == 1 ? names.first()
                         : names.mid(0, names.size() - 1).join(", ") + muse::qtrc("starscore", " and ") + names.last();

    // After the menu that set the status has closed
    QTimer::singleShot(0, &m_timerGuard, [this, sectionId, mainId, missing, list]() {
        constexpr int Create = static_cast<int>(IInteractive::Button::CustomButton) + 1;
        constexpr int NotNow = static_cast<int>(IInteractive::Button::CustomButton) + 2;
        const IInteractive::Result answer = interactive()->questionSync(
            muse::trc("starscore", "Bass Trombone part finished"),
            muse::qtrc("starscore", "Create %1 parts with the same music? Each is written for its own instrument. "
                                    "Check them afterwards: notes below an instrument's range need moving up.").arg(list).toStdString(), {
            IInteractive::ButtonData(NotNow, muse::trc("starscore", "Not now")),
            IInteractive::ButtonData(Create, muse::trc("starscore", "Create parts"), true),
        }, Create);
        if (answer.button() != Create) {
            return;
        }
        const RetVal<QStringList> made = createLowAlternates(sectionId, mainId, missing);
        if (!made.ret) {
            interactive()->error(muse::trc("starscore", "Couldn't create the parts"), made.ret.toString());
            return;
        }
        interactive()->info(muse::trc("starscore", "Parts created"),
                            muse::qtrc("starscore", "%1 now have the Bass Trombone's music, below it in the 7-Horn section, and their status "
                                                    "is Needs review. They show and hide with the 7-Horn section and are in the "
                                                    "7-Horn arrangement's score. Their part scores are open, with the Bass "
                                                    "Trombone part's page breaks, system breaks and system locks. Notes too low "
                                                    "for an instrument are colored as out of range.").arg(list).toStdString());
    });
}

RetVal<QStringList> StarScoreService::createLowAlternates(const QString& sectionId, const QString& mainPartId,
                                                          const QStringList& instrumentIds)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms || !ms->firstMeasure()) {
        return RetVal<QStringList>::make_ret(Ret::Code::InternalError);
    }
    engraving::Part* mainPart = ms->partById(ID(mainPartId));
    if (!mainPart || mainPart->staves().empty()) {
        return RetVal<QStringList>::make_ret(Ret::Code::UnknownError);
    }

    std::vector<StarScoreInstrument> wanted;
    for (const auto& [id, name] : STARSCORE_LOW_VERSIONS) {
        if (instrumentIds.contains(id)) {
            StarScoreInstrument inst;
            inst.instrumentId = id;
            inst.partName = name;
            inst.hidden = false;   // shown like the rest of the 7-Horn section (set below)
            wanted.push_back(inst);
        }
    }
    if (wanted.empty()) {
        return RetVal<QStringList>::make_ret(Ret::Code::UnknownError);
    }

    // Whether the 7-Horn section is showing now (any of its instruments visible): the new versions match it
    bool sectionOn = false;
    {
        const Data d0 = load();
        for (const StarScoreSection& s0 : d0.sections) {
            if (s0.id != sectionId) {
                continue;
            }
            for (const QString& pid : s0.partIds) {
                if (const engraving::Part* p = ms->partById(ID(pid))) {
                    sectionOn |= p->show();
                }
            }
        }
    }

    // Right after the Bass Trombone
    std::set<QString> before;
    PartInstrumentList list;
    int insertAt = -1;
    for (const engraving::Part* p : ms->parts()) {
        before.insert(idText(p));
        PartInstrument pi;
        pi.isExistingPart = true;
        pi.partId = p->id();
        list << pi;
        if (p == mainPart) {
            insertAt = int(list.size());
        }
    }
    std::vector<StarScoreInstrument> added;
    for (const StarScoreInstrument& inst : wanted) {
        const InstrumentTemplate& tpl = instrumentsRepository()->instrumentTemplate(String::fromQString(inst.instrumentId));
        if (tpl.id.isEmpty()) {
            LOGW() << "[starscore] unknown instrument " << inst.instrumentId;
            continue;
        }
        PartInstrument pi;
        pi.isExistingPart = false;
        pi.instrumentTemplate = tpl;
        if (insertAt < 0 || insertAt > int(list.size())) {
            list << pi;
        } else {
            list.insert(insertAt++, pi);
        }
        added.push_back(inst);
    }
    if (added.empty()) {
        return RetVal<QStringList>::make_ret(Ret::Code::UnknownError);
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
        return RetVal<QStringList>::make_ret(Ret::Code::UnknownError);
    }

    // Same music as the Bass Trombone (pasting keeps the sounding pitch, so each saxophone's part is transposed)
    mainPart = ms->partById(ID(mainPartId));
    if (mainPart && !mainPart->staves().empty()) {
        engraving::Segment* start = ms->firstMeasure()->first(engraving::SegmentType::ChordRest);
        const engraving::staff_idx_t src = mainPart->staves().front()->idx();
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Saxophone versions of the Bass Trombone"));
        for (engraving::Part* to : newParts) {
            if (to->staves().empty()) {
                continue;
            }
            engraving::Selection sel(ms);
            sel.setRange(start, nullptr, src, src + 1);
            const ByteArray mime = sel.mimeData();
            if (mime.empty()) {
                continue;
            }
            engraving::XmlReader reader(mime);
            ms->pasteStaff(reader, start, to->staves().front()->idx());
        }
        // pasting brings the notes, not the barlines: the double barlines (before each repeat) too
        starscore::copyEndBarlines(ms, mainPart, newParts);
        master->notation()->undoStack()->commitChanges();
    }

    const StarScoreSection made = finishNewParts(newParts, added);

    // Hidden only while the 7-Horn section is off
    if (!sectionOn) {
        std::vector<std::pair<muse::ID, bool> > hide;
        for (const QString& pid : made.partIds) {
            hide.emplace_back(muse::ID(pid), false);
        }
        master->parts()->setPartsVisible(hide, TranslatableString::untranslatable("Hide instruments"));
    }

    Data d = load();
    for (StarScoreSection& s : d.sections) {
        if (s.id != sectionId) {
            continue;
        }
        for (const QString& pid : made.partIds) {
            if (!s.partIds.contains(pid)) {
                s.partIds << pid;
            }
            // turned on and off with the section, like its other instruments
            if (!s.shownPartIds.isEmpty() && !s.shownPartIds.contains(pid)) {
                s.shownPartIds << pid;
            }
            s.alternates[pid] = mainPartId;
            d.partStatus[pid] = statusKey(StarScoreStatus::NeedsReview);
        }
    }
    d.alternatesInSection = true;
    d.alternateBarlinesMatched = true;
    store(d);
    // named like the rest of the section ("7H: Bari Sax")
    standardizeHornNames();
    // the 7-Horn arrangement's own score gets them too
    syncArrangementScores();
    applyStyles(made.partIds);

    // Their part scores: the Bass Trombone part's layout (page and system breaks, system locks), the exported title,
    // and open, the first one showing
    auto bookOf = [&](const engraving::Part* part) -> IExcerptNotationPtr {
        IExcerptNotationPtr found;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            engraving::Excerpt* ex = importExcerptOf(e);
            if (!ex) {
                continue;
            }
            const std::vector<engraving::Part*> ps = masterPartsOf(ex);
            if (ps.size() == 1 && ps.front() == part && (!found || e->name() == part->partName().toQString())) {
                found = e;
            }
        }
        return found;
    };
    mainPart = ms->partById(ID(mainPartId));
    const IExcerptNotationPtr mainBook = mainPart ? bookOf(mainPart) : nullptr;
    std::vector<INotationPtr> newBooks;
    for (const QString& pid : made.partIds) {
        const engraving::Part* p = ms->partById(ID(pid));
        const IExcerptNotationPtr book = p ? bookOf(p) : nullptr;
        if (book && book->notation()) {
            newBooks.push_back(book->notation());
        }
    }
    if (mainBook && mainBook->notation()) {
        const engraving::Score* src = mainBook->notation()->elements()->msScore();
        std::vector<engraving::Score*> targets;
        for (const INotationPtr& n : newBooks) {
            if (engraving::Score* es = n->elements()->msScore()) {
                targets.push_back(es);
            }
        }
        if (src && !targets.empty()) {
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Copy part formatting"));
            starscore::copyLayout(src, targets, starscore::LayoutCopyOptions());
            // and where the texts sit ("Final soloist continues playing", "End solo", the tempo mark)
            for (engraving::Score* t : targets) {
                starscore::copyTextPositions(src, t);
            }
            master->notation()->undoStack()->commitChanges();
        }
    }
    labelPartBooks();
    for (const INotationPtr& n : newBooks) {
        master->setExcerptIsOpen(n, true);
    }
    if (!newBooks.empty()) {
        globalContext()->setCurrentNotation(newBooks.front());
    }

    const QStringList newIds = made.partIds;
    QTimer::singleShot(1500, &m_timerGuard, [this, newIds]() { applyMixerDefaults(newIds); });
    QTimer::singleShot(4000, &m_timerGuard, [this, newIds]() { applyMixerDefaults(newIds); });

    master->notation()->notationChanged().notify();
    scheduleChanged();
    return RetVal<QStringList>::make_ok(made.partIds);
}

//! Files from before 1.15.1 made the stand-in versions (Bari Sax, Bass Sax, Bassoon) hidden and left them out of
//! the section's shown instruments. Once per file: they join their section — shown when it's on, turned on and
//! off with it, and in its arrangement scores.
void StarScoreService::showOldAlternates()
{
    if (!m_mainProject || globalContext()->currentProject() != m_mainProject) {
        return;
    }
    engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return;
    }
    Data d = load();
    if (d.alternatesInSection) {
        return;
    }
    bool any = false;
    std::vector<std::pair<muse::ID, bool> > show;
    for (StarScoreSection& s : d.sections) {
        if (s.alternates.empty()) {
            continue;
        }
        any = true;
        bool sectionOn = false;
        for (const QString& pid : s.partIds) {
            const engraving::Part* p = ms->partById(ID(pid));
            if (p && !s.alternates.count(pid)) {
                sectionOn |= p->show();
            }
        }
        for (const auto& [alt, main] : s.alternates) {
            if (!s.shownPartIds.isEmpty() && !s.shownPartIds.contains(alt)) {
                s.shownPartIds << alt;
            }
            const engraving::Part* p = ms->partById(ID(alt));
            if (p && sectionOn && !p->show()) {
                show.emplace_back(ID(alt), true);
            }
        }
    }
    if (!any) {
        return;   // nothing to fix, and the file stays unchanged
    }
    d.alternatesInSection = true;
    store(d);
    if (!show.empty()) {
        if (IMasterNotationPtr master = globalContext()->currentMasterNotation()) {
            master->parts()->setPartsVisible(show, TranslatableString::untranslatable("Show instruments"));
        }
    }
    syncArrangementScores();
}

//! Horn parts are named by their section and the band's name for the horn: "7H: Trumpet 1", "7H: Bari Sax",
//! "3H: Tenor Sax", "1H: Alto Sax", "3H Flexible: Horn 1". The part's name in the score and its part score's name
//! follow. (The exported sheets have names of their own: "BALK - Trumpet 1.pdf", printed "Trumpet 1 in B♭".)
int StarScoreService::standardizeHornNames()
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return 0;
    }
    const Data d = load();
    static const QRegularExpression keyRe("^(\\d)-horn(-any)?$");
    auto bare = [](const QString& name) {
        const int at = name.lastIndexOf(": ");
        return (at >= 0 ? name.mid(at + 2) : name).trimmed();
    };

    std::vector<std::pair<engraving::Part*, QString> > renames;
    for (const StarScoreSection& s : d.sections) {
        const QRegularExpressionMatch m = keyRe.match(s.templateKey);
        if (!m.hasMatch()) {
            continue;
        }
        const bool flexible = !m.captured(2).isEmpty();
        const QString prefix = m.captured(1) + "H" + (flexible ? QString(" Flexible") : QString());

        std::vector<engraving::Part*> parts;   // in score order
        for (engraving::Part* p : ms->parts()) {
            if (s.partIds.contains(idText(p))) {
                parts.push_back(p);
            }
        }
        std::vector<QString> bases;
        std::vector<bool> numbered;
        std::map<QString, int> counts;
        for (engraving::Part* p : parts) {
            QString base = flexible ? QString() : starscore::bandHornName(p->instrumentId().toQString());
            const bool byHorn = !base.isEmpty();
            if (!byHorn) {
                base = bare(p->partName().toQString());   // "Horn 1", "Horn 1 (Flute)", or a name set by hand
            }
            bases.push_back(base);
            numbered.push_back(byHorn);
            if (byHorn) {
                counts[base]++;
            }
        }
        std::map<QString, int> seen;
        for (size_t i = 0; i < parts.size(); ++i) {
            QString name = bases[i];
            if (name.isEmpty()) {
                continue;
            }
            if (numbered[i] && counts[name] > 1) {
                name += QString(" %1").arg(++seen[bases[i]]);
            }
            const QString full = prefix + ": " + name;
            if (parts[i]->partName().toQString() != full) {
                renames.emplace_back(parts[i], full);
            }
        }
    }

    if (renames.empty()) {
        return 0;
    }
    // One edit for all of them: renaming through the Instruments panel's route lays out the score and every part
    // score after each name (Balkan Wedding: 35 names x 48 scores, which kept StarScore busy for minutes on opening)
    std::map<const engraving::Part*, QString> olds;
    master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Horn part names"));
    for (const auto& [p, name] : renames) {
        olds[p] = p->partName().toQString();
        engraving::EditPart::setInstrumentName(ms, p, engraving::Fraction(0, 1), String::fromQString(name));
        p->setPartName(String::fromQString(name));
    }
    master->notation()->undoStack()->commitChanges();

    for (const auto& [p, name] : renames) {
        const QString old = olds[p];
        // its part score follows: the one named like the part, or its only one
        IExcerptNotationPtr book;
        int books = 0;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            engraving::Excerpt* ex = importExcerptOf(e);
            if (!ex) {
                continue;
            }
            const std::vector<engraving::Part*> ps = masterPartsOf(ex);
            if (ps.size() == 1 && ps.front() == p) {
                ++books;
                if (!book || e->name() == old) {
                    book = e;
                }
            }
        }
        if (book && (books == 1 || book->name() == old) && book->name() != name) {
            book->setName(name);
        }
    }
    if (INotationProjectPtr project = globalContext()->currentProject()) {
        project->markAsUnsaved();
    }
    master->parts()->partsChanged().notify();
    master->notation()->notationChanged().notify();
    scheduleChanged();
    return int(renames.size());
}

//! On opening a file: stand-in versions from older files shown with their section, their barlines like the Bass
//! Trombone's, horn parts named by the scheme, and the horn part scores showing their exported title
void StarScoreService::tidyOpenedScore()
{
    if (!m_mainProject || globalContext()->currentProject() != m_mainProject) {
        return;
    }
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms || load().sections.empty()) {
        return;
    }
    showOldAlternates();   // may store, so the data is read after it

    // Stand-in versions made before 1.15.3 lost the double barlines of the line they stand in for: matched once per
    // file, then left alone (Joel adjusts stand-in parts by hand after they're made)
    Data d = load();
    if (!d.alternateBarlinesMatched) {
        std::map<QString, std::vector<engraving::Part*> > altsOf;   // main part id -> its stand-ins
        for (const StarScoreSection& s : d.sections) {
            for (const auto& [alt, main] : s.alternates) {
                if (engraving::Part* a = ms->partById(ID(alt))) {
                    altsOf[main].push_back(a);
                }
            }
        }
        if (!altsOf.empty()) {
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Barlines like the Bass Trombone"));
            int changed = 0;
            for (const auto& [main, alts] : altsOf) {
                changed += starscore::copyEndBarlines(ms, ms->partById(ID(main)), alts);
            }
            master->notation()->undoStack()->commitChanges();
            if (changed) {
                master->notation()->notationChanged().notify();
            }
            d.alternateBarlinesMatched = true;
            store(d);
        }
    }

    // Tempo marks in the text style's font, title frames of a fixed height
    if (starscore::tidyTempoAndFrames(ms, false) > 0) {
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Tempo fonts and title frames"));
        starscore::tidyTempoAndFrames(ms, true);
        master->notation()->undoStack()->commitChanges();
        master->notation()->notationChanged().notify();
    }

    // Barlines the same on every staff (a staff added later starts with plain ones)
    if (starscore::syncEndBarlines(ms, false) > 0) {
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Barlines the same on every staff"));
        starscore::syncEndBarlines(ms, true);
        master->notation()->undoStack()->commitChanges();
        master->notation()->notationChanged().notify();
    }

    standardizeHornNames();
    labelPartBooks();
}
