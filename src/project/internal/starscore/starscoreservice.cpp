/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include <QSettings>
#include "starscoreservice.h"
#include "starscorehouse.h"

#include <algorithm>
#include <map>
#include <set>

#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUuid>
#include <QStandardPaths>
#include <cmath>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/part.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/rw/xmlreader.h"

#include "serialization/zipreader.h"
#include "serialization/zipwriter.h"

#include "notation/inotationparts.h"
#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationstyle.h"

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
    installBuiltinDefaultStyle();

    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        onCurrentProjectChanged();
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

//! The engraving Excerpt behind an (initialised) part book, or nullptr
static QString starscoreArrangementScoreName(const QString& arrangementName)
{
    return arrangementName + " Score";
}

static int starscoreFindExcerpt(const ExcerptNotationList& excerpts, const QString& name)
{
    if (name.isEmpty()) {
        return -1;
    }
    for (size_t i = 0; i < excerpts.size(); ++i) {
        if (excerpts[i]->name() == name) {
            return int(i);
        }
    }
    return -1;
}

static mu::engraving::Excerpt* starscoreExcerptOf(const IExcerptNotationPtr& excerptNotation)
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
    case StarScoreStatus::FinishedLeadSheetParts: return "finished-lead-sheet-parts";
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
    } else if (key == "finished-lead-sheet-parts") {
        return StarScoreStatus::FinishedLeadSheetParts;
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

    for (const QJsonValue& v : root.value("solos").toArray()) {
        const QJsonObject o = v.toObject();
        StarScoreSolo solo;
        solo.id = o.value("id").toString();
        solo.name = o.value("name").toString();
        solo.file = o.value("file").toString();
        solo.startBar = o.value("startBar").toInt(1);
        solo.endBar = o.value("endBar").toInt(1);
        solo.passes = o.value("passes").toInt(1);
        solo.soloBars = o.value("soloBars").toInt(0);
        if (!solo.id.isEmpty()) {
            data.solos.push_back(solo);
        }
    }

    data.version = root.value("scoreVersion").toString();

    for (const QJsonValue& v : root.value("arrangements").toArray()) {
        const QJsonObject o = v.toObject();
        StarScoreArrangement a;
        a.id = o.value("id").toString();
        a.name = o.value("name").toString();
        a.templateKey = o.value("template").toString();
        a.scoreName = o.value("score").toString();
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
        if (!a.scoreName.isEmpty()) {
            o["score"] = a.scoreName;
        }
        arrangements.append(o);
    }

    QJsonArray solos;
    for (const StarScoreSolo& solo : data.solos) {
        QJsonObject o;
        o["id"] = solo.id;
        o["name"] = solo.name;
        o["file"] = solo.file;
        o["startBar"] = solo.startBar;
        o["endBar"] = solo.endBar;
        o["passes"] = solo.passes;
        o["soloBars"] = solo.soloBars;
        solos.append(o);
    }

    QJsonObject root;
    root["version"] = 1;
    root["sections"] = sections;
    root["arrangements"] = arrangements;
    if (!data.version.isEmpty()) {
        root["scoreVersion"] = data.version;
    }
    if (!solos.isEmpty()) {
        root["solos"] = solos;
    }

    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

mu::engraving::MasterScore* StarScoreService::masterScore() const
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    return master ? master->masterScore() : nullptr;
}

StarScoreService::Data StarScoreService::load() const
{
    return loadFrom(masterScore());
}

StarScoreService::Data StarScoreService::loadFrom(const engraving::MasterScore* ms) const
{
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
    storeTo(masterScore(), data, globalContext()->currentProject());
}

void StarScoreService::storeTo(engraving::MasterScore* ms, const Data& data, const INotationProjectPtr& project)
{
    if (!ms) {
        return;
    }

    const String json = String::fromQString(toJson(data));
    if (ms->metaTag(STARSCORE_META_TAG) != json) {
        ms->setMetaTag(STARSCORE_META_TAG, json);
        if (project) {
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

    // A section is "on" when any of its own instruments is visible. An instrument shared with another section
    // (e.g. a soprano sax in both the 6- and 7-Horn sections) only counts when all of the section's instruments
    // are shared; otherwise showing one section would make the other look "on" too, and turning it off would
    // then remember just the shared instrument.
    std::map<QString, int> useCount;
    for (const StarScoreSection& s : data.sections) {
        for (const QString& id : s.partIds) {
            ++useCount[id];
        }
    }
    for (StarScoreSection& s : data.sections) {
        const bool hasOwn = std::any_of(s.partIds.begin(), s.partIds.end(), [&](const QString& id) { return useCount[id] == 1; });
        bool anyVisible = false;
        for (const QString& id : s.partIds) {
            if (hasOwn && useCount[id] > 1) {
                continue;
            }
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
                // "finished, drums/percussion/keys use the lead sheet" counts as finished
                result = std::min(result, std::min(s.status, StarScoreStatus::Finished));
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
            // (a remembered list with none of the section's own instruments is from an old mix-up: show everything)
            std::map<QString, int> uses;
            for (const StarScoreSection& o : data.sections) {
                for (const QString& id : o.partIds) {
                    ++uses[id];
                }
            }
            const bool rememberedOwn = std::any_of(s.shownPartIds.begin(), s.shownPartIds.end(),
                                                   [&](const QString& id) { return uses[id] == 1; });
            const bool sectionHasOwn = std::any_of(s.partIds.begin(), s.partIds.end(),
                                                   [&](const QString& id) { return uses[id] == 1; });
            const QStringList& restore = (s.shownPartIds.isEmpty() || (sectionHasOwn && !rememberedOwn))
                                         ? s.partIds : s.shownPartIds;
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
    auto chair = [](const char* id, const char* name, int minA, int maxA, int minP, int maxP, bool hidden = false) {
        StarScoreInstrument i;
        i.instrumentId = id;
        i.partName = name;
        i.hidden = hidden;
        i.minPitchA = minA;
        i.maxPitchA = maxA;
        i.minPitchP = minP;
        i.maxPitchP = maxP;
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
              inst("soprano-saxophone", "Soprano Saxophone"), inst("alto-saxophone", "Alto Saxophone"),
              inst("tenor-saxophone", "Tenor Saxophone"), inst("trombone", "Trombone"), inst("bass-trombone", "Bass Trombone") } },
        // "Any Horns": one concert-pitch staff per chair, with the chair's ranges from the Starsign Band Guide.
        // Transposed versions for each instrument are made at export time.
        { "2-horn-any", "2-Horn Any", { chair("c-trumpet", "Horn 1", 56, 80, 52, 85), chair("trombone", "Horn 2", 44, 71, 44, 74) } },
        { "3-horn-any", "3-Horn Any", { chair("c-trumpet", "Horn 1", 56, 80, 52, 85), chair("flute", "Horn 1 (Flute)", -1, -1, -1, -1, true),
              chair("c-trumpet", "Horn 2", 52, 75, 52, 85), chair("trombone", "Horn 3", 44, 71, 44, 74) } },

        // --- Big band ---
        { "bigband-saxes", "Big Band Saxophones", {
              inst("alto-saxophone", "Alto Saxophone 1"), inst("alto-saxophone", "Alto Saxophone 2"),
              inst("tenor-saxophone", "Tenor Saxophone 1"), inst("tenor-saxophone", "Tenor Saxophone 2"),
              inst("baritone-saxophone", "Baritone Saxophone") } },
        { "bigband-trumpets", "Big Band Trumpets", {
              inst("bb-trumpet", "Trumpet 1"), inst("bb-trumpet", "Trumpet 2"),
              inst("bb-trumpet", "Trumpet 3"), inst("bb-trumpet", "Trumpet 4") } },
        { "bigband-trombones", "Big Band Trombones", {
              inst("trombone", "Trombone 1"), inst("trombone", "Trombone 2"),
              inst("trombone", "Trombone 3"), inst("bass-trombone", "Bass Trombone") } },
        { "bigband-rhythm", "Big Band Rhythm", {
              inst("piano", "Piano"), inst("electric-guitar", "Guitar"), inst("contrabass", "Bass"), inst("drumset", "Drums") } },

        // --- Marching band ---
        { "marching-woodwinds", "Marching Woodwinds", {
              inst("piccolo", "Piccolo"), inst("flute", "Flute"), inst("bb-clarinet", "Clarinet 1"), inst("bb-clarinet", "Clarinet 2"),
              inst("alto-saxophone", "Alto Saxophone"), inst("tenor-saxophone", "Tenor Saxophone"),
              inst("baritone-saxophone", "Baritone Saxophone") } },
        { "marching-brass", "Marching Brass", {
              inst("bb-trumpet", "Trumpet 1"), inst("bb-trumpet", "Trumpet 2"), inst("bb-trumpet", "Trumpet 3"),
              inst("mellophone", "Mellophone"), inst("trombone", "Trombone 1"), inst("trombone", "Trombone 2"),
              inst("baritone-horn", "Baritone"), inst("sousaphone", "Sousaphone") } },
        { "marching-battery", "Marching Battery", {
              inst("marching-snare", "Snare Drum"), inst("marching-tenor-drums", "Tenor Drums"),
              inst("marching-bass-drums", "Bass Drums"), inst("marching-cymbals", "Cymbals") } },
        { "marching-front", "Front Ensemble", {
              inst("marimba", "Marimba"), inst("vibraphone", "Vibraphone"), inst("glockenspiel", "Glockenspiel"),
              inst("timpani", "Timpani") } },

        // --- Orchestra ---
        { "orch-woodwinds", "Orchestra Woodwinds", {
              inst("flute", "Flute 1"), inst("flute", "Flute 2"), inst("oboe", "Oboe 1"), inst("oboe", "Oboe 2"),
              inst("bb-clarinet", "Clarinet 1"), inst("bb-clarinet", "Clarinet 2"),
              inst("bassoon", "Bassoon 1"), inst("bassoon", "Bassoon 2") } },
        { "orch-brass", "Orchestra Brass", {
              inst("horn", "Horn 1"), inst("horn", "Horn 2"), inst("horn", "Horn 3"), inst("horn", "Horn 4"),
              inst("bb-trumpet", "Trumpet 1"), inst("bb-trumpet", "Trumpet 2"),
              inst("trombone", "Trombone 1"), inst("trombone", "Trombone 2"), inst("bass-trombone", "Bass Trombone"),
              inst("tuba", "Tuba") } },
        { "orch-percussion", "Orchestra Percussion", {
              inst("timpani", "Timpani"), inst("snare-drum", "Snare Drum"), inst("bass-drum", "Bass Drum"),
              inst("crash-cymbal", "Cymbals"), inst("glockenspiel", "Glockenspiel") } },
        { "orch-strings", "Orchestra Strings", {
              inst("violin", "Violin I"), inst("violin", "Violin II"), inst("viola", "Viola"),
              inst("violoncello", "Cello"), inst("contrabass", "Contrabass") } },
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
        { "big-band", "Big Band", { "bigband-saxes", "bigband-trumpets", "bigband-trombones", "bigband-rhythm" } },
        { "marching-band", "Marching Band", { "marching-woodwinds", "marching-brass", "marching-battery", "marching-front" } },
        { "orchestra", "Orchestra", { "orch-woodwinds", "orch-brass", "orch-percussion", "orch-strings" } },
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
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms || partIds.isEmpty()) {
        return;
    }

    // MuseScore offers one "potential" part book per instrument that has none yet, in score order
    std::set<QString> covered;
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        if (engraving::Excerpt* ex = starscoreExcerptOf(e)) {
            covered.insert(QString::fromStdString(ex->initialPartId().toStdString()));
        }
    }
    std::vector<engraving::Part*> uncovered;
    for (engraving::Part* p : ms->parts()) {
        if (!covered.count(idText(p))) {
            uncovered.push_back(p);
        }
    }

    const ExcerptNotationList& potential = master->potentialExcerpts();
    ExcerptNotationList excerpts = master->excerpts();
    bool added = false;

    if (potential.size() == uncovered.size()) {
        for (size_t i = 0; i < potential.size(); ++i) {
            if (partIds.contains(idText(uncovered[i]))) {
                excerpts.push_back(potential[i]);
                added = true;
            }
        }
    } else {
        // Fallback: match by name
        QStringList wanted;
        for (engraving::Part* p : uncovered) {
            if (partIds.contains(idText(p))) {
                wanted << p->partName().toQString();
            }
        }
        for (const IExcerptNotationPtr& e : potential) {
            const int idx = wanted.indexOf(e->name());
            if (idx >= 0) {
                wanted.removeAt(idx);
                excerpts.push_back(e);
                added = true;
            }
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

        if (engraving::Instrument* ins = p->instrument()) {
            if (inst.minPitchA >= 0) {
                ins->setMinPitchA(inst.minPitchA);
            }
            if (inst.maxPitchA >= 0) {
                ins->setMaxPitchA(inst.maxPitchA);
            }
            if (inst.minPitchP >= 0) {
                ins->setMinPitchP(inst.minPitchP);
            }
            if (inst.maxPitchP >= 0) {
                ins->setMaxPitchP(inst.maxPitchP);
            }
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

    applyStyles(section.partIds);

    if (section.templateKey.endsWith("-horn-any")) {
        fillAnyHornsFromStandard(section);
    }

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
    syncArrangementScores();
    applyStyles();
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
    syncArrangementScores();
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
    removePartsKeepingSystemObjects(globalContext()->currentMasterNotation(), partIdsToRemove);
}

void StarScoreService::removePartsKeepingSystemObjects(const IMasterNotationPtr& master, const QStringList& partIdsToRemove)
{
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
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
    syncArrangementScores();
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
    syncArrangementScores();

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
    syncArrangementScores();
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
    syncArrangementScores();
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
    // Its own score goes too
    if (IMasterNotationPtr master = globalContext()->currentMasterNotation()) {
        for (const StarScoreArrangement& a : data.arrangements) {
            if (a.id != arrangementId) {
                continue;
            }
            ExcerptNotationList excerpts = master->excerpts();
            const int idx = starscoreFindExcerpt(excerpts, a.scoreName);
            if (idx >= 0) {
                excerpts.erase(excerpts.begin() + idx);
                master->setExcerpts(excerpts);
            }
        }
    }
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
    syncArrangementScores();
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
        engraving::Excerpt* ex = starscoreExcerptOf(e);
        if (!ex || !ex->excerptScore()) {
            continue;
        }
        bool allKept = true;
        for (engraving::Part* p : ex->excerptScore()->parts()) {
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

// ---------------------------------------------------------------------------
//  Part-book styles
// ---------------------------------------------------------------------------

StarScoreService::StyleSettings StarScoreService::loadStyleSettings() const
{
    StyleSettings settings;
    QFile file(globalConfiguration()->userAppDataPath().appendingComponent("starscore_styles.json").toQString());
    if (!file.open(QIODevice::ReadOnly)) {
        return settings;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    settings.defaultStyle = root.value("defaultStyle").toString();
    settings.bandFolder = root.value("bandFolder").toString();
    settings.builtinStyleVersion = root.value("builtinStyleVersion").toInt();
    settings.exportUnticked = root.value("exportUnticked").toObject();
    for (const QJsonValue& v : root.value("rules").toArray()) {
        const QJsonObject o = v.toObject();
        settings.rules.push_back({ o.value("section").toString(), o.value("part").toString(), o.value("style").toString() });
    }
    return settings;
}

void StarScoreService::saveStyleSettings(const StyleSettings& settings)
{
    QJsonArray rules;
    for (const StarScoreStyleRule& r : settings.rules) {
        QJsonObject o;
        o["section"] = r.sectionKey;
        o["part"] = r.partName;
        o["style"] = r.stylePath;
        rules.append(o);
    }
    QJsonObject root;
    root["defaultStyle"] = settings.defaultStyle;
    root["bandFolder"] = settings.bandFolder;
    root["builtinStyleVersion"] = settings.builtinStyleVersion;
    root["exportUnticked"] = settings.exportUnticked;
    root["rules"] = rules;

    QFile file(globalConfiguration()->userAppDataPath().appendingComponent("starscore_styles.json").toQString());
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(QJsonDocument(root).toJson());
    }
}

//! Copies the bundled Starsign style next to the settings and makes it the default part-score style.
//! Done once per bundled version. A default the user chose themselves (not an older bundled one) is kept.
void StarScoreService::installBuiltinDefaultStyle()
{
    static const int BUILTIN_STYLE_VERSION = 6;   // 1 = Starsign 2.0, 2 = 2.1, 3 = 2.2, 4 = 2.3, 5 = 2.4, 6 = 2.5

    const QString dir = globalConfiguration()->userAppDataPath().appendingComponent("StarScoreStyles").toQString();
    const QString target = dir + "/Starsign 2.5.mss";

    StyleSettings settings = loadStyleSettings();
    const bool newVersion = settings.builtinStyleVersion < BUILTIN_STYLE_VERSION;
    if (!newVersion && QFileInfo::exists(target)) {
        return;
    }

    QDir().mkpath(dir);
    QFile::remove(target);
    if (!QFile::copy(":/resources/starscore/Starsign_2.5.mss", target)) {
        LOGE() << "Could not install the built-in StarScore style";
        return;
    }
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup | QFileDevice::ReadOther);

    if (newVersion) {
        const bool usingBundled = settings.defaultStyle.isEmpty() || settings.defaultStyle.startsWith(dir + "/Starsign ");
        if (usingBundled) {
            settings.defaultStyle = target;
        }
        settings.builtinStyleVersion = BUILTIN_STYLE_VERSION;
        saveStyleSettings(settings);
    }
}

QString StarScoreService::defaultStylePath() const
{
    return loadStyleSettings().defaultStyle;
}

void StarScoreService::setDefaultStylePath(const QString& path)
{
    StyleSettings settings = loadStyleSettings();
    settings.defaultStyle = path;
    saveStyleSettings(settings);
}

std::vector<StarScoreStyleRule> StarScoreService::styleRules() const
{
    return loadStyleSettings().rules;
}

void StarScoreService::setStyleRules(const std::vector<StarScoreStyleRule>& rules)
{
    StyleSettings settings = loadStyleSettings();
    settings.rules = rules;
    saveStyleSettings(settings);
}

int StarScoreService::applyStyles(const QStringList& partIds)
{
    const int restyled = applyStylesOnly(partIds);
    applyMixerDefaults(partIds);
    return restyled;
}

int StarScoreService::applyStylesOnly(const QStringList& partIds)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return 0;
    }

    syncMinMajDefaults();

    const StyleSettings settings = loadStyleSettings();
    const Data data = load();
    const QString version = scoreVersion();

    auto usable = [](const QString& path) {
        return !path.isEmpty() && QFileInfo::exists(path);
    };

    // Style file(s), then the house settings a style file can't hold, in one undo step per score
    auto restyle = [&](INotationPtr n, const QString& ruleStyle, bool partBook) {
        if (!n) {
            return false;
        }
        bool changed = false;
        if (usable(settings.defaultStyle)) {
            changed |= n->style()->loadStyle(settings.defaultStyle, true);
        }
        if (!ruleStyle.isEmpty()) {
            changed |= n->style()->loadStyle(ruleStyle, true);
        }
        engraving::Score* score = n->elements()->msScore();
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("StarScore house style"));
        starscore::applyHouseStyle(score, partBook, version);
        n->undoStack()->commitChanges();
        n->notationChanged().notify();
        return true;
    };

    int restyled = 0;

    // The main score (a workspace of every arrangement) when restyling everything
    if (partIds.isEmpty() && restyle(master->notation(), QString(), false)) {
        ++restyled;
    }

    for (const IExcerptNotationPtr& e : master->excerpts()) {
        engraving::Excerpt* ex = starscoreExcerptOf(e);
        if (!ex) {
            continue;
        }

        const std::vector<engraving::Part*> parts = masterPartsOf(ex);
        const bool partBook = parts.size() == 1;
        const engraving::Part* part = partBook ? parts.front() : nullptr;
        const QString partId = part ? idText(part) : QString();

        if (!partIds.isEmpty()) {
            bool touches = false;
            for (const engraving::Part* p : parts) {
                touches |= partIds.contains(idText(p));
            }
            if (!touches) {
                continue;
            }
        }

        QStringList sectionKeys;
        for (const StarScoreSection& s : data.sections) {
            if (!partId.isEmpty() && s.partIds.contains(partId)) {
                sectionKeys << s.templateKey;
            }
        }

        QString chosen;
        if (part) {
            const QString partName = part->partName().toQString().trimmed();
            for (const StarScoreStyleRule& r : settings.rules) {
                const bool sectionOk = r.sectionKey.isEmpty() || sectionKeys.contains(r.sectionKey);
                const bool partOk = r.partName.trimmed().isEmpty()
                                    || r.partName.trimmed().compare(partName, Qt::CaseInsensitive) == 0
                                    || r.partName.trimmed().compare(e->name().trimmed(), Qt::CaseInsensitive) == 0;
                if (sectionOk && partOk && usable(r.stylePath)) {
                    chosen = r.stylePath;   // later rules win
                }
            }
        }

        if (restyle(e->notation(), chosen, partBook)) {
            ++restyled;
        }
    }

    return restyled;
}

// ---------------------------------------------------------------------------
//  Solo transcriptions
// ---------------------------------------------------------------------------

static const QString STARSCORE_SOLOS_DIR("StarScoreSolos");

void StarScoreService::onCurrentProjectChanged()
{
    if (m_switching) {
        return;
    }

    INotationProjectPtr current = globalContext()->currentProject();
    if (!current) {
        clearSolos();
        return;
    }

    if (isSoloProject(current.get()) || current == m_mainProject) {
        return;
    }

    clearSolos();
    m_mainProject = current;

    m_mainProject->saveComplited().onReceive(this, [this](const io::path_t& path, SaveMode mode) {
        if (mode == SaveMode::Save || mode == SaveMode::SaveAs || mode == SaveMode::SaveCopy) {
            if (!solos().empty()) {
                Ret ret = injectSolos(path);
                if (!ret) {
                    LOGE() << "[starscore] could not store solos in " << path << ": " << ret.toString();
                }
            }
        }
    });

    extractSolos();
}

void StarScoreService::clearSolos()
{
    if (m_mainProject) {
        m_mainProject->saveComplited().disconnect(this);
    }
    m_soloProjects.clear();
    m_mainProject.reset();
    if (!m_workDir.isEmpty()) {
        QDir(m_workDir).removeRecursively();
        m_workDir.clear();
    }
}

io::path_t StarScoreService::soloWorkPath(const QString& soloId) const
{
    return io::path_t(m_workDir + "/" + soloId + ".mscz");
}

void StarScoreService::extractSolos()
{
    if (!m_mainProject) {
        return;
    }

    m_workDir = QDir::tempPath() + "/StarScoreSolos-" + QUuid::createUuid().toString(QUuid::Id128);
    QDir().mkpath(m_workDir);

    const std::vector<StarScoreSolo> list = solos();
    if (list.empty() || !QFileInfo::exists(m_mainProject->path().toQString())) {
        return;
    }

    ZipReader zip(m_mainProject->path());
    for (const StarScoreSolo& solo : list) {
        ByteArray data = zip.fileData(solo.file.toStdString());
        if (data.empty()) {
            LOGW() << "[starscore] solo file missing from the .starscore: " << solo.file;
            continue;
        }
        QFile out(soloWorkPath(solo.id).toQString());
        if (out.open(QIODevice::WriteOnly)) {
            out.write(data.toQByteArrayNoCopy());
        }
    }
}

std::vector<StarScoreSolo> StarScoreService::solos() const
{
    if (!m_mainProject) {
        return {};
    }
    return loadFrom(m_mainProject->masterNotation()->masterScore()).solos;
}

QString StarScoreService::currentSoloId() const
{
    INotationProjectPtr current = globalContext()->currentProject();
    for (const auto& [id, project] : m_soloProjects) {
        if (project == current) {
            return id;
        }
    }
    return QString();
}

bool StarScoreService::canAddSolos() const
{
    return m_mainProject && io::suffix(m_mainProject->path()) == "starscore" && !m_mainProject->isNewlyCreated();
}

bool StarScoreService::isSoloProject(const INotationProject* project) const
{
    for (const auto& [id, p] : m_soloProjects) {
        if (p.get() == project) {
            return true;
        }
    }
    return false;
}

bool StarScoreService::hasUnsavedSolos() const
{
    for (const auto& [id, p] : m_soloProjects) {
        if (p->needSave().val) {
            return true;
        }
    }
    return false;
}

io::path_t StarScoreService::mainProjectPath() const
{
    return m_mainProject ? m_mainProject->path() : io::path_t();
}

mu::engraving::Measure* StarScoreService::mainMeasureByNumber(int barNumber) const
{
    if (!m_mainProject) {
        return nullptr;
    }
    engraving::MasterScore* ms = m_mainProject->masterNotation()->masterScore();
    for (engraving::Measure* m = ms->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->no() + 1 == barNumber) {
            return m;
        }
    }
    return nullptr;
}

RetVal<StarScoreSoloPlan> StarScoreService::planSolo(const io::path_t& soloFile, int startBar, int endBar) const
{
    StarScoreSoloPlan plan;
    if (!m_mainProject) {
        return RetVal<StarScoreSoloPlan>::make_ret(Ret::Code::InternalError);
    }

    INotationProjectPtr probe = projectCreator()->newProject(iocContext());
    Ret ret = probe->load(soloFile);
    if (!ret) {
        return RetVal<StarScoreSoloPlan>::make_ret(ret);
    }
    for (engraving::Measure* m = probe->masterNotation()->masterScore()->firstMeasure(); m; m = m->nextMeasure()) {
        ++plan.soloBars;
    }

    engraving::Measure* start = mainMeasureByNumber(startBar);
    if (!start) {
        plan.warning = muse::qtrc("starscore", "The main score has no bar %1.").arg(startBar);
        return RetVal<StarScoreSoloPlan>::make_ok(plan);
    }
    plan.startBar = startBar;

    engraving::Measure* end = endBar > 0 ? mainMeasureByNumber(endBar) : nullptr;
    if (!end && start->repeatStart()) {
        for (engraving::Measure* m = start; m; m = m->nextMeasure()) {
            if (m->repeatEnd()) {
                end = m;
                break;
            }
        }
    }

    if (end) {
        plan.endBar = end->no() + 1;
        plan.repeated = end->repeatEnd() || start->repeatStart();
    } else {
        plan.endBar = startBar + plan.soloBars - 1;
        plan.repeated = false;
        if (!mainMeasureByNumber(plan.endBar)) {
            plan.warning = muse::qtrc("starscore", "The solo (%1 bars) runs past the end of the main score.").arg(plan.soloBars);
        }
    }

    const int sectionBars = std::max(1, plan.endBar - plan.startBar + 1);
    plan.passes = std::max(1, int(std::ceil(double(plan.soloBars) / sectionBars)));
    if (plan.soloBars % sectionBars != 0 && plan.warning.isEmpty()) {
        plan.warning = muse::qtrc("starscore", "The solo's %1 bars don't divide evenly into %2-bar passes; the last pass will be partial.")
                       .arg(plan.soloBars).arg(sectionBars);
    }

    if (plan.passes > 1) {
        plan.summary = muse::qtrc("starscore", "%1-bar solo over bars %2–%3, played %4 times. The band's bars %2–%3 are copied in %4 times.")
                       .arg(plan.soloBars).arg(plan.startBar).arg(plan.endBar).arg(plan.passes);
    } else {
        plan.summary = muse::qtrc("starscore", "%1-bar written-out solo over bars %2–%3. The band's bars %2–%3 are copied in once.")
                       .arg(plan.soloBars).arg(plan.startBar).arg(plan.endBar);
    }

    return RetVal<StarScoreSoloPlan>::make_ok(plan);
}

RetVal<INotationProjectPtr> StarScoreService::loadSoloProject(const QString& soloId)
{
    auto it = m_soloProjects.find(soloId);
    if (it != m_soloProjects.end()) {
        return RetVal<INotationProjectPtr>::make_ok(it->second);
    }

    const io::path_t path = soloWorkPath(soloId);
    if (!QFileInfo::exists(path.toQString())) {
        return RetVal<INotationProjectPtr>::make_ret(Ret::Code::UnknownError);
    }

    INotationProjectPtr project = projectCreator()->newProject(iocContext());
    Ret ret = project->load(path);
    if (!ret) {
        return RetVal<INotationProjectPtr>::make_ret(ret);
    }

    for (const StarScoreSolo& solo : solos()) {
        if (solo.id == soloId) {
            project->setDisplayNameOverride(m_mainProject->displayName() + " — " + muse::qtrc("starscore", "Solo: %1").arg(solo.name));
        }
    }

    m_soloProjects[soloId] = project;
    return RetVal<INotationProjectPtr>::make_ok(project);
}

Ret StarScoreService::buildSoloBand(const INotationProjectPtr& soloProject, const StarScoreSolo& solo)
{
    if (!m_mainProject) {
        return make_ret(Ret::Code::InternalError);
    }

    IMasterNotationPtr soloMaster = soloProject->masterNotation();
    engraving::MasterScore* soloScore = soloMaster->masterScore();
    engraving::MasterScore* mainScore = m_mainProject->masterNotation()->masterScore();
    const Data mainData = loadFrom(mainScore);

    // 1. The soloist's instruments: the "solo" section, or every instrument on first import
    Data soloData = loadFrom(soloScore);
    QStringList soloistIds;
    for (const StarScoreSection& sec : soloData.sections) {
        if (sec.templateKey == "solo") {
            soloistIds = sec.partIds;
        }
    }
    if (soloistIds.isEmpty()) {
        for (const engraving::Part* p : soloScore->parts()) {
            soloistIds << idText(p);
        }
    }

    // 2. Remove the old band copies
    QStringList oldBand;
    for (const engraving::Part* p : soloScore->parts()) {
        if (!soloistIds.contains(idText(p))) {
            oldBand << idText(p);
        }
    }
    removePartsKeepingSystemObjects(soloMaster, oldBand);

    // 3. Add one instrument per main-score instrument that belongs to a section
    struct Mapping {
        engraving::Part* mainPart = nullptr;
        QString sectionId;
    };
    std::vector<Mapping> planned;
    PartInstrumentList list;
    std::set<uint64_t> before;
    for (const engraving::Part* p : soloScore->parts()) {
        before.insert(p->id().toUint64());
        PartInstrument pi;
        pi.isExistingPart = true;
        pi.partId = p->id();
        list << pi;
    }
    std::set<QString> used;
    for (const StarScoreSection& sec : mainData.sections) {
        for (const QString& pid : sec.partIds) {
            if (used.count(pid)) {
                continue;
            }
            engraving::Part* mp = mainScore->partById(ID(pid));
            if (!mp) {
                continue;
            }
            const InstrumentTemplate& tpl = instrumentsRepository()->instrumentTemplate(mp->instrumentId());
            if (tpl.id.isEmpty()) {
                continue;
            }
            PartInstrument pi;
            pi.instrumentTemplate = tpl;
            list << pi;
            planned.push_back({ mp, sec.id });
            used.insert(pid);
        }
    }

    engraving::ScoreOrder order = soloMaster->parts()->scoreOrder();
    order.customized = true;
    soloMaster->parts()->setParts(list, order);

    std::vector<engraving::Part*> added;
    for (engraving::Part* p : soloScore->parts()) {
        if (!before.count(p->id().toUint64())) {
            added.push_back(p);
        }
    }
    if (added.size() != planned.size()) {
        LOGW() << "[starscore] solo band: expected " << planned.size() << " instruments, got " << added.size();
    }

    // names and staff visibility follow the main score
    for (size_t i = 0; i < added.size() && i < planned.size(); ++i) {
        engraving::Part* sp = added[i];
        const engraving::Part* mp = planned[i].mainPart;
        const QString name = mp->partName().toQString();
        if (!name.isEmpty()) {
            soloMaster->parts()->setInstrumentName(InstrumentKey { sp->instrumentId(), sp->id(), engraving::Fraction(0, 1) },
                                                   mp->longName().toQString().isEmpty() ? name : mp->longName().toQString());
            sp->setPartName(String::fromQString(name));
        }
        for (size_t st = 0; st < sp->staves().size() && st < mp->staves().size(); ++st) {
            if (!mp->staves().at(st)->visible()) {
                soloMaster->parts()->setStaffVisible(sp->staves().at(st)->id(), false);
            }
        }
    }

    // 4. Copy the band's bars, once per pass
    engraving::Measure* srcStart = mainMeasureByNumber(solo.startBar);
    engraving::Measure* srcEnd = mainMeasureByNumber(solo.endBar);
    if (!srcStart || !srcEnd) {
        return make_ret(Ret::Code::UnknownError);
    }
    const int sectionBars = solo.endBar - solo.startBar + 1;
    engraving::Segment* srcStartSeg = srcStart->first(engraving::SegmentType::ChordRest);
    engraving::Segment* srcEndSeg = srcEnd->nextMeasure() ? srcEnd->nextMeasure()->first(engraving::SegmentType::ChordRest) : nullptr;

    std::vector<engraving::Measure*> soloMeasures;
    for (engraving::Measure* m = soloScore->firstMeasure(); m; m = m->nextMeasure()) {
        soloMeasures.push_back(m);
    }

    soloMaster->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Copy band into solo"));
    for (size_t i = 0; i < added.size() && i < planned.size(); ++i) {
        engraving::Part* sp = added[i];
        engraving::Part* mp = planned[i].mainPart;
        const size_t nStaves = std::min(sp->staves().size(), mp->staves().size());
        if (nStaves == 0) {
            continue;
        }
        const engraving::staff_idx_t mainStaff = mp->staves().front()->idx();
        const engraving::staff_idx_t soloStaff = sp->staves().front()->idx();

        engraving::Selection sel(mainScore);
        sel.setRange(srcStartSeg, srcEndSeg, mainStaff, mainStaff + nStaves);
        const ByteArray mime = sel.mimeData();
        if (mime.empty()) {
            continue;
        }

        for (int pass = 0; pass < solo.passes; ++pass) {
            const size_t firstBar = size_t(pass * sectionBars);
            if (firstBar + size_t(sectionBars) > soloMeasures.size()) {
                break;   // no room for a full pass
            }
            engraving::Segment* dst = soloMeasures[firstBar]->first(engraving::SegmentType::ChordRest);
            engraving::XmlReader reader(mime);
            soloScore->pasteStaff(reader, dst, soloStaff);
        }
    }
    soloMaster->notation()->undoStack()->commitChanges();

    // 5. Sections and arrangements for the solo view: the soloist plus a copy of each band section
    Data newData;
    StarScoreSection soloSection;
    soloSection.id = "solo";
    soloSection.name = muse::qtrc("starscore", "Solo");
    soloSection.templateKey = "solo";
    soloSection.status = StarScoreStatus::InProgress;
    soloSection.partIds = soloistIds;
    soloSection.shownPartIds = soloistIds;
    newData.sections.push_back(soloSection);

    for (const StarScoreSection& sec : mainData.sections) {
        StarScoreSection copy = sec;
        copy.partIds.clear();
        copy.shownPartIds.clear();
        for (size_t i = 0; i < added.size() && i < planned.size(); ++i) {
            if (planned[i].sectionId == sec.id) {
                copy.partIds << idText(added[i]);
                if (sec.shownPartIds.isEmpty() || sec.shownPartIds.contains(idText(planned[i].mainPart))) {
                    copy.shownPartIds << idText(added[i]);
                }
            }
        }
        if (!copy.partIds.isEmpty()) {
            newData.sections.push_back(copy);
        }
    }
    for (const StarScoreArrangement& a : mainData.arrangements) {
        StarScoreArrangement copy = a;
        copy.sectionIds.prepend("solo");
        newData.arrangements.push_back(copy);
    }
    storeTo(soloScore, newData, soloProject);

    // 6. Show the soloist and the main score's current arrangement (or everything)
    QStringList onIds { "solo" };
    {
        // which arrangement is showing in the main score?
        std::vector<StarScoreSection> mainSections = mainData.sections;
        QStringList mainOn;
        for (const StarScoreSection& sec : mainSections) {
            for (const QString& pid : sec.partIds) {
                const engraving::Part* p = mainScore->partById(ID(pid));
                if (p && p->show()) {
                    mainOn << sec.id;
                    break;
                }
            }
        }
        onIds << mainOn;
    }
    std::vector<std::pair<muse::ID, bool> > vis;
    for (const StarScoreSection& sec : newData.sections) {
        const bool on = onIds.contains(sec.id);
        for (const QString& pid : sec.partIds) {
            vis.emplace_back(ID(pid), on && (sec.shownPartIds.isEmpty() || sec.shownPartIds.contains(pid)));
        }
    }
    soloMaster->parts()->setPartsVisible(vis, TranslatableString::untranslatable("Show sections"));

    return make_ok();
}

RetVal<QString> StarScoreService::addSolo(const io::path_t& soloFile, const QString& name, int startBar, int endBar)
{
    if (!canAddSolos()) {
        return RetVal<QString>::make_ret(make_ret(Ret::Code::NotSupported,
                                                  muse::trc("starscore", "Save the score as a .starscore file first.")));
    }

    RetVal<StarScoreSoloPlan> plan = planSolo(soloFile, startBar, endBar);
    if (!plan.ret) {
        return RetVal<QString>::make_ret(plan.ret);
    }
    if (!mainMeasureByNumber(plan.val.startBar) || !mainMeasureByNumber(plan.val.endBar)) {
        return RetVal<QString>::make_ret(make_ret(Ret::Code::UnknownError, plan.val.warning.toStdString()));
    }

    Data mainData = loadFrom(m_mainProject->masterNotation()->masterScore());
    QStringList taken;
    for (const StarScoreSolo& s : mainData.solos) {
        taken << s.id;
    }

    StarScoreSolo solo;
    solo.id = uniqueId(taken, "solo");
    solo.name = name.trimmed().isEmpty() ? QFileInfo(soloFile.toQString()).completeBaseName() : name.trimmed();
    solo.file = STARSCORE_SOLOS_DIR + "/" + solo.id + ".mscz";
    solo.startBar = plan.val.startBar;
    solo.endBar = plan.val.endBar;
    solo.passes = plan.val.passes;
    solo.soloBars = plan.val.soloBars;

    const QString work = soloWorkPath(solo.id).toQString();
    QFile::remove(work);
    if (!QFile::copy(soloFile.toQString(), work)) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError);
    }
    QFile(work).setPermissions(QFile::ReadOwner | QFile::WriteOwner);

    mainData.solos.push_back(solo);
    storeTo(m_mainProject->masterNotation()->masterScore(), mainData, m_mainProject);

    RetVal<INotationProjectPtr> project = loadSoloProject(solo.id);
    if (!project.ret) {
        return RetVal<QString>::make_ret(project.ret);
    }

    Ret ret = buildSoloBand(project.val, solo);
    if (!ret) {
        return RetVal<QString>::make_ret(ret);
    }
    project.val->save(soloWorkPath(solo.id), SaveMode::Save, false);

    return RetVal<QString>::make_ok(solo.id);
}

Ret StarScoreService::showSolo(const QString& soloId)
{
    RetVal<INotationProjectPtr> project = loadSoloProject(soloId);
    if (!project.ret) {
        return project.ret;
    }

    if (INotationPtr n = globalContext()->currentNotation()) {
        if (n->interaction()->isTextEditingStarted()) {
            n->interaction()->endEditText();
        }
    }
    if (playbackController()->isPlaying()) {
        playbackController()->reset();
    }

    m_switching = true;
    globalContext()->setCurrentProject(project.val);
    m_switching = false;
    listenCurrentProject();
    m_changed.notify();
    return make_ok();
}

Ret StarScoreService::showMainScore()
{
    if (!m_mainProject) {
        return make_ret(Ret::Code::InternalError);
    }
    if (INotationPtr n = globalContext()->currentNotation()) {
        if (n->interaction()->isTextEditingStarted()) {
            n->interaction()->endEditText();
        }
    }
    if (playbackController()->isPlaying()) {
        playbackController()->reset();
    }

    m_switching = true;
    globalContext()->setCurrentProject(m_mainProject);
    m_switching = false;
    listenCurrentProject();
    m_changed.notify();
    return make_ok();
}

Ret StarScoreService::refreshSoloBand(const QString& soloId)
{
    for (const StarScoreSolo& solo : solos()) {
        if (solo.id != soloId) {
            continue;
        }
        RetVal<INotationProjectPtr> project = loadSoloProject(soloId);
        if (!project.ret) {
            return project.ret;
        }
        Ret ret = buildSoloBand(project.val, solo);
        project.val->markAsUnsaved();
        m_changed.notify();
        return ret;
    }
    return make_ret(Ret::Code::UnknownError);
}

void StarScoreService::renameSolo(const QString& soloId, const QString& name)
{
    if (!m_mainProject || name.trimmed().isEmpty()) {
        return;
    }
    Data data = loadFrom(m_mainProject->masterNotation()->masterScore());
    for (StarScoreSolo& solo : data.solos) {
        if (solo.id == soloId) {
            solo.name = name.trimmed();
        }
    }
    storeTo(m_mainProject->masterNotation()->masterScore(), data, m_mainProject);
    if (auto it = m_soloProjects.find(soloId); it != m_soloProjects.end()) {
        it->second->setDisplayNameOverride(m_mainProject->displayName() + " — " + muse::qtrc("starscore", "Solo: %1").arg(name.trimmed()));
    }
}

void StarScoreService::removeSolo(const QString& soloId)
{
    if (!m_mainProject) {
        return;
    }
    if (currentSoloId() == soloId) {
        showMainScore();
    }
    Data data = loadFrom(m_mainProject->masterNotation()->masterScore());
    data.solos.erase(std::remove_if(data.solos.begin(), data.solos.end(),
                                    [&](const StarScoreSolo& s) { return s.id == soloId; }), data.solos.end());
    storeTo(m_mainProject->masterNotation()->masterScore(), data, m_mainProject);
    m_soloProjects.erase(soloId);
    QFile::remove(soloWorkPath(soloId).toQString());
}

Ret StarScoreService::exportSolo(const QString& soloId, const io::path_t& msczPath)
{
    RetVal<INotationProjectPtr> project = loadSoloProject(soloId);
    if (!project.ret) {
        return project.ret;
    }
    return project.val->save(msczPath, SaveMode::SaveCopy, false);
}

Ret StarScoreService::injectSolos(const io::path_t& starscorePath)
{
    // Save changed solos to their working files first
    for (auto& [id, project] : m_soloProjects) {
        if (project->needSave().val) {
            Ret ret = project->save(soloWorkPath(id), SaveMode::Save, false);
            if (!ret) {
                return ret;
            }
        }
    }

    const std::vector<StarScoreSolo> list = solos();
    const QString target = starscorePath.toQString();
    const QString tmp = target + ".solos-tmp";

    {
        ZipReader reader(starscorePath);
        if (reader.hasError()) {
            return make_ret(Ret::Code::UnknownError);
        }
        QFile::remove(tmp);
        ZipWriter writer { io::path_t(tmp) };
        for (const ZipReader::FileInfo& info : reader.fileInfoList()) {
            const std::string name = info.filePath.toStdString();
            if (!info.isFile || QString::fromStdString(name).startsWith(STARSCORE_SOLOS_DIR + "/")) {
                continue;
            }
            writer.addFile(name, reader.fileData(name));
        }
        for (const StarScoreSolo& solo : list) {
            QFile f(soloWorkPath(solo.id).toQString());
            if (!f.open(QIODevice::ReadOnly)) {
                LOGW() << "[starscore] missing working copy of solo " << solo.id;
                continue;
            }
            writer.addFile(solo.file.toStdString(), ByteArray::fromQByteArray(f.readAll()));
        }
        writer.close();
        if (writer.hasError()) {
            QFile::remove(tmp);
            return make_ret(Ret::Code::UnknownError);
        }
    }

    QFile::remove(target);
    if (!QFile::rename(tmp, target)) {
        return make_ret(Ret::Code::UnknownError);
    }
    return make_ok();
}

Ret StarScoreService::saveAll()
{
    if (!m_mainProject) {
        return make_ret(Ret::Code::InternalError);
    }

    if (m_mainProject->needSave().val) {
        // saving the main score triggers injectSolos() through saveComplited
        return m_mainProject->save(io::path_t(), SaveMode::Save, true);
    }

    Ret ret = injectSolos(m_mainProject->path());
    m_changed.notify();
    return ret;
}

// ---------------------------------------------------------------------------
//  Each arrangement's own score: a part book holding all of the arrangement's
//  instruments, since the main score is a workspace of every arrangement at once
// ---------------------------------------------------------------------------

void StarScoreService::syncArrangementScores()
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms || isSoloProject(globalContext()->currentProject().get())) {
        return;
    }

    Data data = load();
    ExcerptNotationList excerpts = master->excerpts();
    bool listChanged = false;
    bool dataChanged = false;
    std::vector<std::pair<IExcerptNotationPtr, QStringList> > created;   // new score, parts to hide in it

    for (StarScoreArrangement& a : data.arrangements) {
        std::set<QString> wanted;
        QStringList hidden;
        for (const StarScoreSection& s : data.sections) {
            if (!a.sectionIds.contains(s.id)) {
                continue;
            }
            for (const QString& pid : s.partIds) {
                wanted.insert(pid);
                // instruments the section keeps hidden (e.g. Congas) start hidden in the score too
                if (!s.shownPartIds.isEmpty() && !s.shownPartIds.contains(pid)) {
                    hidden << pid;
                }
            }
        }
        std::vector<engraving::Part*> parts;
        for (engraving::Part* p : ms->parts()) {
            if (wanted.count(idText(p))) {
                parts.push_back(p);
            }
        }

        const QString name = starscoreArrangementScoreName(a.name);
        const int idx = starscoreFindExcerpt(excerpts, a.scoreName);

        if (parts.empty()) {
            if (idx >= 0) {
                excerpts.erase(excerpts.begin() + idx);
                listChanged = true;
            }
            if (!a.scoreName.isEmpty()) {
                a.scoreName.clear();
                dataChanged = true;
            }
            continue;
        }

        if (idx >= 0) {
            const IExcerptNotationPtr& existing = excerpts[idx];
            std::vector<engraving::Part*> have = masterPartsOf(starscoreExcerptOf(existing));
            std::set<const engraving::Part*> haveSet(have.begin(), have.end());
            std::set<const engraving::Part*> wantSet(parts.begin(), parts.end());
            if (haveSet == wantSet) {
                if (existing->name() != name && starscoreFindExcerpt(excerpts, name) < 0) {
                    existing->setName(name);
                    a.scoreName = name;
                    dataChanged = true;
                }
                continue;
            }
            // The instruments changed: rebuild the score (its own layout breaks are lost)
            excerpts.erase(excerpts.begin() + idx);
            listChanged = true;
        }

        QString unique = name;
        for (int i = 2; starscoreFindExcerpt(excerpts, unique) >= 0; ++i) {
            unique = QString("%1 (%2)").arg(name).arg(i);
        }
        IExcerptNotationPtr score = master->createEmptyExcerpt(unique);
        score->setMasterParts(parts);
        excerpts.push_back(score);
        created.emplace_back(score, hidden);
        a.scoreName = unique;
        listChanged = true;
        dataChanged = true;
    }

    if (listChanged) {
        master->setExcerpts(excerpts);
    }
    if (dataChanged) {
        store(data);
    }
    syncMinMajDefaults();

    if (!created.empty()) {
        const StyleSettings settings = loadStyleSettings();
        for (auto& [score, hidden] : created) {
            INotationPtr n = score->notation();
            if (!n) {
                continue;
            }
            if (!hidden.isEmpty()) {
                std::vector<std::pair<muse::ID, bool> > vis;
                for (const QString& pid : hidden) {
                    vis.emplace_back(muse::ID(pid), false);
                }
                n->parts()->setPartsVisible(vis, TranslatableString::untranslatable("Hide instruments"));
            }
            if (!settings.defaultStyle.isEmpty() && QFileInfo::exists(settings.defaultStyle)) {
                n->style()->loadStyle(settings.defaultStyle, true);
            }
            n->undoStack()->prepareChanges(TranslatableString::untranslatable("StarScore house style"));
            starscore::applyHouseStyle(n->elements()->msScore(), false, scoreVersion());
            n->undoStack()->commitChanges();
        }
    }
}

void StarScoreService::openArrangementScore(const QString& arrangementId)
{
    syncArrangementScores();

    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master) {
        return;
    }
    for (const StarScoreArrangement& a : load().arrangements) {
        if (a.id != arrangementId) {
            continue;
        }
        const int idx = starscoreFindExcerpt(master->excerpts(), a.scoreName);
        if (idx < 0) {
            return;
        }
        INotationPtr n = master->excerpts().at(idx)->notation();
        master->setExcerptIsOpen(n, true);
        globalContext()->setCurrentNotation(n);
    }
}

// ---------------------------------------------------------------------------
//  Minor-major seventh symbol, per score (meta tag "starscoreMinMaj": on / off set by the user,
//  auto-on / auto-off set from the sections; the chord layout reads it)
// ---------------------------------------------------------------------------

static const muse::String STARSCORE_MINMAJ_TAG(u"starscoreMinMaj");

void StarScoreService::syncMinMajDefaults()
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return;
    }
    const Data data = load();

    auto isOrchestralPart = [&](const engraving::Part* p) {
        const QString pid = idText(p);
        for (const StarScoreSection& s : data.sections) {
            if (s.partIds.contains(pid) && (s.templateKey.startsWith("orch-") || s.templateKey.startsWith("marching-")
                                                  || s.templateKey.startsWith("bigband-"))) {
                return true;
            }
        }
        return false;
    };
    auto setAuto = [](engraving::Score* score, bool on) {
        const muse::String now = score->metaTag(STARSCORE_MINMAJ_TAG);
        if (now == u"on" || now == u"off") {
            return;   // chosen by hand
        }
        const muse::String want = on ? u"auto-on" : u"auto-off";
        if (now != want) {
            score->setMetaTag(STARSCORE_MINMAJ_TAG, want);
        }
    };

    setAuto(ms, true);
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        engraving::Excerpt* ex = starscoreExcerptOf(e);
        if (!ex || !ex->excerptScore()) {
            continue;
        }
        const std::vector<engraving::Part*> parts = masterPartsOf(ex);
        bool allOrchestral = !parts.empty();
        for (const engraving::Part* p : parts) {
            allOrchestral &= isOrchestralPart(p);
        }
        setAuto(ex->excerptScore(), !allOrchestral);
    }
}

bool StarScoreService::minMajSymbolInCurrentScore() const
{
    INotationPtr n = globalContext()->currentNotation();
    engraving::Score* score = n ? n->elements()->msScore() : nullptr;
    if (!score) {
        return true;
    }
    const muse::String v = score->metaTag(STARSCORE_MINMAJ_TAG);
    return !(v == u"off" || v == u"auto-off");
}

void StarScoreService::setMinMajSymbolInCurrentScore(bool on)
{
    INotationPtr n = globalContext()->currentNotation();
    engraving::Score* score = n ? n->elements()->msScore() : nullptr;
    if (!score) {
        return;
    }
    score->setMetaTag(STARSCORE_MINMAJ_TAG, on ? u"on" : u"off");
    score->setLayoutAll();
    score->doLayout();
    n->notationChanged().notify();
    if (INotationProjectPtr project = globalContext()->currentProject()) {
        project->markAsUnsaved();
    }
    m_changed.notify();
}

// ---------------------------------------------------------------------------
//  Showing / hiding the StarScore panel
// ---------------------------------------------------------------------------

bool StarScoreService::isPanelVisible() const
{
    return QSettings().value("StarScore/panelVisible", true).toBool();
}

void StarScoreService::setPanelVisible(bool visible)
{
    if (isPanelVisible() == visible) {
        return;
    }
    QSettings().setValue("StarScore/panelVisible", visible);
    m_panelVisibleChanged.notify();
    m_changed.notify();
}

muse::async::Notification StarScoreService::panelVisibleChanged() const
{
    return m_panelVisibleChanged;
}
