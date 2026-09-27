/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "starscoreservice.h"

#include <algorithm>
#include <set>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"

#include "notation/inotationparts.h"
#include "notation/internal/excerptnotation.h"

#include "inotationproject.h"

#include "log.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;

static const muse::String STARSCORE_META_TAG(u"starscore");

//! Instruments that make up the rhythm section when sections are detected from an existing score
static const std::set<QString> STARSCORE_RHYTHM_INSTRUMENTS {
    "piano", "grand-piano", "upright-piano", "electric-piano", "clavinet", "harpsichord", "organ", "hammond-organ",
    "pipe-organ", "synthesizer", "brightness-synth", "bass-synthesizer", "goblins-synth", "keyboard",
    "electric-guitar", "electric-guitar-treble-clef", "guitar-steel", "guitar-nylon", "acoustic-guitar",
    "bass-guitar", "electric-bass", "fretless-electric-bass", "acoustic-bass", "contrabass", "double-bass",
    "drumset", "drum-kit", "congas", "bongos", "percussion", "timbales", "cajon", "vibraphone", "marimba"
};

StarScoreService::StarScoreService(const modularity::ContextPtr& iocCtx)
    : Contextable(iocCtx)
{
}

void StarScoreService::init()
{
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        listenCurrentProject();
        m_changed.notify();
    });
}

void StarScoreService::listenCurrentProject()
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master) {
        return;
    }

    master->parts()->partsChanged().onNotify(this, [this]() {
        m_changed.notify();
    });

    master->notation()->undoStack()->stackChanged().onNotify(this, [this]() {
        m_changed.notify();
    });
}

// ---------------------------------------------------------------------------
//  Stored data
// ---------------------------------------------------------------------------

QString StarScoreService::statusKey(StarScoreStatus status)
{
    switch (status) {
    case StarScoreStatus::Empty: return "empty";
    case StarScoreStatus::Sketch: return "sketch";
    case StarScoreStatus::InProgress: return "in-progress";
    case StarScoreStatus::NeedsReview: return "needs-review";
    case StarScoreStatus::Finished: return "finished";
    }
    return "in-progress";
}

StarScoreStatus StarScoreService::statusFromKey(const QString& key)
{
    if (key == "empty") {
        return StarScoreStatus::Empty;
    } else if (key == "sketch") {
        return StarScoreStatus::Sketch;
    } else if (key == "needs-review") {
        return StarScoreStatus::NeedsReview;
    } else if (key == "finished") {
        return StarScoreStatus::Finished;
    }
    return StarScoreStatus::InProgress;
}

StarScoreService::Data StarScoreService::fromJson(const QString& json)
{
    Data data;
    if (json.trimmed().isEmpty()) {
        return data;
    }

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        LOGW() << "[starscore] ignoring unreadable starscore data: " << err.errorString();
        return data;
    }

    const QJsonObject root = doc.object();

    for (const QJsonValue& v : root.value("sections").toArray()) {
        const QJsonObject o = v.toObject();
        StarScoreSection s;
        s.id = o.value("id").toString();
        s.name = o.value("name").toString();
        s.templateKey = o.value("template").toString("custom");
        s.status = statusFromKey(o.value("status").toString());
        for (const QJsonValue& p : o.value("parts").toArray()) {
            s.partIds << p.toString();
        }
        for (const QJsonValue& p : o.value("shown").toArray()) {
            s.shownPartIds << p.toString();
        }
        if (!s.id.isEmpty()) {
            data.sections.push_back(s);
        }
    }

    for (const QJsonValue& v : root.value("arrangements").toArray()) {
        const QJsonObject o = v.toObject();
        StarScoreArrangement a;
        a.id = o.value("id").toString();
        a.name = o.value("name").toString();
        a.templateKey = o.value("template").toString();
        for (const QJsonValue& s : o.value("sections").toArray()) {
            a.sectionIds << s.toString();
        }
        if (!a.id.isEmpty()) {
            data.arrangements.push_back(a);
        }
    }

    return data;
}

QString StarScoreService::toJson(const Data& data)
{
    QJsonArray sections;
    for (const StarScoreSection& s : data.sections) {
        QJsonObject o;
        o["id"] = s.id;
        o["name"] = s.name;
        o["template"] = s.templateKey;
        o["status"] = statusKey(s.status);
        o["parts"] = QJsonArray::fromStringList(s.partIds);
        o["shown"] = QJsonArray::fromStringList(s.shownPartIds);
        sections.append(o);
    }

    QJsonArray arrangements;
    for (const StarScoreArrangement& a : data.arrangements) {
        QJsonObject o;
        o["id"] = a.id;
        o["name"] = a.name;
        o["template"] = a.templateKey;
        o["sections"] = QJsonArray::fromStringList(a.sectionIds);
        arrangements.append(o);
    }

    QJsonObject root;
    root["version"] = 1;
    root["sections"] = sections;
    root["arrangements"] = arrangements;

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

mu::engraving::MasterScore* StarScoreService::masterScore() const
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    return master ? master->masterScore() : nullptr;
}

StarScoreService::Data StarScoreService::load() const
{
    const engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return {};
    }

    Data data = fromJson(ms->metaTag(STARSCORE_META_TAG).toQString());

    // Drop parts that no longer exist (deleted in the Instruments panel)
    std::set<QString> existing;
    for (const engraving::Part* p : ms->parts()) {
        existing.insert(QString::fromStdString(p->id().toStdString()));
    }
    for (StarScoreSection& s : data.sections) {
        QStringList kept;
        for (const QString& id : s.partIds) {
            if (existing.count(id)) {
                kept << id;
            }
        }
        s.partIds = kept;
        QStringList shown;
        for (const QString& id : s.shownPartIds) {
            if (existing.count(id)) {
                shown << id;
            }
        }
        s.shownPartIds = shown;
    }

    // Drop references to sections that no longer exist
    std::set<QString> sectionIds;
    for (const StarScoreSection& s : data.sections) {
        sectionIds.insert(s.id);
    }
    for (StarScoreArrangement& a : data.arrangements) {
        QStringList kept;
        for (const QString& id : a.sectionIds) {
            if (sectionIds.count(id)) {
                kept << id;
            }
        }
        a.sectionIds = kept;
    }

    return data;
}

void StarScoreService::store(const Data& data)
{
    engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return;
    }

    const String json = String::fromQString(toJson(data));
    if (ms->metaTag(STARSCORE_META_TAG) != json) {
        ms->setMetaTag(STARSCORE_META_TAG, json);
        if (INotationProjectPtr project = globalContext()->currentProject()) {
            project->markAsUnsaved();
        }
    }

    m_changed.notify();
}

QString StarScoreService::uniqueId(const QStringList& taken, const QString& base)
{
    QString b = base.isEmpty() ? QString("section") : base;
    if (!taken.contains(b)) {
        return b;
    }
    for (int i = 2;; ++i) {
        QString candidate = QString("%1-%2").arg(b).arg(i);
        if (!taken.contains(candidate)) {
            return candidate;
        }
    }
}

QString StarScoreService::idText(const engraving::Part* part)
{
    return QString::fromStdString(part->id().toStdString());
}

// ---------------------------------------------------------------------------
//  Queries
// ---------------------------------------------------------------------------

bool StarScoreService::hasScore() const
{
    return masterScore() != nullptr;
}

bool StarScoreService::isStarScoreFile() const
{
    INotationProjectPtr project = globalContext()->currentProject();
    return project && io::suffix(project->path()) == "starscore";
}

muse::async::Notification StarScoreService::changed() const
{
    return m_changed;
}

std::vector<StarScoreSection> StarScoreService::sections() const
{
    Data data = load();
    const engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return {};
    }

    // A section is "on" when any of its instruments is visible
    for (StarScoreSection& s : data.sections) {
        bool anyVisible = false;
        for (const QString& id : s.partIds) {
            const engraving::Part* p = ms->partById(ID(id));
            if (p && p->show()) {
                anyVisible = true;
                break;
            }
        }
        s.on = anyVisible;
    }

    return data.sections;
}

std::vector<StarScoreArrangement> StarScoreService::arrangements() const
{
    return load().arrangements;
}

QStringList StarScoreService::onSectionIds(const Data&) const
{
    QStringList ids;
    for (const StarScoreSection& s : sections()) {
        if (s.on) {
            ids << s.id;
        }
    }
    return ids;
}

QString StarScoreService::activeArrangementId() const
{
    const Data data = load();
    QStringList on = onSectionIds(data);
    std::sort(on.begin(), on.end());

    for (const StarScoreArrangement& a : data.arrangements) {
        QStringList ids = a.sectionIds;
        std::sort(ids.begin(), ids.end());
        if (!ids.isEmpty() && ids == on) {
            return a.id;
        }
    }
    return QString();
}

StarScoreStatus StarScoreService::arrangementStatus(const QString& arrangementId) const
{
    const Data data = load();
    StarScoreStatus result = StarScoreStatus::Finished;
    bool any = false;

    for (const StarScoreArrangement& a : data.arrangements) {
        if (a.id != arrangementId) {
            continue;
        }
        for (const StarScoreSection& s : data.sections) {
            if (a.sectionIds.contains(s.id)) {
                any = true;
                result = std::min(result, s.status);
            }
        }
    }

    return any ? result : StarScoreStatus::Empty;
}

std::vector<StarScorePartInfo> StarScoreService::parts() const
{
    std::vector<StarScorePartInfo> result;
    const engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return result;
    }

    const Data data = load();
    for (const engraving::Part* p : ms->parts()) {
        StarScorePartInfo info;
        info.partId = QString::fromStdString(p->id().toStdString());
        info.name = p->partName().toQString();
        if (info.name.isEmpty()) {
            info.name = p->instrument()->nameAsPlainText().toQString();
        }
        info.visible = p->show();
        for (const StarScoreSection& s : data.sections) {
            if (s.partIds.contains(info.partId)) {
                info.sectionIds << s.id;
            }
        }
        result.push_back(info);
    }
    return result;
}

// ---------------------------------------------------------------------------
//  Showing / hiding
// ---------------------------------------------------------------------------

void StarScoreService::applyOnSections(const QStringList& onIds, const QString& actionName)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return;
    }

    Data data = load();
    const std::vector<StarScoreSection> current = sections();   // with the derived "on" flag

    auto isVisible = [ms](const QString& partId) {
        const engraving::Part* p = ms->partById(ID(partId));
        return p && p->show();
    };

    std::set<QString> managed;
    std::set<QString> shown;
    bool rememberedChanged = false;

    for (StarScoreSection& s : data.sections) {
        const bool wasOn = std::any_of(current.begin(), current.end(), [&](const StarScoreSection& c) {
            return c.id == s.id && c.on;
        });
        const bool wantOn = onIds.contains(s.id);

        for (const QString& id : s.partIds) {
            managed.insert(id);
        }

        if (wantOn && wasOn) {
            // stays on: leave its instruments as they are
            for (const QString& id : s.partIds) {
                if (isVisible(id)) {
                    shown.insert(id);
                }
            }
        } else if (wantOn) {
            // turning on: bring back the instruments that were showing when it was turned off
            const QStringList& restore = s.shownPartIds.isEmpty() ? s.partIds : s.shownPartIds;
            for (const QString& id : restore) {
                shown.insert(id);
            }
        } else if (wasOn) {
            // turning off: remember which instruments were showing
            QStringList visibleNow;
            for (const QString& id : s.partIds) {
                if (isVisible(id)) {
                    visibleNow << id;
                }
            }
            if (visibleNow != s.shownPartIds) {
                s.shownPartIds = visibleNow;
                rememberedChanged = true;
            }
        }
    }

    if (rememberedChanged) {
        store(data);
    }

    std::vector<std::pair<muse::ID, bool> > changes;
    for (const engraving::Part* p : ms->parts()) {
        const QString id = idText(p);
        if (!managed.count(id)) {
            continue;
        }
        const bool visible = shown.count(id) > 0;
        if (p->show() != visible) {
            changes.emplace_back(p->id(), visible);
        }
    }

    if (changes.empty()) {
        return;
    }

    master->parts()->setPartsVisible(changes, TranslatableString::untranslatable(String::fromQString(actionName)));
    m_changed.notify();
}

void StarScoreService::showArrangement(const QString& arrangementId)
{
    for (const StarScoreArrangement& a : load().arrangements) {
        if (a.id == arrangementId) {
            applyOnSections(a.sectionIds, QString("Show arrangement “%1”").arg(a.name));
            return;
        }
    }
}

void StarScoreService::setSectionOn(const QString& sectionId, bool on)
{
    QStringList ids = onSectionIds(load());
    ids.removeAll(sectionId);
    if (on) {
        ids << sectionId;
    }
    applyOnSections(ids, on ? QString("Show section") : QString("Hide section"));
}

void StarScoreService::soloSection(const QString& sectionId)
{
    applyOnSections({ sectionId }, QString("Show only one section"));
}

void StarScoreService::setAllSectionsOn(bool on)
{
    QStringList ids;
    if (on) {
        for (const StarScoreSection& s : load().sections) {
            ids << s.id;
        }
    }
    applyOnSections(ids, on ? QString("Show all sections") : QString("Hide all sections"));
}

// ---------------------------------------------------------------------------
//  Sections
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
//  Templates
// ---------------------------------------------------------------------------

std::vector<StarScoreSectionTemplate> StarScoreService::sectionTemplates() const
{
    auto inst = [](const char* id, const char* name, bool hidden = false, std::vector<int> hiddenStaves = {}) {
        StarScoreInstrument i;
        i.instrumentId = id;
        i.partName = name;
        i.hidden = hidden;
        i.hiddenStaves = hiddenStaves;
        return i;
    };

    return {
        { "lead-sheet", "Lead Sheet", { inst("piano", "Lead", false, { 1 }) } },
        { "rhythm", "Rhythm Section", {
              inst("piano", "Piano"), inst("electric-guitar", "Electric Guitar"), inst("electric-bass", "Electric Bass"),
              inst("drumset", "Drum Kit"), inst("congas", "Congas", true) } },
        { "2-horn", "2-Horn Section", { inst("bb-trumpet", "Trumpet"), inst("tenor-saxophone", "Tenor Saxophone") } },
        { "3-horn", "3-Horn Section", { inst("bb-trumpet", "Trumpet"), inst("alto-saxophone", "Alto Saxophone"),
              inst("tenor-saxophone", "Tenor Saxophone") } },
        { "4-horn", "4-Horn Section", { inst("bb-trumpet", "Trumpet"), inst("alto-saxophone", "Alto Saxophone"),
              inst("tenor-saxophone", "Tenor Saxophone"), inst("trombone", "Trombone") } },
        { "5-horn", "5-Horn Section", { inst("bb-trumpet", "Trumpet 1"), inst("bb-trumpet", "Trumpet 2"),
              inst("alto-saxophone", "Alto Saxophone"), inst("tenor-saxophone", "Tenor Saxophone"), inst("trombone", "Trombone") } },
        { "6-horn", "6-Horn Section", { inst("bb-trumpet", "Trumpet 1"), inst("bb-trumpet", "Trumpet 2"),
              inst("soprano-saxophone", "Soprano Saxophone"), inst("alto-saxophone", "Alto Saxophone"),
              inst("tenor-saxophone", "Tenor Saxophone"), inst("trombone", "Trombone") } },
        { "7-horn", "7-Horn Section", { inst("bb-trumpet", "Trumpet 1"), inst("bb-trumpet", "Trumpet 2"),
              inst("alto-saxophone", "Alto Saxophone"), inst("tenor-saxophone", "Tenor Saxophone 1"),
              inst("tenor-saxophone", "Tenor Saxophone 2"), inst("trombone", "Trombone"), inst("bass-trombone", "Bass Trombone") } },
        { "2-horn-any", "2-Horn Any", { inst("c-trumpet", "Horn 1 in C"), inst("tenor-saxophone", "Horn 2 in C") } },
        { "3-horn-any", "3-Horn Any", { inst("c-trumpet", "Horn 1 in C"), inst("alto-saxophone", "Horn 2 in C"),
              inst("tenor-saxophone", "Horn 3 in C") } },
    };
}

std::vector<StarScoreArrangementTemplate> StarScoreService::arrangementTemplates() const
{
    return {
        { "2-horn-standard", "2-Horn Standard", { "lead-sheet", "2-horn", "rhythm" } },
        { "3-horn-standard", "3-Horn Standard", { "lead-sheet", "3-horn", "rhythm" } },
        { "4-horn-standard", "4-Horn Standard", { "lead-sheet", "4-horn", "rhythm" } },
        { "5-horn-standard", "5-Horn Standard", { "lead-sheet", "5-horn", "rhythm" } },
        { "6-horn-standard", "6-Horn Standard", { "lead-sheet", "6-horn", "rhythm" } },
        { "7-horn-standard", "7-Horn Standard", { "lead-sheet", "7-horn", "rhythm" } },
        { "2-horn-any", "2-Horn Any", { "lead-sheet", "2-horn-any", "rhythm" } },
        { "3-horn-any", "3-Horn Any", { "lead-sheet", "3-horn-any", "rhythm" } },
    };
}

const StarScoreSectionTemplate* StarScoreService::sectionTemplate(const QString& key) const
{
    static std::vector<StarScoreSectionTemplate> templates;
    templates = sectionTemplates();
    for (const StarScoreSectionTemplate& t : templates) {
        if (t.key == key) {
            return &t;
        }
    }
    return nullptr;
}

void StarScoreService::addPartBooksFor(const QStringList& partIds)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master || partIds.isEmpty()) {
        return;
    }

    ExcerptNotationList excerpts = master->excerpts();
    bool added = false;

    for (const IExcerptNotationPtr& potential : master->potentialExcerpts()) {
        auto impl = std::dynamic_pointer_cast<ExcerptNotation>(potential);
        if (!impl || !impl->excerpt()) {
            continue;
        }
        const QString initialPart = QString::fromStdString(impl->excerpt()->initialPartId().toStdString());
        if (partIds.contains(initialPart)) {
            excerpts.push_back(potential);
            added = true;
        }
    }

    if (added) {
        master->setExcerpts(excerpts);
    }
}

StarScoreSection StarScoreService::finishNewParts(const std::vector<engraving::Part*>& newParts,
                                                  const std::vector<StarScoreInstrument>& instruments)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();

    StarScoreSection section;
    std::vector<std::pair<muse::ID, bool> > hide;

    for (size_t i = 0; i < newParts.size(); ++i) {
        engraving::Part* p = newParts[i];
        const QString id = idText(p);
        section.partIds << id;

        if (i >= instruments.size()) {
            section.shownPartIds << id;
            continue;
        }
        const StarScoreInstrument& inst = instruments[i];

        if (!inst.partName.isEmpty()) {
            master->parts()->setInstrumentName(InstrumentKey { p->instrumentId(), p->id(), engraving::Fraction(0, 1) }, inst.partName);
            p->setPartName(String::fromQString(inst.partName));
        }

        for (int staffIdx : inst.hiddenStaves) {
            if (staffIdx >= 0 && staffIdx < int(p->staves().size())) {
                master->parts()->setStaffVisible(p->staves().at(staffIdx)->id(), false);
            }
        }

        if (inst.hidden) {
            hide.emplace_back(p->id(), false);
        } else {
            section.shownPartIds << id;
        }
    }

    addPartBooksFor(section.partIds);

    if (!hide.empty()) {
        master->parts()->setPartsVisible(hide, TranslatableString::untranslatable("Hide instruments"));
    }

    return section;
}

RetVal<QString> StarScoreService::createSection(const QString& templateKey, const QString& name,
                                                const std::vector<StarScoreInstrument>& instruments)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return RetVal<QString>::make_ret(Ret::Code::InternalError);
    }

    std::set<QString> before;
    PartInstrumentList list;
    for (const engraving::Part* p : ms->parts()) {
        before.insert(idText(p));
        PartInstrument pi;
        pi.isExistingPart = true;
        pi.partId = p->id();
        list << pi;
    }

    std::vector<StarScoreInstrument> added;
    QStringList missing;
    for (const StarScoreInstrument& inst : instruments) {
        const InstrumentTemplate& tpl = instrumentsRepository()->instrumentTemplate(String::fromQString(inst.instrumentId));
        if (tpl.id.isEmpty()) {
            missing << inst.instrumentId;
            continue;
        }
        PartInstrument pi;
        pi.isExistingPart = false;
        pi.instrumentTemplate = tpl;
        list << pi;
        added.push_back(inst);
    }

    if (added.empty()) {
        LOGE() << "[starscore] no known instruments to add: " << missing.join(", ");
        return RetVal<QString>::make_ret(Ret::Code::UnknownError);
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

    StarScoreSection section = finishNewParts(newParts, added);

    Data data = load();
    QStringList taken;
    for (const StarScoreSection& s : data.sections) {
        taken << s.id;
    }

    section.templateKey = templateKey.isEmpty() ? QString("custom") : templateKey;
    section.id = uniqueId(taken, section.templateKey);
    section.name = name;
    section.status = StarScoreStatus::Empty;
    data.sections.push_back(section);
    store(data);

    if (!missing.isEmpty()) {
        LOGW() << "[starscore] skipped unknown instruments: " << missing.join(", ");
    }

    return RetVal<QString>::make_ok(section.id);
}

RetVal<QString> StarScoreService::createSectionFromTemplate(const QString& templateKey)
{
    const StarScoreSectionTemplate* t = sectionTemplate(templateKey);
    if (!t) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError);
    }
    return createSection(t->key, t->name, t->instruments);
}

RetVal<QString> StarScoreService::createSectionFromParts(const QString& name, const QStringList& partIds)
{
    if (!hasScore()) {
        return RetVal<QString>::make_ret(Ret::Code::InternalError);
    }

    Data data = load();
    QStringList taken;
    for (const StarScoreSection& s : data.sections) {
        taken << s.id;
    }

    StarScoreSection section;
    section.id = uniqueId(taken, "custom");
    section.name = name;
    section.templateKey = "custom";
    section.partIds = partIds;
    data.sections.push_back(section);
    store(data);

    return RetVal<QString>::make_ok(section.id);
}

Ret StarScoreService::newStarScore(const StarScoreNewOptions& options)
{
    const StarScoreArrangementTemplate* arrangement = nullptr;
    const std::vector<StarScoreArrangementTemplate> arrangementTpls = arrangementTemplates();
    for (const StarScoreArrangementTemplate& a : arrangementTpls) {
        if (a.key == options.arrangementTemplateKey) {
            arrangement = &a;
        }
    }
    if (!arrangement) {
        arrangement = &arrangementTpls.at(1); // 3-Horn Standard
    }

    // All instruments of the arrangement's sections, in section order
    struct Planned {
        QString sectionKey;
        StarScoreInstrument instrument;
    };
    std::vector<Planned> planned;
    const std::vector<StarScoreSectionTemplate> sectionTpls = sectionTemplates();

    ProjectCreateOptions projectOptions;
    projectOptions.title = options.title;
    projectOptions.composer = options.composer;

    ScoreCreateOptions& score = projectOptions.scoreOptions;
    score.withTempo = options.tempoBpm > 0;
    score.tempo.valueBpm = options.tempoBpm;
    score.tempo.duration = engraving::DurationType::V_QUARTER;
    score.globalTimesig = engraving::Fraction(options.timeSigNumerator, options.timeSigDenominator);
    score.key = static_cast<engraving::Key>(std::clamp(options.keyFifths, -7, 7));
    score.totalMeasures = std::max(1, options.measures);
    score.order = customOrder();
    score.order.customized = true;

    for (const QString& sectionKey : arrangement->sectionKeys) {
        for (const StarScoreSectionTemplate& t : sectionTpls) {
            if (t.key != sectionKey) {
                continue;
            }
            for (const StarScoreInstrument& inst : t.instruments) {
                const InstrumentTemplate& tpl = instrumentsRepository()->instrumentTemplate(String::fromQString(inst.instrumentId));
                if (tpl.id.isEmpty()) {
                    LOGW() << "[starscore] unknown instrument: " << inst.instrumentId;
                    continue;
                }
                PartInstrument pi;
                pi.instrumentTemplate = tpl;
                score.parts << pi;
                planned.push_back({ sectionKey, inst });
            }
        }
    }

    INotationProjectPtr project = projectCreator()->newProject(iocContext());
    Ret ret = project->createNew(projectOptions);
    if (!ret) {
        return ret;
    }

    globalContext()->setCurrentProject(project);

    engraving::MasterScore* ms = masterScore();
    if (!ms || ms->parts().size() != planned.size()) {
        LOGE() << "[starscore] new score has an unexpected number of instruments";
        return make_ok();
    }

    Data data;
    QStringList takenIds;
    size_t index = 0;
    for (const QString& sectionKey : arrangement->sectionKeys) {
        std::vector<engraving::Part*> parts;
        std::vector<StarScoreInstrument> instruments;
        while (index < planned.size() && planned[index].sectionKey == sectionKey) {
            parts.push_back(ms->parts().at(index));
            instruments.push_back(planned[index].instrument);
            ++index;
        }
        if (parts.empty()) {
            continue;
        }

        StarScoreSection section = finishNewParts(parts, instruments);
        section.templateKey = sectionKey;
        section.id = uniqueId(takenIds, sectionKey);
        section.status = StarScoreStatus::Empty;
        for (const StarScoreSectionTemplate& t : sectionTpls) {
            if (t.key == sectionKey) {
                section.name = t.name;
            }
        }
        takenIds << section.id;
        data.sections.push_back(section);
    }

    StarScoreArrangement a;
    a.id = "arr-" + arrangement->key;
    a.name = arrangement->name;
    a.templateKey = arrangement->key;
    for (const StarScoreSection& s : data.sections) {
        a.sectionIds << s.id;
    }
    data.arrangements.push_back(a);

    store(data);
    return make_ok();
}

void StarScoreService::setSectionStatus(const QString& sectionId, StarScoreStatus status)
{
    Data data = load();
    for (StarScoreSection& s : data.sections) {
        if (s.id == sectionId) {
            s.status = status;
        }
    }
    store(data);
}

void StarScoreService::renameSection(const QString& sectionId, const QString& name)
{
    Data data = load();
    for (StarScoreSection& s : data.sections) {
        if (s.id == sectionId && !name.trimmed().isEmpty()) {
            s.name = name.trimmed();
        }
    }
    store(data);
}

void StarScoreService::setSectionParts(const QString& sectionId, const QStringList& partIds)
{
    Data data = load();
    for (StarScoreSection& s : data.sections) {
        if (s.id == sectionId) {
            s.partIds = partIds;
        }
    }
    store(data);
}

void StarScoreService::moveSection(const QString& sectionId, int newIndex)
{
    Data data = load();
    auto it = std::find_if(data.sections.begin(), data.sections.end(), [&](const StarScoreSection& s) { return s.id == sectionId; });
    if (it == data.sections.end()) {
        return;
    }
    StarScoreSection s = *it;
    data.sections.erase(it);
    newIndex = std::clamp(newIndex, 0, int(data.sections.size()));
    data.sections.insert(data.sections.begin() + newIndex, s);
    store(data);
}

void StarScoreService::removePartsKeepingSystemObjects(const QStringList& partIdsToRemove)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms || partIdsToRemove.isEmpty()) {
        return;
    }

    PartInstrumentList survivors;
    PartInstrumentList doomed;
    for (const engraving::Part* p : ms->parts()) {
        PartInstrument pi;
        pi.isExistingPart = true;
        pi.partId = p->id();
        if (partIdsToRemove.contains(QString::fromStdString(p->id().toStdString()))) {
            doomed << pi;
        } else {
            survivors << pi;
        }
    }

    if (survivors.isEmpty() || doomed.isEmpty()) {
        return; // never delete every instrument
    }

    engraving::ScoreOrder order = master->parts()->scoreOrder();
    order.customized = true;

    // Tempo marks, rehearsal marks, jumps and voltas live on the top staff. Move the survivors to
    // the top first so those stay with the score when the removed instruments go.
    PartInstrumentList sorted = survivors;
    sorted << doomed;
    master->parts()->setParts(sorted, order);
    master->parts()->setParts(survivors, order);
}

void StarScoreService::removeSection(const QString& sectionId, bool deleteInstruments)
{
    Data data = load();
    auto it = std::find_if(data.sections.begin(), data.sections.end(), [&](const StarScoreSection& s) { return s.id == sectionId; });
    if (it == data.sections.end()) {
        return;
    }

    QStringList toDelete;
    if (deleteInstruments) {
        for (const QString& partId : it->partIds) {
            bool usedElsewhere = false;
            for (const StarScoreSection& other : data.sections) {
                if (other.id != sectionId && other.partIds.contains(partId)) {
                    usedElsewhere = true;
                }
            }
            if (!usedElsewhere) {
                toDelete << partId;
            }
        }
    }

    data.sections.erase(it);
    for (StarScoreArrangement& a : data.arrangements) {
        a.sectionIds.removeAll(sectionId);
    }
    store(data);

    removePartsKeepingSystemObjects(toDelete);
}

// ---------------------------------------------------------------------------
//  Arrangements
// ---------------------------------------------------------------------------

RetVal<QString> StarScoreService::createArrangementFromTemplate(const QString& templateKey)
{
    if (!hasScore()) {
        return RetVal<QString>::make_ret(Ret::Code::InternalError);
    }

    const std::vector<StarScoreArrangementTemplate> templates = arrangementTemplates();
    auto tpl = std::find_if(templates.begin(), templates.end(), [&](const StarScoreArrangementTemplate& t) {
        return t.key == templateKey;
    });
    if (tpl == templates.end()) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError);
    }

    // Reuse sections that already exist (same template key); create only the missing ones
    QStringList sectionIds;
    for (const QString& sectionKey : tpl->sectionKeys) {
        QString existingId;
        for (const StarScoreSection& s : load().sections) {
            if (s.templateKey == sectionKey) {
                existingId = s.id;
                break;
            }
        }
        if (existingId.isEmpty()) {
            RetVal<QString> created = createSectionFromTemplate(sectionKey);
            if (!created.ret) {
                LOGW() << "[starscore] could not create section " << sectionKey;
                continue;
            }
            existingId = created.val;
        }
        sectionIds << existingId;
    }

    // Name it after the template; "2-Horn Standard (2)" if that name is taken
    QString name = tpl->name;
    QStringList names;
    for (const StarScoreArrangement& a : load().arrangements) {
        names << a.name;
    }
    for (int i = 2; names.contains(name); ++i) {
        name = QString("%1 (%2)").arg(tpl->name).arg(i);
    }

    RetVal<QString> arr = createArrangement(name, sectionIds);
    if (!arr.ret) {
        return arr;
    }

    Data data = load();
    for (StarScoreArrangement& a : data.arrangements) {
        if (a.id == arr.val) {
            a.templateKey = tpl->key;
        }
    }
    store(data);

    showArrangement(arr.val);
    return arr;
}

RetVal<QString> StarScoreService::createArrangement(const QString& name, const QStringList& sectionIds)
{
    if (!hasScore()) {
        return RetVal<QString>::make_ret(Ret::Code::InternalError);
    }

    Data data = load();
    QStringList taken;
    for (const StarScoreArrangement& a : data.arrangements) {
        taken << a.id;
    }

    QString base = name.toLower();
    base.replace(QRegularExpression("[^a-z0-9]+"), "-");

    StarScoreArrangement a;
    a.id = uniqueId(taken, "arr-" + base);
    a.name = name;
    a.sectionIds = sectionIds;
    data.arrangements.push_back(a);
    store(data);

    return RetVal<QString>::make_ok(a.id);
}

void StarScoreService::renameArrangement(const QString& arrangementId, const QString& name)
{
    Data data = load();
    for (StarScoreArrangement& a : data.arrangements) {
        if (a.id == arrangementId && !name.trimmed().isEmpty()) {
            a.name = name.trimmed();
        }
    }
    store(data);
}

void StarScoreService::setArrangementSections(const QString& arrangementId, const QStringList& sectionIds)
{
    Data data = load();
    for (StarScoreArrangement& a : data.arrangements) {
        if (a.id == arrangementId) {
            a.sectionIds = sectionIds;
        }
    }
    store(data);
}

void StarScoreService::moveArrangement(const QString& arrangementId, int newIndex)
{
    Data data = load();
    auto it = std::find_if(data.arrangements.begin(), data.arrangements.end(),
                           [&](const StarScoreArrangement& a) { return a.id == arrangementId; });
    if (it == data.arrangements.end()) {
        return;
    }
    StarScoreArrangement a = *it;
    data.arrangements.erase(it);
    newIndex = std::clamp(newIndex, 0, int(data.arrangements.size()));
    data.arrangements.insert(data.arrangements.begin() + newIndex, a);
    store(data);
}

void StarScoreService::removeArrangement(const QString& arrangementId)
{
    Data data = load();
    data.arrangements.erase(std::remove_if(data.arrangements.begin(), data.arrangements.end(),
                                           [&](const StarScoreArrangement& a) { return a.id == arrangementId; }),
                            data.arrangements.end());
    store(data);
}

// ---------------------------------------------------------------------------
//  Detection from an existing Main Score
// ---------------------------------------------------------------------------

std::vector<mu::engraving::Part*> StarScoreService::masterPartsOf(const engraving::Excerpt* excerpt) const
{
    std::vector<engraving::Part*> result;
    engraving::MasterScore* ms = masterScore();
    if (!ms || !excerpt) {
        return result;
    }

    std::vector<engraving::Part*> candidates;
    if (excerpt->excerptScore()) {
        candidates = excerpt->excerptScore()->parts();
    } else {
        candidates = excerpt->parts();
    }

    for (engraving::Part* p : candidates) {
        engraving::Part* masterPart = nullptr;
        if (p->score() == ms) {
            masterPart = p;
        } else {
            for (engraving::Staff* staff : p->staves()) {
                if (engraving::Staff* linked = staff->findLinkedInScore(ms)) {
                    masterPart = linked->part();
                    break;
                }
            }
        }
        if (masterPart && std::find(result.begin(), result.end(), masterPart) == result.end()) {
            result.push_back(masterPart);
        }
    }

    return result;
}

int StarScoreService::detectSections()
{
    engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return 0;
    }

    Data data = load();

    QStringList takenIds;
    QStringList takenNames;
    std::set<QString> assigned;
    for (const StarScoreSection& s : data.sections) {
        takenIds << s.id;
        takenNames << s.name.toLower();
        for (const QString& p : s.partIds) {
            assigned.insert(p);
        }
    }

    auto partIdText = [](const engraving::Part* p) { return QString::fromStdString(p->id().toStdString()); };

    int added = 0;
    auto addSection = [&](const QString& key, const QString& name, const std::vector<engraving::Part*>& parts) {
        if (parts.empty() || takenNames.contains(name.toLower())) {
            return;
        }
        StarScoreSection s;
        s.id = uniqueId(takenIds, key);
        s.name = name;
        s.templateKey = key;
        s.status = StarScoreStatus::InProgress;
        for (const engraving::Part* p : parts) {
            s.partIds << partIdText(p);
            assigned.insert(partIdText(p));
        }
        takenIds << s.id;
        takenNames << name.toLower();
        data.sections.push_back(s);
        ++added;
    };

    // 1. Part books that hold a whole horn arrangement ("3-Horn Arrangement", "3H", "2-Horn Arr") or the lead sheet
    static const QRegularExpression hornRe("^\\s*([2-9])\\s*(?:-?\\s*horn|h)\\b(.*)$", QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression leadRe("^\\s*lead\\s*sheet\\s*$", QRegularExpression::CaseInsensitiveOption);

    for (const engraving::Excerpt* excerpt : ms->excerpts()) {
        const QString name = excerpt->name().toQString().trimmed();
        const std::vector<engraving::Part*> parts = masterPartsOf(excerpt);

        if (leadRe.match(name).hasMatch()) {
            addSection("lead-sheet", "Lead Sheet", parts);
            continue;
        }

        QRegularExpressionMatch m = hornRe.match(name);
        if (m.hasMatch() && parts.size() >= 2) {
            const QString n = m.captured(1);
            const QString rest = m.captured(2);
            const bool inC = rest.contains(QRegularExpression("in\\s*C\\b", QRegularExpression::CaseInsensitiveOption))
                             || rest.contains("any", Qt::CaseInsensitive);
            addSection(inC ? QString("%1-horn-any").arg(n) : QString("%1-horn").arg(n),
                       inC ? QString("%1-Horn Any").arg(n) : QString("%1-Horn Section").arg(n), parts);
        }
    }

    // 2. Rhythm section: rhythm instruments not already in a section
    std::vector<engraving::Part*> rhythm;
    for (engraving::Part* p : ms->parts()) {
        if (assigned.count(partIdText(p))) {
            continue;
        }
        if (STARSCORE_RHYTHM_INSTRUMENTS.count(p->instrumentId().toQString())) {
            rhythm.push_back(p);
        }
    }
    addSection("rhythm", "Rhythm Section", rhythm);

    // 3. One arrangement per horn section: Lead Sheet + that section + Rhythm
    QString leadId;
    QString rhythmId;
    for (const StarScoreSection& s : data.sections) {
        if (s.templateKey == "lead-sheet" && leadId.isEmpty()) {
            leadId = s.id;
        } else if (s.templateKey == "rhythm" && rhythmId.isEmpty()) {
            rhythmId = s.id;
        }
    }

    QStringList arrangementNames;
    QStringList arrangementIds;
    for (const StarScoreArrangement& a : data.arrangements) {
        arrangementNames << a.name.toLower();
        arrangementIds << a.id;
    }

    for (const StarScoreSection& s : data.sections) {
        static const QRegularExpression hornKeyRe("^([2-9])-horn(-any)?$");
        QRegularExpressionMatch km = hornKeyRe.match(s.templateKey);
        if (!km.hasMatch()) {
            continue;
        }
        const QString arrKey = km.captured(2).isEmpty() ? QString("%1-horn-standard").arg(km.captured(1)) : s.templateKey;
        const QString arrName = km.captured(2).isEmpty() ? QString("%1-Horn Standard").arg(km.captured(1)) : s.name;
        if (arrangementNames.contains(arrName.toLower())) {
            continue;
        }
        StarScoreArrangement a;
        a.id = uniqueId(arrangementIds, "arr-" + arrKey);
        a.name = arrName;
        a.templateKey = arrKey;
        if (!leadId.isEmpty()) {
            a.sectionIds << leadId;
        }
        a.sectionIds << s.id;
        if (!rhythmId.isEmpty()) {
            a.sectionIds << rhythmId;
        }
        arrangementIds << a.id;
        arrangementNames << a.name.toLower();
        data.arrangements.push_back(a);
    }

    store(data);
    return added;
}

// ---------------------------------------------------------------------------
//  Export one arrangement as a plain .mscz
// ---------------------------------------------------------------------------

Ret StarScoreService::exportArrangement(const QString& arrangementId, const io::path_t& msczPath)
{
    INotationProjectPtr project = globalContext()->currentProject();
    if (!project) {
        return make_ret(Ret::Code::InternalError);
    }

    const Data data = load();
    auto arr = std::find_if(data.arrangements.begin(), data.arrangements.end(),
                            [&](const StarScoreArrangement& a) { return a.id == arrangementId; });
    if (arr == data.arrangements.end()) {
        return make_ret(Ret::Code::UnknownError);
    }

    std::set<QString> keep;
    for (const StarScoreSection& s : data.sections) {
        if (arr->sectionIds.contains(s.id)) {
            for (const QString& p : s.partIds) {
                keep.insert(p);
            }
        }
    }
    if (keep.empty()) {
        return make_ret(Ret::Code::UnknownError);
    }

    // Work on a copy so the open score is untouched
    const QString tmpPath = QDir::temp().filePath(QString("starscore-export-%1.mscz").arg(QUuid::createUuid().toString(QUuid::Id128)));
    Ret ret = project->save(tmpPath, SaveMode::SaveCopy, false);
    if (!ret) {
        return ret;
    }

    INotationProjectPtr copy = projectCreator()->newProject(iocContext());
    ret = copy->load(tmpPath);
    if (!ret) {
        QFile::remove(tmpPath);
        return ret;
    }

    IMasterNotationPtr master = copy->masterNotation();
    engraving::MasterScore* ms = master->masterScore();

    // Part books: keep those whose instruments all belong to the arrangement
    ExcerptNotationList keptExcerpts;
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        auto impl = std::dynamic_pointer_cast<ExcerptNotation>(e);
        if (!impl || !impl->excerpt() || !impl->excerpt()->excerptScore()) {
            continue;
        }
        bool allKept = true;
        for (engraving::Part* p : impl->excerpt()->excerptScore()->parts()) {
            engraving::Part* mp = nullptr;
            for (engraving::Staff* staff : p->staves()) {
                if (engraving::Staff* linked = staff->findLinkedInScore(ms)) {
                    mp = linked->part();
                    break;
                }
            }
            if (!mp || !keep.count(QString::fromStdString(mp->id().toStdString()))) {
                allKept = false;
                break;
            }
        }
        if (allKept) {
            keptExcerpts.push_back(e);
        }
    }
    master->setExcerpts(keptExcerpts);

    // Instruments: keep the arrangement's, in score order, visible
    PartInstrumentList kept;
    PartInstrumentList dropped;
    std::vector<std::pair<muse::ID, bool> > show;
    for (const engraving::Part* p : ms->parts()) {
        PartInstrument pi;
        pi.isExistingPart = true;
        pi.partId = p->id();
        if (keep.count(QString::fromStdString(p->id().toStdString()))) {
            kept << pi;
            show.emplace_back(p->id(), true);
        } else {
            dropped << pi;
        }
    }

    engraving::ScoreOrder order = master->parts()->scoreOrder();
    order.customized = true;
    if (!dropped.isEmpty()) {
        PartInstrumentList sorted = kept;
        sorted << dropped;
        master->parts()->setParts(sorted, order);   // keep system objects on the new top staff
        master->parts()->setParts(kept, order);
    }
    master->parts()->setPartsVisible(show, TranslatableString::untranslatable("Show instruments"));

    // The exported file is a plain arrangement: no StarScore data
    ms->setMetaTag(STARSCORE_META_TAG, String());

    ret = copy->save(msczPath, SaveMode::SaveAs, false);
    QFile::remove(tmpPath);
    return ret;
}
