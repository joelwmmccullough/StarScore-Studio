/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include <QSettings>
#include "starscoreservice.h"
#include "settings.h"
#include "starscorehouse.h"
#include "starscorepdf.h"
#include "starscoreengraving.h"
#include "organizer/orgcore.h"
#include "organizer/orgstores.h"

#include <algorithm>
#include <map>
#include <set>

#include <QCryptographicHash>
#include <QDir>
#include <QTimer>
#include <QDateTime>
#include <QImage>
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
#include "engraving/editing/editpart.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/chordrest.h"
#include "engraving/dom/select.h"
#include "engraving/dom/text.h"
#include "engraving/dom/measurebase.h"
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

    // the library's files under the standard name, once StarScore has started (and opened whatever it reopens)
    QTimer::singleShot(4000, &m_timerGuard, [this]() { renameLibraryFilesToCodes(); });

    // One handler per channel: muse async keeps the first callback set for a receiver and silently drops a
    // second onNotify(this, ...) on the same channel, so the reference-panel part of this used to never run
    globalContext()->currentProjectChanged().onNotify(this, [this]() {
        onCurrentProjectChanged();
        listenCurrentProject();
        scheduleChanged();

        // Reference PDF panel: the notation page and its panels may still be loading: restore once they are there
        m_projectOpenedMs = QDateTime::currentMSecsSinceEpoch();
        m_referencePanelTouched = false;
        m_auditRevealed.clear();
        QTimer::singleShot(0, &m_timerGuard, [this]() { pickReferenceForCurrentScore(); });
        QTimer::singleShot(1000, &m_timerGuard, [this]() { pickReferenceForCurrentScore(); });
        QTimer::singleShot(2500, &m_timerGuard, [this]() { pickReferenceForCurrentScore(); });
        QTimer::singleShot(5000, &m_timerGuard, [this]() { pickReferenceForCurrentScore(); });
        // the part score the file reopens on: its composer credit placed once everything has loaded
        QTimer::singleShot(1500, &m_timerGuard, [this]() { clearComposerInCurrentScore(); });
    });

    // Reference PDF panel: each part score shows the reference PDF last chosen for it (or stays closed)
    globalContext()->currentNotationChanged().onNotify(this, [this]() {
        pickReferenceForCurrentScore();
        // a part score being shown is laid out: if its composer credit runs into the arrangement label, move it now
        QTimer::singleShot(0, &m_timerGuard, [this]() { clearComposerInCurrentScore(); });
    });
    dockWindowProvider()->windowChanged().onNotify(this, [this]() {
        listenReferencePanel();
        pickReferenceForCurrentScore();
    });
    listenReferencePanel();

    // Audit listen-through: stop at the end of the rehearsal section
    playbackController()->currentPlaybackPositionChanged().onReceive(this, [this](muse::audio::secs_t, muse::midi::tick_t tick) {
        onPlaybackPosition(int(tick));
    });
}

void StarScoreService::listenCurrentProject()
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master) {
        return;
    }

    master->parts()->partsChanged().onNotify(this, [this]() {
        scheduleChanged();
    });

    master->notation()->undoStack()->stackChanged().onNotify(this, [this]() {
        scheduleChanged();
        scheduleIntegrityCheck();
    });
}

void StarScoreService::scheduleIntegrityCheck()
{
    const int generation = ++m_integrityGeneration;
    QTimer::singleShot(1500, &m_timerGuard, [this, generation]() {
        if (generation == m_integrityGeneration) {
            checkIntegrity();
        }
    });
}

void StarScoreService::checkIntegrity()
{
    engraving::MasterScore* ms = masterScore();
    if (!ms || autotestRequested()) {
        return;
    }
    const Ret ret = ms->sanityCheck();
    if (ret) {
        m_integrityBroken = false;
        return;
    }
    if (m_integrityBroken) {
        return;   // (said once; again only after it has been fixed and breaks anew)
    }
    m_integrityBroken = true;
    QString text = QString::fromStdString(ret.text());
    static const QRegularExpression tags("<[^>]*>");
    text.remove(tags);
    QStringList lines = text.split('\n', Qt::SkipEmptyParts);
    if (lines.size() > 4) {
        const int more = int(lines.size()) - 3;
        lines = lines.mid(0, 3);
        lines << muse::qtrc("starscore", "…and %1 more").arg(more);
    }
    interactive()->warning(muse::trc("starscore", "The last edit damaged the score"),
                           muse::qtrc("starscore", "A bar now has the wrong number of beats:\n\n%1\n\nUndo (⌘Z) until this "
                                                   "message would no longer apply, then make the edit again another way. "
                                                   "Saved like this, the file only opens as a damaged score.")
                           .arg(lines.join("\n")).toStdString());
}

int StarScoreService::overlappingRests(engraving::MasterScore* ms, bool remove, QStringList* where) const
{
    if (!ms) {
        return 0;
    }
    int found = 0;
    // the main score first: removing a rest there removes its copies in the part scores too
    for (engraving::Score* sc : ms->scoreList()) {
        std::vector<engraving::Rest*> rests;
        for (engraving::Measure* m = sc->firstMeasure(); m; m = m->nextMeasure()) {
            for (size_t staffIdx = 0; staffIdx < sc->nstaves(); ++staffIdx) {
                for (engraving::voice_idx_t v = 0; v < engraving::VOICES; ++v) {
                    const engraving::track_idx_t track = staffIdx * engraving::VOICES + v;
                    engraving::Fraction end = m->tick();
                    for (engraving::Segment* seg = m->first(engraving::SegmentType::ChordRest); seg;
                         seg = seg->next(engraving::SegmentType::ChordRest)) {
                        engraving::EngravingItem* e = seg->element(track);
                        if (!e || !e->isChordRest()) {
                            continue;
                        }
                        engraving::ChordRest* cr = engraving::toChordRest(e);
                        if (cr->isRest() && seg->tick() < end) {
                            rests.push_back(engraving::toRest(cr));
                            if (where) {
                                const engraving::Staff* st = sc->staff(staffIdx);
                                const QString name = st && st->part() ? st->part()->partName().toQString() : QString();
                                const QString place = muse::qtrc("starscore", "%1, bar %2").arg(name).arg(m->no() + 1);
                                if (!where->contains(place)) {
                                    *where << place;
                                }
                            }
                            continue;
                        }
                        end = std::max(end, seg->tick() + cr->actualTicks());
                    }
                }
            }
        }
        found += int(rests.size());
        if (remove) {
            for (engraving::Rest* r : rests) {
                sc->undoRemoveElement(r);
            }
        }
    }
    return found;
}

void StarScoreService::scheduleChanged()
{
    if (m_changedScheduled) {
        return;
    }
    m_changedScheduled = true;
    QTimer::singleShot(0, &m_timerGuard, [this]() {
        m_changedScheduled = false;
        m_changed.notify();
    });
}

//! The name of an arrangement's own score (a part book of all its instruments)
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

//! The engraving Excerpt behind an (initialised) part book, or nullptr
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
        return StarScoreStatus::Finished;   // an earlier test build's status; its sheet choices are read in fromJson
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
        s.autoStatus = o.value("status").toString() == "auto";
        for (const QJsonValue& v : o.value("skipSheets").toArray()) {
            s.skipSheets << v.toString();
        }
        if (o.value("status").toString() == "finished-lead-sheet-parts" && s.skipSheets.isEmpty()) {
            s.skipSheets = { "drums", "percussion", "keys" };
        }
        for (const QJsonValue& p : o.value("parts").toArray()) {
            s.partIds << p.toString();
        }
        for (const QJsonValue& p : o.value("shown").toArray()) {
            s.shownPartIds << p.toString();
        }
        const QJsonObject alts = o.value("alternates").toObject();
        for (auto it = alts.begin(); it != alts.end(); ++it) {
            s.alternates[it.key()] = it.value().toString();
        }
        const QJsonObject own = o.value("sheetParts").toObject();
        for (auto it = own.begin(); it != own.end(); ++it) {
            s.sheetParts[it.key()] = it.value().toString();
        }
        // "N-Horn Any" is now called "N-Horn Flexible" (1.8.0)
        static const QRegularExpression anyRe("\\b(\\d+)-Horn Any\\b");
        s.name.replace(anyRe, "\\1-Horn Flexible");
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

    for (const QJsonValue& v : root.value("references").toArray()) {
        const QJsonObject o = v.toObject();
        StarScoreReference ref;
        ref.id = o.value("id").toString();
        ref.name = o.value("name").toString();
        ref.file = o.value("file").toString();
        ref.invert = o.value("invert").toBool(true);
        ref.instrument = o.value("instrument").toString();
        if (!ref.id.isEmpty()) {
            data.references.push_back(ref);
        }
    }

    // ("referenceForScore", written by earlier builds, is skipped: nothing ever read it; the reference shown with
    // each part score is kept in the app settings, see referenceViewSettingsKeys)

    data.version = root.value("scoreVersion").toString();
    data.fileId = root.value("fileId").toString();
    const QJsonObject deco = root.value("decoRestore").toObject();
    for (auto it = deco.begin(); it != deco.end(); ++it) {
        QStringList fonts;
        for (const QJsonValue& v : it.value().toArray()) {
            fonts << v.toString();
        }
        data.decoRestore[it.key()] = fonts;
    }
    data.exportSignatures = root.value("exportSignatures").toObject();
    data.scoreSystems = root.value("scoreSystems").toObject();
    data.recordings = root.value("recordings").toObject();
    const QJsonObject partStatus = root.value("partStatus").toObject();
    for (auto it = partStatus.begin(); it != partStatus.end(); ++it) {
        data.partStatus[it.key()] = it.value().toString();
    }
    const QJsonObject scoreStatus = root.value("scoreStatus").toObject();
    for (auto it = scoreStatus.begin(); it != scoreStatus.end(); ++it) {
        data.scoreStatus[it.key()] = it.value().toString();
    }
    data.alternatesInSection = root.value("alternatesInSection").toBool();
    data.alternateBarlinesMatched = root.value("alternateBarlinesMatched").toBool();
    data.flexibleClefsSet = root.value("flexibleClefsSet").toBool();

    const QJsonObject audit = root.value("audit").toObject();
    data.auditReferenceSectionId = audit.value("reference").toString();
    for (const QJsonValue& v : audit.value("intentional").toArray()) {
        data.auditIntentional << v.toString();
    }
    for (const QJsonValue& v : audit.value("listened").toArray()) {
        data.auditListened << v.toString();
    }
    const QJsonObject audited = audit.value("audited").toObject();
    for (auto it = audited.begin(); it != audited.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        data.auditAudited[it.key()] = { o.value("date").toString(), o.value("fp").toString() };
    }
    const QJsonObject partsAudited = audit.value("parts").toObject();
    for (auto it = partsAudited.begin(); it != partsAudited.end(); ++it) {
        const QJsonObject o = it.value().toObject();
        data.auditPartAudited[it.key()] = { o.value("date").toString(), o.value("fp").toString() };
    }

    for (const QJsonValue& v : root.value("arrangements").toArray()) {
        const QJsonObject o = v.toObject();
        StarScoreArrangement a;
        a.id = o.value("id").toString();
        a.name = o.value("name").toString();
        {
            static const QRegularExpression anyRe("\\b(\\d+)-Horn Any\\b");
            a.name.replace(anyRe, "\\1-Horn Flexible");
        }
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
        o["status"] = s.autoStatus ? QString("auto") : statusKey(s.status);
        o["parts"] = QJsonArray::fromStringList(s.partIds);
        o["shown"] = QJsonArray::fromStringList(s.shownPartIds);
        if (!s.skipSheets.isEmpty()) {
            o["skipSheets"] = QJsonArray::fromStringList(s.skipSheets);
        }
        if (!s.alternates.empty()) {
            QJsonObject alts;
            for (const auto& [alt, main] : s.alternates) {
                alts[alt] = main;
            }
            o["alternates"] = alts;
        }
        if (!s.sheetParts.empty()) {
            QJsonObject own;
            for (const auto& [sheet, pid] : s.sheetParts) {
                own[sheet] = pid;
            }
            o["sheetParts"] = own;
        }
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
    QJsonArray refs;
    for (const StarScoreReference& ref : data.references) {
        QJsonObject o;
        o["id"] = ref.id;
        o["name"] = ref.name;
        o["file"] = ref.file;
        o["invert"] = ref.invert;
        if (!ref.instrument.isEmpty()) {
            o["instrument"] = ref.instrument;
        }
        refs.append(o);
    }
    if (!refs.isEmpty()) {
        root["references"] = refs;
    }
    if (!data.fileId.isEmpty()) {
        root["fileId"] = data.fileId;
    }
    if (!data.decoRestore.empty()) {
        QJsonObject deco;
        for (const auto& [key, fonts] : data.decoRestore) {
            deco[key] = QJsonArray::fromStringList(fonts);
        }
        root["decoRestore"] = deco;
    }
    if (!data.exportSignatures.isEmpty()) {
        root["exportSignatures"] = data.exportSignatures;
    }
    if (!data.scoreSystems.isEmpty()) {
        root["scoreSystems"] = data.scoreSystems;
    }
    if (!data.recordings.isEmpty()) {
        root["recordings"] = data.recordings;
    }
    QJsonObject partStatus;
    for (const auto& [pid, key] : data.partStatus) {
        partStatus[pid] = key;
    }
    if (!partStatus.isEmpty()) {
        root["partStatus"] = partStatus;
    }
    QJsonObject scoreStatus;
    for (const auto& [aid, key] : data.scoreStatus) {
        scoreStatus[aid] = key;
    }
    if (!scoreStatus.isEmpty()) {
        root["scoreStatus"] = scoreStatus;
    }
    if (data.alternatesInSection) {
        root["alternatesInSection"] = true;
    }
    if (data.alternateBarlinesMatched) {
        root["alternateBarlinesMatched"] = true;
    }
    if (data.flexibleClefsSet) {
        root["flexibleClefsSet"] = true;
    }
    QJsonObject audit;
    if (!data.auditReferenceSectionId.isEmpty()) {
        audit["reference"] = data.auditReferenceSectionId;
    }
    if (!data.auditIntentional.isEmpty()) {
        audit["intentional"] = QJsonArray::fromStringList(data.auditIntentional);
    }
    if (!data.auditListened.isEmpty()) {
        audit["listened"] = QJsonArray::fromStringList(data.auditListened);
    }
    QJsonObject audited;
    for (const auto& [arrId, entry] : data.auditAudited) {
        audited[arrId] = QJsonObject { { "date", entry.first }, { "fp", entry.second } };
    }
    if (!audited.isEmpty()) {
        audit["audited"] = audited;
    }
    QJsonObject partsAudited;
    for (const auto& [pid, entry] : data.auditPartAudited) {
        partsAudited[pid] = QJsonObject { { "date", entry.first }, { "fp", entry.second } };
    }
    if (!partsAudited.isEmpty()) {
        audit["parts"] = partsAudited;
    }
    if (!audit.isEmpty()) {
        root["audit"] = audit;
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

QString StarScoreService::rhythmRole(const QString& id)
{
    // the order matters for the section status: a bass drum is drums, not bass
    if (id == "drumset" || id == "drum-kit" || id.startsWith("drum")) {
        return "drums";
    }
    if (id == "congas" || id == "bongos" || id == "percussion" || id == "timbales" || id == "cajon"
        || id.contains("shaker") || id.contains("tambourine") || id.contains("cowbell") || id.contains("conga")) {
        return "percussion";
    }
    if (id.contains("bass")) {
        return "bass";
    }
    if (id.contains("guitar")) {
        return "guitar";
    }
    return "keys";
}

//! Rhythm players who can read the lead sheet instead of their own sheet: "drums", "percussion", "keys"
//! (empty for guitar and bass)
static QString starscoreLeadSheetKind(const QString& id)
{
    const QString role = StarScoreService::rhythmRole(id);
    return role == "bass" || role == "guitar" ? QString() : role;
}

//! What loadFrom's result depends on besides the meta tag: which parts exist and what they play
static QString starscorePartsFingerprint(const mu::engraving::MasterScore* ms)
{
    QString out;
    for (const mu::engraving::Part* p : ms->parts()) {
        out += StarScoreService::idTextOf(p);
        out += '|';
        out += p->instrumentId().toQString();
        out += ';';
    }
    return out;
}

StarScoreService::Data StarScoreService::loadFrom(const engraving::MasterScore* ms) const
{
    if (!ms) {
        return {};
    }

    const String tag = ms->metaTag(STARSCORE_META_TAG);
    const QString fingerprint = starscorePartsFingerprint(ms);
    if (auto it = m_loadCache.find(ms); it != m_loadCache.end() && it->second.tag == tag && it->second.parts == fingerprint) {
        return it->second.data;
    }

    Data data = fromJson(tag.toQString());

    // Drop parts that no longer exist (deleted in the Instruments panel)
    std::set<QString> existing;
    for (const engraving::Part* p : ms->parts()) {
        existing.insert(idText(p));
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
        for (auto it = s.alternates.begin(); it != s.alternates.end();) {
            if (existing.count(it->first) && existing.count(it->second)) {
                ++it;
            } else {
                it = s.alternates.erase(it);
            }
        }
        for (auto it = s.sheetParts.begin(); it != s.sheetParts.end();) {
            it = existing.count(it->second) ? std::next(it) : s.sheetParts.erase(it);
        }
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

    // "Auto" sections: the least-finished of the parts the section shows (a part with no tag counts as Empty).
    // Rhythm sections: drums, percussion and keys parts with no tag read the lead sheet, so they don't count.
    for (StarScoreSection& s : data.sections) {
        if (!s.autoStatus) {
            continue;
        }
        const bool rhythm = s.templateKey == "rhythm" || s.templateKey == "bigband-rhythm";
        // A 1- to 7-Horn section counts every chair, hidden or not: each one is exported (1.18.14: Amplitudes' 2-Horn
        // Section read Finished from its Tenor Sax alone, its Trumpet left hidden by the audit)
        static const QRegularExpression hornSection("^[1-7]-horn$");
        QStringList counted = (s.shownPartIds.isEmpty() || hornSection.match(s.templateKey).hasMatch()) ? s.partIds
                              : s.shownPartIds;
        for (const auto& [alt, main] : s.alternates) {   // stand-in versions count even when left out of the shown list
            if (!counted.contains(alt)) {
                counted << alt;
            }
        }
        StarScoreStatus result = StarScoreStatus::Finished;
        int countedParts = 0;
        for (const QString& pid : counted) {
            auto it = data.partStatus.find(pid);
            // (tagged Empty counts the same as no tag: The Courier's Keys part was tagged Empty, which made its
            // Rhythm Section, and so every arrangement, grey)
            const bool untaggedOrEmpty = it == data.partStatus.end() || statusFromKey(it->second) == StarScoreStatus::Empty;
            if (untaggedOrEmpty && rhythm) {
                const engraving::Part* p = ms->partById(ID(pid));
                const QString kind = p ? starscoreLeadSheetKind(p->instrumentId().toQString()) : QString();
                if (!kind.isEmpty()) {
                    if (!s.autoSkipSheets.contains(kind)) {
                        s.autoSkipSheets << kind;
                    }
                    continue;
                }
            }
            ++countedParts;
            result = std::min(result, it == data.partStatus.end() ? StarScoreStatus::Empty : statusFromKey(it->second));
        }
        s.status = countedParts == 0 ? StarScoreStatus::Empty : result;
    }

    // Players who read the lead sheet are only as done as the lead sheet: the section can't be further along than it.
    // With no lead sheet section, nobody can read it, so those parts count as Empty after all.
    bool hasLeadSheet = false;
    StarScoreStatus leadSheetStatus = StarScoreStatus::Finished;
    for (const StarScoreSection& s : data.sections) {
        if (s.templateKey == "lead-sheet") {
            hasLeadSheet = true;
            leadSheetStatus = std::min(leadSheetStatus, s.status);
        }
    }
    for (StarScoreSection& s : data.sections) {
        if (!s.autoStatus || s.autoSkipSheets.isEmpty()) {
            continue;
        }
        s.status = hasLeadSheet ? std::min(s.status, leadSheetStatus) : StarScoreStatus::Empty;
        s.leadSheetFinish = s.status == StarScoreStatus::Finished;
        if (!hasLeadSheet) {
            s.autoSkipSheets.clear();
        }
    }

    // The main score and a few solo scores at most; a score that has gone is harmless here, since an entry is
    // only used when its tag and parts match again (then the result would be the same anyway)
    if (m_loadCache.size() > 8) {
        m_loadCache.clear();
    }
    m_loadCache[ms] = { tag, fingerprint, data };
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

    scheduleChanged();
}

INotationProjectPtr StarScoreService::projectOf(const engraving::MasterScore* ms) const
{
    if (m_mainProject && m_mainProject->masterNotation()->masterScore() == ms) {
        return m_mainProject;
    }
    for (const auto& [id, project] : m_soloProjects) {
        if (project && project->masterNotation()->masterScore() == ms) {
            return project;
        }
    }
    INotationProjectPtr current = globalContext()->currentProject();
    if (current && current->masterNotation()->masterScore() == ms) {
        return current;
    }
    return m_mainProject ? m_mainProject : current;
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
    return part->id().toQString();
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
    return sectionsWithOn(load());
}

std::vector<StarScoreSection> StarScoreService::sectionsWithOn(const Data& data) const
{
    const engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return {};
    }
    std::vector<StarScoreSection> sections = data.sections;

    // A section is "on" when any of its own instruments is visible. An instrument shared with another section
    // (e.g. a soprano sax in both the 6- and 7-Horn sections) only counts when all of the section's instruments
    // are shared; otherwise showing one section would make the other look "on" too, and turning it off would
    // then remember just the shared instrument.
    std::map<QString, int> useCount;
    for (const StarScoreSection& s : sections) {
        for (const QString& id : s.partIds) {
            ++useCount[id];
        }
    }
    for (StarScoreSection& s : sections) {
        const bool hasOwn = std::any_of(s.partIds.begin(), s.partIds.end(), [&](const QString& id) { return useCount[id] == 1; });
        bool anyVisible = false;
        for (const QString& id : s.partIds) {
            if (hasOwn && useCount[id] > 1) {
                continue;
            }
            const engraving::Part* p = ms->partById(ID(id));
            if (p && p->show() && !m_auditRevealed.count(id)) {
                anyVisible = true;
                break;
            }
        }
        s.on = anyVisible;
    }

    return sections;
}

std::vector<StarScoreArrangement> StarScoreService::arrangements() const
{
    return load().arrangements;
}

QStringList StarScoreService::onSectionIds(const Data& data) const
{
    QStringList ids;
    for (const StarScoreSection& s : sectionsWithOn(data)) {
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
        // Big Band, Orchestra, Marching Band: the full score must be marked Finished too
        if (hasOwnScoreStatus(a.templateKey)) {
            result = std::min(result, ownScoreStatus(data, a));
        }
    }

    return any ? result : StarScoreStatus::Empty;
}

bool StarScoreService::hasOwnScoreStatus(const QString& arrangementTemplateKey)
{
    return arrangementTemplateKey == "big-band" || arrangementTemplateKey == "orchestra" || arrangementTemplateKey == "marching-band";
}

StarScoreStatus StarScoreService::ownScoreStatus(const Data& data, const StarScoreArrangement& arrangement)
{
    auto it = data.scoreStatus.find(arrangement.id);
    return it == data.scoreStatus.end() ? StarScoreStatus::Empty : statusFromKey(it->second);
}

//! The Big Band / Orchestra / Marching Band arrangement whose full score this part score is ("" for any other)
static QString starscoreFamilyArrangementOfScore(const mu::engraving::Score* score, const mu::engraving::MasterScore* ms,
                                                 const std::vector<StarScoreArrangement>& arrangements)
{
    if (!score || !ms || score == ms) {
        return QString();
    }
    QString name;
    for (const mu::engraving::Excerpt* ex : ms->excerpts()) {
        if (ex && ex->excerptScore() == score) {
            name = ex->name().toQString();
            break;
        }
    }
    if (name.isEmpty()) {
        return QString();
    }
    for (const StarScoreArrangement& a : arrangements) {
        if (!a.scoreName.isEmpty() && a.scoreName == name && StarScoreService::hasOwnScoreStatus(a.templateKey)) {
            return a.id;
        }
    }
    return QString();
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
        info.partId = idText(p);
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
    const std::vector<StarScoreSection> current = sectionsWithOn(data);   // with the derived "on" flag

    // an instrument shown only for an audit issue counts as hidden here
    auto isVisible = [this, ms](const QString& partId) {
        const engraving::Part* p = ms->partById(ID(partId));
        return p && p->show() && !m_auditRevealed.count(partId);
    };

    std::set<QString> managed;
    std::set<QString> shown;
    bool rememberedChanged = false;
    QStringList turnedOn;   // their part scores open afterwards

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
            turnedOn << s.id;
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
            // A 1- to 7-Horn section always comes back with every chair; only its stand-in versions are remembered
            // (1.18.14: an instrument the audit showed for an issue, still showing after the file was reopened,
            // was remembered as the 2-Horn Section's only instrument, and Amplitudes' 2-Horn lost its Trumpet)
            static const QRegularExpression hornSection("^[1-7]-horn$");
            if (hornSection.match(s.templateKey).hasMatch()) {
                for (const QString& id : s.partIds) {
                    if (!s.alternates.count(id)) {
                        shown.insert(id);
                    }
                }
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
    m_auditRevealed.clear();

    std::vector<std::pair<muse::ID, bool> > changes;
    std::vector<muse::ID> unhideStaves;   // shown parts whose every staff was hidden staff by staff
    for (const engraving::Part* p : ms->parts()) {
        const QString id = idText(p);
        if (!managed.count(id)) {
            continue;
        }
        const bool visible = shown.count(id) > 0;
        if (p->show() != visible) {
            changes.emplace_back(p->id(), visible);
        }
        if (visible && !p->staves().empty()
            && std::none_of(p->staves().begin(), p->staves().end(), [](const engraving::Staff* st) { return st->visible(); })) {
            for (const engraving::Staff* st : p->staves()) {
                unhideStaves.push_back(st->id());
            }
        }
    }

    // (one undo step per staff: NotationParts::setStaffVisible opens and commits its own command, and a nested
    // commit would end an enclosing one early, so these can't be wrapped in one)
    for (const muse::ID& sid : unhideStaves) {
        master->parts()->setStaffVisible(sid, true);
    }
    if (!changes.empty()) {
        master->parts()->setPartsVisible(changes, TranslatableString::untranslatable(String::fromQString(actionName)));
    }
    if (!changes.empty() || !unhideStaves.empty()) {
        scheduleChanged();
    }
    syncSectionTabs(turnedOn);
}

// ---------------------------------------------------------------------------
//  Part score tabs (Joel, 6 Oct 2026: the Parts window is gone, so showing a section opens its part scores)
// ---------------------------------------------------------------------------

StarScoreExportProgress StarScoreService::exportProgress() const
{
    return m_exportProgress;
}

void StarScoreService::reportExportProgress(const QString& phase, int done, int total, const QString& step)
{
    m_exportProgress.running = true;
    m_exportProgress.phase = phase;
    m_exportProgress.done = done;
    m_exportProgress.total = total;
    m_exportProgress.step = step;
    // where the export is, in the log, so a crash can be traced to the sheet being made
    LOGI() << "[starscore] " << phase << " " << done + 1 << "/" << total << ": " << step;
    // the progress window repaints; clicks and keys wait (the export dialog can't be closed meanwhile)
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void StarScoreService::endExportProgress()
{
    m_exportProgress = StarScoreExportProgress();
}

static const Settings::Key SHOW_SECTION_SCORES("project", "starscore/showSectionScores");
static const Settings::Key SHOW_PERCUSSION_SCORE("project", "starscore/showPercussionScore");

bool StarScoreService::percussionScoreShown() const
{
    settings()->setDefaultValue(SHOW_PERCUSSION_SCORE, Val(false));
    return settings()->value(SHOW_PERCUSSION_SCORE).toBool();
}

void StarScoreService::setPercussionScoreShown(bool shown)
{
    settings()->setSharedValue(SHOW_PERCUSSION_SCORE, Val(shown));
    syncSectionTabs({});
    scheduleChanged();
}

bool StarScoreService::sectionScoresShown() const
{
    settings()->setDefaultValue(SHOW_SECTION_SCORES, Val(false));
    return settings()->value(SHOW_SECTION_SCORES).toBool();
}

void StarScoreService::setSectionScoresShown(bool shown)
{
    settings()->setSharedValue(SHOW_SECTION_SCORES, Val(shown));
    syncSectionTabs({});
    scheduleChanged();
}

void StarScoreService::syncSectionTabs(const QStringList& turnedOnSectionIds)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return;
    }
    // a score closed here while it's the one showing: the main score shows first (as closing its tab does)
    auto close = [&](const INotationPtr& n) {
        if (globalContext()->currentNotation() == n) {
            globalContext()->setCurrentNotation(master->notation());
        }
        master->setExcerptIsOpen(n, false);
    };
    const Data data = load();
    // the part scores of the sections just turned on: their shown instruments, one part score each
    std::set<QString> openParts;
    for (const StarScoreSection& s : data.sections) {
        if (!turnedOnSectionIds.contains(s.id)) {
            continue;
        }
        for (const QString& pid : s.partIds) {
            const engraving::Part* p = ms->partById(ID(pid));
            if (p && p->show()) {
                openParts.insert(pid);
            }
        }
    }
    // the arrangements' own scores ("4-Horn Arrangement"): open with every section of theirs showing, when the
    // "Section scores visible" switch is on; closed when it's off (the exported Scores are made on their own)
    const bool scoresShown = sectionScoresShown();
    std::set<QString> onIds;
    for (const QString& id : onSectionIds(data)) {
        onIds.insert(id);
    }
    std::map<QString, bool> arrangementScores;   // score name -> open
    for (const StarScoreArrangement& a : data.arrangements) {
        if (a.scoreName.isEmpty()) {
            continue;
        }
        const bool allOn = !a.sectionIds.isEmpty() && std::all_of(a.sectionIds.begin(), a.sectionIds.end(),
                                                                   [&](const QString& id) { return onIds.count(id) > 0; });
        arrangementScores[a.scoreName] = arrangementScores[a.scoreName] || (scoresShown && allOn);
    }
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        INotationPtr n = e ? e->notation() : nullptr;
        engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
        if (!es) {
            continue;
        }
        const auto arr = arrangementScores.find(e->name());
        if (arr != arrangementScores.end()) {
            if (n->isOpen() && !arr->second) {
                close(n);
            } else if (!n->isOpen() && arr->second) {
                master->setExcerptIsOpen(n, true);
            }
            continue;
        }
        if (es->parts().size() != 1) {
            // any other score of several parts (the full horn scores kept from the converted files, "2-Horn
            // Arrangement"): closed with the switch off
            if (!scoresShown && n->isOpen()) {
                close(n);
            }
            continue;
        }
        const std::vector<engraving::Part*> parts = masterPartsOf(es, ms);
        if (parts.size() != 1) {
            continue;
        }
        // the percussion part's score (Congas…): open only with the "Percussion score visible" switch on (Joel kept
        // closing it)
        if (rhythmRole(parts.front()->instrumentId().toQString()) == "percussion") {
            if (!percussionScoreShown()) {
                if (n->isOpen()) {
                    close(n);
                }
                continue;
            }
            if (!n->isOpen() && parts.front()->show()) {
                master->setExcerptIsOpen(n, true);
            }
            continue;
        }
        if (!n->isOpen() && openParts.count(idText(parts.front()))) {
            master->setExcerptIsOpen(n, true);
        }
    }
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
//  StarScore Deco on / off
// ---------------------------------------------------------------------------

static const QString DECO_FONT = QStringLiteral("StarScore Deco");
static const QString DECO_TEXT_FONT = QStringLiteral("StarScore Deco Text");

bool StarScoreService::decoOn() const
{
    const engraving::MasterScore* ms = masterScore();
    return ms && ms->style().value(engraving::Sid::musicalSymbolFont).value<String>().toQString() == DECO_FONT;
}

void StarScoreService::toggleDeco()
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master || !masterScore()) {
        return;
    }
    Data data = load();
    const bool turnOff = decoOn();

    // the main score and every part book (each has its own style)
    std::vector<std::pair<QString, INotationPtr> > notations { { QString(), master->notation() } };
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        if (e && e->notation()) {
            notations.emplace_back(e->name(), e->notation());
        }
    }

    static const std::vector<StyleId> ids { StyleId::musicalSymbolFont, StyleId::musicalTextFont, StyleId::dynamicsFont };
    for (const auto& [key, n] : notations) {
        auto current = [&](StyleId id) {
            return n->style()->styleValue(id).value<String>().toQString();
        };
        if (turnOff) {
            auto it = data.decoRestore.find(key);
            if (it == data.decoRestore.end() || it->second.size() != int(ids.size())) {
                continue;   // not switched on by the button: leave it
            }
            n->undoStack()->prepareChanges(TranslatableString::untranslatable("StarScore Deco off"));
            for (size_t i = 0; i < ids.size(); ++i) {
                n->style()->setStyleValue(ids[i], PropertyValue(String::fromQString(it->second.at(int(i)))));
            }
            n->undoStack()->commitChanges();
        } else {
            if (current(StyleId::musicalSymbolFont) == DECO_FONT) {
                continue;
            }
            QStringList before;
            for (StyleId id : ids) {
                before << current(id);
            }
            data.decoRestore[key] = before;
            const bool ownDynamicsFont = n->style()->styleValue(StyleId::dynamicsOverrideFont).toBool();
            n->undoStack()->prepareChanges(TranslatableString::untranslatable("StarScore Deco on"));
            n->style()->setStyleValue(StyleId::musicalSymbolFont, PropertyValue(String::fromQString(DECO_FONT)));
            n->style()->setStyleValue(StyleId::musicalTextFont, PropertyValue(String::fromQString(DECO_TEXT_FONT)));
            if (!ownDynamicsFont) {
                n->style()->setStyleValue(StyleId::dynamicsFont, PropertyValue(String::fromQString(DECO_FONT)));
            }
            n->undoStack()->commitChanges();
        }
    }
    if (turnOff) {
        data.decoRestore.clear();
    }
    store(data);
}

// ---------------------------------------------------------------------------
//  Templates
// ---------------------------------------------------------------------------

//! Lead sheet: the bass staff is shown only in systems where it has music (staff setting "Hide when empty: Always",
//! which also applies to the first system), in the main score and every part book
void StarScoreService::autoHideLeadBassStaff(const IMasterNotationPtr& master, engraving::Part* part)
{
    if (!master || !part || part->nstaves() < 2) {
        return;
    }
    engraving::Staff* bass = part->staves().at(1);
    if (!bass->show()) {
        master->parts()->setStaffVisible(bass->id(), true);
    }
    bass->setHideWhenEmpty(engraving::AutoOnOff::ON);
    for (engraving::Staff* linked : bass->staffList()) {
        linked->setHideWhenEmpty(engraving::AutoOnOff::ON);
    }
    master->masterScore()->setLayoutAll();
    for (engraving::Score* score : master->masterScore()->scoreList()) {
        score->setLayoutAll();
    }
}

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
        // a chair's short name, on the score's systems after the first: "H1", not the instrument's "Tpt. 1"
        static const QRegularExpression hornRe("^Horn (\\d+)$");
        const QRegularExpressionMatch m = hornRe.match(QString::fromUtf8(name));
        if (m.hasMatch()) {
            i.shortName = "H" + m.captured(1);
        }
        return i;
    };

    return {
        { "lead-sheet", "Lead Sheet", { [&]() {
              StarScoreInstrument lead = inst("piano", "Lead");
              lead.shortName = "Lead";
              lead.autoHideLowerStaff = true;
              return lead;
          }() } },
        { "rhythm", "Rhythm Section", {
              inst("piano", "Piano"), inst("electric-guitar", "Electric Guitar"), inst("electric-bass", "Electric Bass"),
              inst("drumset", "Drum Kit"), inst("congas", "Congas", true) } },
        // 1-Horn: the melody written out for each horn we make songbooks for, one sheet per instrument
        // (only the Trumpet to start: the E♭ and B♭ Saxophone and Trombone sheets are made from it when it's Finished)
        { "1-horn", "1-Horn Section", { inst("bb-trumpet", "Trumpet") } },
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
        { "2-horn-any", "2-Horn Flexible", { chair("c-trumpet", "Horn 1", 56, 80, 52, 85), chair("trombone", "Horn 2", 44, 71, 44, 74) } },
        // (until 1.18.2 the 3-Horn also had a hidden "Horn 1 (Flute)" staff; Horn 1's Flute sheet is now made from Horn 1)
        { "3-horn-any", "3-Horn Flexible", { chair("c-trumpet", "Horn 1", 56, 80, 52, 85),
              chair("c-trumpet", "Horn 2", 52, 75, 52, 85), chair("trombone", "Horn 3", 44, 71, 44, 74) } },

        // --- Strings (Joel, 6 Oct 2026): extra colour added to many songs, in no arrangement ---
        { "string-duo", "String Duo", { inst("violin", "Duo: Violin"), inst("violoncello", "Duo: Cello") } },
        { "string-trio", "String Trio", { inst("violin", "Trio: Violin"), inst("viola", "Trio: Viola"),
              inst("violoncello", "Trio: Cello") } },
        { "string-quartet", "String Quartet", { inst("violin", "Quartet: Violin"), inst("viola", "Quartet: Viola"),
              inst("violoncello", "Quartet: Cello"), inst("contrabass", "Quartet: Double Bass") } },
        { "string-quintet", "String Quintet", { inst("violin", "Quintet: Violin 1"), inst("violin", "Quintet: Violin 2"),
              inst("viola", "Quintet: Viola"), inst("violoncello", "Quintet: Cello"), inst("contrabass", "Quintet: Double Bass") } },

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
        // in menu order: each Flexible right after its Standard (Joel, 5 Oct 2026)
        { "1-horn-standard", "1-Horn Standard", { "lead-sheet", "1-horn", "rhythm" } },
        { "2-horn-standard", "2-Horn Standard", { "lead-sheet", "2-horn", "rhythm" } },
        { "2-horn-any", "2-Horn Flexible", { "lead-sheet", "2-horn-any", "rhythm" } },
        { "3-horn-standard", "3-Horn Standard", { "lead-sheet", "3-horn", "rhythm" } },
        { "3-horn-any", "3-Horn Flexible", { "lead-sheet", "3-horn-any", "rhythm" } },
        { "4-horn-standard", "4-Horn Standard", { "lead-sheet", "4-horn", "rhythm" } },
        { "5-horn-standard", "5-Horn Standard", { "lead-sheet", "5-horn", "rhythm" } },
        { "6-horn-standard", "6-Horn Standard", { "lead-sheet", "6-horn", "rhythm" } },
        { "7-horn-standard", "7-Horn Standard", { "lead-sheet", "7-horn", "rhythm" } },
        { "big-band", "Big Band", { "bigband-saxes", "bigband-trumpets", "bigband-trombones", "bigband-rhythm" } },
        { "marching-band", "Marching Band", { "marching-woodwinds", "marching-brass", "marching-battery", "marching-front" } },
        { "orchestra", "Orchestra", { "orch-woodwinds", "orch-brass", "orch-percussion", "orch-strings" } },
    };
}

std::optional<StarScoreSectionTemplate> StarScoreService::sectionTemplate(const QString& key) const
{
    // by value: this used to hand out a pointer into a static vector that the next call replaced
    for (const StarScoreSectionTemplate& t : sectionTemplates()) {
        if (t.key == key) {
            return t;
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
//  Sections
// ---------------------------------------------------------------------------

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
            covered.insert(ex->initialPartId().toQString());
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

    std::vector<muse::ID> hiddenStaves;
    // Names, short names and ranges in one edit: each through the Instruments panel's route lays out the score and
    // every part score again (making a few parts in a song with dozens of part scores took about a minute)
    engraving::MasterScore* ms = master->masterScore();
    master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Name instruments"));
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
            engraving::EditPart::setInstrumentName(ms, p, engraving::Fraction(0, 1), String::fromQString(inst.partName));
            p->setPartName(String::fromQString(inst.partName));

            // Short name: MuseScore numbers instruments of the same kind ("Pno. 2" when the lead sheet is also a
            // piano); use the instrument's own short name, numbered only when the part name is ("Tpt. 1")
            QString shortName = inst.shortName;
            if (shortName.isEmpty()) {
                const InstrumentTemplate& tpl = instrumentsRepository()->instrumentTemplate(String::fromQString(inst.instrumentId));
                if (!tpl.shortNames.empty()) {
                    shortName = tpl.shortNames.front().name().toQString();
                    static const QRegularExpression numberRe("\\s(\\d+)$");
                    const QRegularExpressionMatch m = numberRe.match(inst.partName);
                    if (m.hasMatch() && !shortName.isEmpty()) {
                        shortName += " " + m.captured(1);
                    }
                }
            }
            if (!shortName.isEmpty()) {
                engraving::EditPart::setInstrumentAbbreviature(ms, p, engraving::Fraction(0, 1), String::fromQString(shortName));
            }
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
                hiddenStaves.push_back(p->staves().at(staffIdx)->id());
            }
        }

        if (inst.hidden) {
            hide.emplace_back(p->id(), false);
        } else {
            section.shownPartIds << id;
        }
    }

    master->notation()->undoStack()->commitChanges();
    master->parts()->partsChanged().notify();
    // one undo step per staff: setStaffVisible commits a command of its own, which can't be nested in another
    for (const muse::ID& sid : hiddenStaves) {
        master->parts()->setStaffVisible(sid, false);
    }

    addPartBooksFor(section.partIds);

    for (size_t i = 0; i < newParts.size() && i < instruments.size(); ++i) {
        if (instruments[i].autoHideLowerStaff) {
            autoHideLeadBassStaff(master, newParts[i]);
        }
    }

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
    standardizeHornNames();   // "4H: Trumpet", "3H Flexible: Horn 1"

    applyStyles(section.partIds);
    // The mixer's tracks for new instruments appear a moment after they're added: apply the defaults again then
    const QStringList newIds = section.partIds;
    QTimer::singleShot(1500, &m_timerGuard, [this, newIds]() { applyMixerDefaults(newIds); });
    QTimer::singleShot(4000, &m_timerGuard, [this, newIds]() { applyMixerDefaults(newIds); });

    if (section.templateKey.endsWith("-horn-any")) {
        fillAnyHornsFromStandard(section);
        applyFlexibleView(section);
        Data withClefs = load();
        if (!withClefs.flexibleClefsSet) {
            withClefs.flexibleClefsSet = true;   // (so opening the file doesn't set them again)
            store(withClefs);
        }
    }
    // A piccolo's Flute part made with the section, empty, so both can be pasted into straight away (Joel, 6 Oct 2026);
    // marking the Piccolo Finished while the Flute is still empty fills it from the Piccolo as before
    QStringList madeIds = section.partIds;
    static const QRegularExpression hornSection("^[3-7]-horn$");
    if (hornSection.match(section.templateKey).hasMatch()) {
        if (engraving::MasterScore* ms = masterScore()) {
            for (const QString& pid : section.partIds) {
                const engraving::Part* p = ms->partById(ID(pid));
                if (!p || p->instrumentId().toQString() != "piccolo") {
                    continue;
                }
                const RetVal<QStringList> flute = createLowAlternates(section.id, pid, { "flute" });
                if (flute.ret) {
                    madeIds << flute.val;
                }
            }
        }
    }
    // the new part scores open, with their sheet titles (after the Flexible chairs took the Standard parts' style)
    labelPartBooks();
    openPartBooks(madeIds);

    if (!missing.isEmpty()) {
        LOGW() << "[starscore] skipped unknown instruments: " << missing.join(", ");
    }

    return RetVal<QString>::make_ok(section.id);
}

RetVal<QString> StarScoreService::createSectionFromTemplate(const QString& templateKey, const QString& doublerInstrumentId,
                                                            const QString& lowHornInstrumentId)
{
    const std::optional<StarScoreSectionTemplate> t = sectionTemplate(templateKey);
    if (!t) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError);
    }
    return createSection(t->key, t->name, templateInstrumentsFor(*t, doublerInstrumentId, lowHornInstrumentId));
}

std::vector<StarScoreHornChoice> StarScoreService::doublerChoices() const
{
    return {
        { "soprano-saxophone", "Soprano Saxophone", "Soprano Sax" },
        { "alto-saxophone", "Alto Saxophone", "Alto Sax" },
        { "tenor-saxophone", "Tenor Saxophone", "Tenor Sax" },
        { "bb-clarinet", "Clarinet", "Clarinet" },
        { "bb-bass-clarinet", "Bass Clarinet", "Bass Clarinet" },
        { "piccolo", "Piccolo", "Piccolo" },
        { "flute", "Flute", "Flute" },
    };
}

std::vector<StarScoreHornChoice> StarScoreService::lowHornChoices() const
{
    // The bass trombone first (the usual 7th horn), then the stand-in versions in the order they're made
    return {
        { "bass-trombone", "Bass Trombone", "Bass Trombone" },
        { "baritone-saxophone", "Baritone Saxophone", "Bari Sax" },
        { "bass-saxophone", "Bass Saxophone", "Bass Sax" },
        { "bassoon", "Bassoon", "Bassoon" },
        { "bb-bass-clarinet", "Bass Clarinet", "Bass Clarinet" },
        { "contrabass-clarinet", "Contrabass Clarinet", "Contrabass Clarinet" },
        { "contrabassoon", "Contrabassoon", "Contrabassoon" },
        { "tuba", "Tuba", "Tuba" },
    };
}

QString StarScoreService::lowHornName(const QString& instrumentId)
{
    // By the band's name rather than the id, so a bass clarinet in bass clef or an E♭ tuba counts as the same horn
    static const QStringList LOW_HORNS { "Bass Trombone", "Bari Sax", "Bass Sax", "Bassoon", "Bass Clarinet",
                                         "Contrabass Clarinet", "Contrabassoon", "Tuba" };
    const QString name = starscore::bandHornName(instrumentId);
    return LOW_HORNS.contains(name) ? name : QString();
}

std::vector<StarScoreHornChoice> StarScoreService::lowVersionsFor(const QString& mainInstrumentId) const
{
    const QString mainName = lowHornName(mainInstrumentId);
    std::vector<StarScoreHornChoice> result;
    for (const StarScoreHornChoice& c : lowHornChoices()) {
        if (c.bandName != mainName) {
            result.push_back(c);
        }
    }
    return result;
}

std::vector<StarScoreHornChoice> StarScoreService::versionsFor(const QString& mainInstrumentId, const QString& sectionTemplateKey) const
{
    const QString key = sectionTemplateKey;
    if (key == "lead-sheet" || key == "rhythm" || key.endsWith("-any") || key.startsWith("bigband-") || key.startsWith("orch-")
        || key.startsWith("marching-")) {
        return {};
    }
    if (key == "7-horn" && !lowHornName(mainInstrumentId).isEmpty()) {
        return lowVersionsFor(mainInstrumentId);
    }
    // 1-Horn (Joel, 5 Oct 2026): the Trumpet sheet is written; the other three are made from it once it's Finished
    if (key == "1-horn" && starscore::bandHornName(mainInstrumentId) == "Trumpet") {
        return { { "alto-saxophone", "Alto Saxophone", "Eb Saxophone" }, { "tenor-saxophone", "Tenor Saxophone", "Bb Saxophone" },
                 { "trombone", "Trombone", "Trombone" } };
    }
    // A piccolo part: the band's flute player reads the same written notes an octave lower (Bet's flute part was a
    // piccolo labelled "Flute" until 1.17.1)
    if (starscore::bandHornName(mainInstrumentId) == "Piccolo") {
        return { { "flute", "Flute", "Flute" } };
    }
    return {};
}

QString StarScoreService::versionMainName(const QString& instrumentId)
{
    return starscore::bandHornName(instrumentId);
}

std::vector<StarScoreInstrument> StarScoreService::templateInstrumentsFor(const StarScoreSectionTemplate& t,
                                                                          const QString& doublerInstrumentId,
                                                                          const QString& lowHornInstrumentId) const
{
    std::vector<StarScoreInstrument> instruments = t.instruments;
    // the doubler (Ben) on Bass Clarinet: the 7th horn is never a second bass clarinet (Joel, 5 Oct 2026)
    const QString low = doublerInstrumentId == "bb-bass-clarinet" && lowHornInstrumentId == "bb-bass-clarinet"
                        ? QString("bass-trombone") : lowHornInstrumentId;
    // Which chair the doubler takes: the alto in 3/4/5-Horn, the soprano in 6/7-Horn (the alto is a player of its own there)
    QString doublerChair;
    if (t.key == "3-horn" || t.key == "4-horn" || t.key == "5-horn") {
        doublerChair = "alto-saxophone";
    } else if (t.key == "6-horn" || t.key == "7-horn") {
        doublerChair = "soprano-saxophone";
    }
    auto replaceChair = [&](const QString& chairId, const QString& withId, const std::vector<StarScoreHornChoice>& choices) {
        if (withId.isEmpty() || withId == chairId) {
            return;
        }
        for (const StarScoreHornChoice& c : choices) {
            if (c.instrumentId != withId) {
                continue;
            }
            for (StarScoreInstrument& inst : instruments) {
                if (inst.instrumentId == chairId) {
                    inst.instrumentId = c.instrumentId;
                    inst.partName = c.partName;
                    inst.shortName.clear();
                    return;   // one chair only
                }
            }
        }
    };
    if (!doublerChair.isEmpty()) {
        replaceChair(doublerChair, doublerInstrumentId, doublerChoices());
        // a tenor doubler is Tenor Sax 2, never 1: it goes after the section's own tenor (names are numbered in order)
        if (doublerInstrumentId == "tenor-saxophone") {
            auto first = std::find_if(instruments.begin(), instruments.end(),
                                      [](const StarScoreInstrument& i) { return i.instrumentId == "tenor-saxophone"; });
            auto second = first == instruments.end() ? first
                          : std::find_if(first + 1, instruments.end(),
                                         [](const StarScoreInstrument& i) { return i.instrumentId == "tenor-saxophone"; });
            if (second != instruments.end()) {
                // the doubler sits in the earlier (alto or soprano) chair: moved to just after the tenor chair
                std::rotate(first, first + 1, second + 1);
            }
        }
    }
    if (t.key == "7-horn") {
        replaceChair("bass-trombone", low, lowHornChoices());
    }
    return instruments;
}

QString StarScoreService::rosterDoublerName() const
{
    const QString band = bandFolder();
    if (band.isEmpty()) {
        return QString();
    }
    starscore::org::Roster roster;
    if (!roster.load(starscore::org::Paths::make(band, QString())).isEmpty()) {
        return QString();   // there but unreadable: the label does without the name
    }
    // The 3-horn alto chair's player is the doubler
    for (const starscore::org::Player* p : roster.currentHorns()) {
        auto it = p->chairs.find(3);
        if (it != p->chairs.end() && it->second.instrument.trimmed().compare("alto sax", Qt::CaseInsensitive) == 0) {
            return p->name;
        }
    }
    // Else whoever reads both the alto and the soprano
    for (const starscore::org::Player* p : roster.currentHorns()) {
        bool alto = false, soprano = false;
        for (const QString& inst : p->instruments) {
            alto |= inst.compare("Alto Sax", Qt::CaseInsensitive) == 0;
            soprano |= inst.compare("Soprano Sax", Qt::CaseInsensitive) == 0;
        }
        if (alto && soprano) {
            return p->name;
        }
    }
    return QString();
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
    auto findTemplate = [&](const QString& key) -> const StarScoreArrangementTemplate* {
        for (const StarScoreArrangementTemplate& a : arrangementTpls) {
            if (a.key == key) {
                return &a;
            }
        }
        return nullptr;
    };
    arrangement = findTemplate(options.arrangementTemplateKey);
    if (!arrangement) {
        // 3-Horn Standard by default (looked up by key: by position this picked 2-Horn Standard)
        arrangement = findTemplate("3-horn-standard");
    }
    if (!arrangement) {
        arrangement = &arrangementTpls.front();
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
    projectOptions.subtitle = options.subtitle;
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
            // the dialog's doubler and 7th-horn choices take their chairs in the horn section
            for (const StarScoreInstrument& inst : templateInstrumentsFor(t, options.doublerInstrumentId, options.lowHornInstrumentId)) {
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
    QStringList partIds;
    for (StarScoreSection& s : data.sections) {
        if (s.id == sectionId) {
            s.status = status;
            s.autoStatus = false;
            partIds = s.partIds;
        }
    }
    // a section marked Finished: its sheets no longer need auditing, and keep their layout
    if (status == StarScoreStatus::Finished) {
        markPartsAudited(data, masterScore(), partIds, true);
    }
    store(data);
    if (status == StarScoreStatus::Finished) {
        lockFinishedParts(partIds);
    }
}

void StarScoreService::setSectionAutoStatus(const QString& sectionId)
{
    Data data = load();
    for (StarScoreSection& s : data.sections) {
        if (s.id == sectionId) {
            s.autoStatus = true;
        }
    }
    store(data);
}

std::map<QString, StarScoreStatus> StarScoreService::partStatuses() const
{
    std::map<QString, StarScoreStatus> result;
    for (const auto& [pid, key] : load().partStatus) {
        result[pid] = statusFromKey(key);
    }
    return result;
}

std::vector<mu::engraving::Part*> StarScoreService::masterPartsOf(const engraving::Score* score, const engraving::MasterScore* ms)
{
    std::vector<engraving::Part*> result;
    if (!score || !ms || score == ms) {
        return result;
    }
    for (engraving::Part* p : score->parts()) {
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

//! Main-score part ids of the parts in a part score
static QStringList starscorePartIdsOfScore(const mu::engraving::Score* score, const mu::engraving::MasterScore* ms)
{
    QStringList ids;
    for (const mu::engraving::Part* p : StarScoreService::masterPartsOf(score, ms)) {
        ids << StarScoreService::idTextOf(p);
    }
    return ids;
}

int StarScoreService::partScoreStatus(const engraving::Score* score) const
{
    const engraving::MasterScore* ms = score ? score->masterScore() : nullptr;
    const QStringList ids = starscorePartIdsOfScore(score, ms);
    if (ids.isEmpty()) {
        return -1;
    }
    const Data data = loadFrom(ms);
    // a Big Band / Orchestra / Marching Band full score: its own status
    const QString family = starscoreFamilyArrangementOfScore(score, ms, data.arrangements);
    if (!family.isEmpty()) {
        auto it = data.scoreStatus.find(family);
        return it == data.scoreStatus.end() ? -1 : int(statusFromKey(it->second));
    }
    bool anyTag = false;
    StarScoreStatus result = StarScoreStatus::Finished;
    for (const QString& id : ids) {
        auto it = data.partStatus.find(id);
        if (it == data.partStatus.end()) {
            result = StarScoreStatus::Empty;
        } else {
            anyTag = true;
            result = std::min(result, statusFromKey(it->second));
        }
    }
    return anyTag ? int(result) : -1;
}

void StarScoreService::setPartScoreStatus(const engraving::Score* score, int status)
{
    engraving::MasterScore* ms = score ? const_cast<engraving::MasterScore*>(score->masterScore()) : nullptr;
    const QStringList ids = starscorePartIdsOfScore(score, ms);
    if (ids.isEmpty()) {
        return;
    }
    Data data = loadFrom(ms);
    // a Big Band / Orchestra / Marching Band full score: its own status, the parts keep theirs
    const QString family = starscoreFamilyArrangementOfScore(score, ms, data.arrangements);
    if (!family.isEmpty()) {
        if (status < 0) {
            data.scoreStatus.erase(family);
        } else {
            data.scoreStatus[family] = statusKey(static_cast<StarScoreStatus>(status));
        }
        // the project this score belongs to is marked unsaved: in a solo view that is the solo project, not the main one
        storeTo(ms, data, projectOf(ms));
        if (status == int(StarScoreStatus::Finished)) {
            lockFinishedScore(const_cast<engraving::Score*>(score));
        }
        return;
    }
    for (const QString& id : ids) {
        if (status < 0) {
            data.partStatus.erase(id);
        } else {
            data.partStatus[id] = statusKey(static_cast<StarScoreStatus>(status));
        }
    }
    markPartsAudited(data, ms, ids, status == int(StarScoreStatus::Finished));
    storeTo(ms, data, projectOf(ms));
    if (status == int(StarScoreStatus::Finished)) {
        lockFinishedScore(const_cast<engraving::Score*>(score));
        offerLowAlternates(ids);
    }
}

//! A sheet marked Finished keeps its layout: every system locked, a page break after each page's last system
void StarScoreService::lockFinishedScore(engraving::Score* score)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!score || !master || score->isMaster()) {
        return;   // the main score is a workspace of every arrangement, not a sheet
    }
    master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Lock the finished sheet's layout"));
    const int added = starscore::lockSheetLayout(score);
    master->notation()->undoStack()->commitChanges();
    if (added > 0) {
        master->notation()->notationChanged().notify();
    }
}

//! The part scores of these parts (one instrument each), their layout kept now they're Finished
void StarScoreService::lockFinishedParts(const QStringList& partIds)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return;
    }
    for (engraving::Excerpt* ex : ms->excerpts()) {
        engraving::Score* es = ex ? ex->excerptScore() : nullptr;
        if (!es || es->parts().size() != 1) {
            continue;
        }
        const std::vector<engraving::Part*> parts = masterPartsOf(es, ms);
        if (parts.size() == 1 && partIds.contains(idText(parts.front()))) {
            lockFinishedScore(es);
        }
    }
}

void StarScoreService::setPartStatus(const QString& partId, int status)
{
    Data data = load();
    if (status < 0) {
        data.partStatus.erase(partId);
    } else {
        data.partStatus[partId] = statusKey(static_cast<StarScoreStatus>(status));
    }
    markPartsAudited(data, masterScore(), { partId }, status == int(StarScoreStatus::Finished));
    store(data);
    if (status == int(StarScoreStatus::Finished)) {
        lockFinishedParts({ partId });
        offerLowAlternates({ partId });
    }
}

void StarScoreService::setSectionSkipSheet(const QString& sectionId, const QString& which, bool skip)
{
    Data data = load();
    for (StarScoreSection& s : data.sections) {
        if (s.id != sectionId) {
            continue;
        }
        s.skipSheets.removeAll(which);
        if (skip) {
            s.skipSheets << which;
        }
        store(data);
        return;
    }
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
        if (partIdsToRemove.contains(idText(p))) {
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

RetVal<QString> StarScoreService::createArrangementFromTemplate(const QString& templateKey, const QString& doublerInstrumentId,
                                                                const QString& lowHornInstrumentId)
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

    // Reuse sections that already exist (same template key); create only the missing ones (a new section is
    // stored as it's made, so the list is read once here, then kept up to date with the ones created)
    QStringList sectionIds;
    const Data before = load();
    std::vector<StarScoreSection> known = before.sections;
    for (const QString& sectionKey : tpl->sectionKeys) {
        QString existingId;
        for (const StarScoreSection& s : known) {
            if (s.templateKey == sectionKey) {
                existingId = s.id;
                break;
            }
        }
        if (existingId.isEmpty()) {
            // the doubler and 7th horn choices are for the horn section ("3-horn"); the others take their template
            static const QRegularExpression hornKey("^\\d+-horn$");
            const bool horns = hornKey.match(sectionKey).hasMatch();
            RetVal<QString> created = createSectionFromTemplate(sectionKey, horns ? doublerInstrumentId : QString(),
                                                                horns ? lowHornInstrumentId : QString());
            if (!created.ret) {
                LOGW() << "[starscore] could not create section " << sectionKey;
                continue;
            }
            existingId = created.val;
            StarScoreSection made;
            made.id = existingId;
            made.templateKey = sectionKey;
            known.push_back(made);
        }
        sectionIds << existingId;
    }

    // Name it after the template; "2-Horn Standard (2)" if that name is taken
    QString name = tpl->name;
    QStringList names;
    for (const StarScoreArrangement& a : before.arrangements) {
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
    static const QRegularExpression notIdRe("[^a-z0-9]+");
    base.replace(notIdRe, "-");

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

    if (excerpt->excerptScore()) {
        return masterPartsOf(excerpt->excerptScore(), ms);
    }
    // a part book without a score yet lists its parts itself
    for (engraving::Part* p : excerpt->parts()) {
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

    auto partIdText = [](const engraving::Part* p) { return idText(p); };

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
            static const QRegularExpression inCRe("in\\s*C\\b", QRegularExpression::CaseInsensitiveOption);
            const bool inC = rest.contains(inCRe) || rest.contains("any", Qt::CaseInsensitive);
            addSection(inC ? QString("%1-horn-any").arg(n) : QString("%1-horn").arg(n),
                       inC ? QString("%1-Horn Flexible").arg(n) : QString("%1-Horn Section").arg(n), parts);
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
    // part books number their bars like the main score
    starscore::syncBarNumbering(ms, starscore::BarNumberingSync::Direct);

    // Part books: keep those whose instruments all belong to the arrangement
    ExcerptNotationList keptExcerpts;
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        engraving::Excerpt* ex = starscoreExcerptOf(e);
        if (!ex || !ex->excerptScore()) {
            continue;
        }
        const std::vector<engraving::Part*> mps = masterPartsOf(ex->excerptScore(), ms);
        const bool allKept = mps.size() == ex->excerptScore()->parts().size()
                             && std::all_of(mps.begin(), mps.end(), [&](const engraving::Part* mp) { return keep.count(idText(mp)) > 0; });
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
        if (keep.count(idText(p))) {
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
    if (m_styleSettings) {
        return *m_styleSettings;
    }
    StyleSettings settings;
    QFile file(globalConfiguration()->userAppDataPath().appendingComponent("starscore_styles.json").toQString());
    if (file.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        settings.defaultStyle = root.value("defaultStyle").toString();
        settings.bandFolder = root.value("bandFolder").toString();
        settings.builtinStyleVersion = root.value("builtinStyleVersion").toInt();
        settings.exportUnticked = root.value("exportUnticked").toObject();
        for (const QJsonValue& v : root.value("rules").toArray()) {
            const QJsonObject o = v.toObject();
            settings.rules.push_back({ o.value("section").toString(), o.value("part").toString(), o.value("style").toString() });
        }
    }
    m_styleSettings = settings;
    return settings;
}

void StarScoreService::saveStyleSettings(const StyleSettings& settings)
{
    m_styleSettings = settings;
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
    static const int BUILTIN_STYLE_VERSION = 9;   // 1 = Starsign 2.0, 2 = 2.1, 3 = 2.2, 4 = 2.3, 5 = 2.4, 6 = 2.5, 7 = 2.6, 8 = 2.6 with H-bar 0.7sp, 9 = Futura staff text and text lines

    const QString dir = globalConfiguration()->userAppDataPath().appendingComponent("StarScoreStyles").toQString();
    const QString target = dir + "/Starsign 2.6.mss";

    StyleSettings settings = loadStyleSettings();
    const bool newVersion = settings.builtinStyleVersion < BUILTIN_STYLE_VERSION;
    if (!newVersion && QFileInfo::exists(target)) {
        return;
    }

    QDir().mkpath(dir);
    QFile::remove(target);
    if (!QFile::copy(":/resources/starscore/Starsign_2.6.mss", target)) {
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
    const Data data = load();
    const int restyled = applyStylesOnly(partIds, data);
    applyMixerDefaults(partIds);
    // horn part scores show their exported title (after the style: the composer credit moves clear of the label)
    labelPartBooks();

    // Default layout: the lead sheet's bass staff shows only where it has music
    if (partIds.isEmpty()) {
        IMasterNotationPtr master = globalContext()->currentMasterNotation();
        engraving::MasterScore* ms = masterScore();
        if (master && ms) {
            for (const StarScoreSection& s : data.sections) {
                if (s.templateKey == "lead-sheet" && !s.partIds.isEmpty()) {
                    autoHideLeadBassStaff(master, ms->partById(ID(s.partIds.front())));
                }
            }
        }
    }
    return restyled;
}

int StarScoreService::applyStylesOnly(const QStringList& partIds, const Data& data)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return 0;
    }

    syncMinMajDefaults(data);

    const StyleSettings settings = loadStyleSettings();
    const QString version = scoreVersion();

    auto usable = [](const QString& path) {
        return !path.isEmpty() && QFileInfo::exists(path);
    };

    // Style file(s), then the house settings a style file can't hold, in one undo step per score
    auto restyle = [&](INotationPtr n, const QString& ruleStyle, bool partBook) {
        if (!n) {
            return false;
        }
        engraving::Score* score = n->elements()->msScore();
        if (!score) {
            return false;
        }
        // Each edit's end lays out every open score (the main score and each open part score), and a restyle is
        // three edits: Branston Pickle, with dozens of part scores open, took many minutes (Joel, 6 Oct 2026). So
        // this score's updates are held while it's restyled, and it is laid out on its own: once first (a style
        // change looks through the score's systems, which must be current), then by the house style where it
        // measures, and once at the end.
        score->setLayoutAll();
        score->doLayout();
        score->lockUpdates(true);
        bool changed = false;
        if (usable(settings.defaultStyle)) {
            changed |= n->style()->loadStyle(settings.defaultStyle, true);
        }
        if (!ruleStyle.isEmpty()) {
            changed |= n->style()->loadStyle(ruleStyle, true);
        }
        n->undoStack()->prepareChanges(TranslatableString::untranslatable("StarScore house style"));
        starscore::applyHouseStyle(score, partBook, version);
        n->undoStack()->commitChanges();
        score->lockUpdates(false);
        score->setLayoutAll();
        score->doLayout();
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
                // a rule may name the part with or without its section ("7H: Bari Sax" or "Bari Sax")
                const QString bare = partName.contains(": ") ? partName.mid(partName.lastIndexOf(": ") + 2).trimmed() : partName;
                const bool partOk = r.partName.trimmed().isEmpty()
                                    || r.partName.trimmed().compare(partName, Qt::CaseInsensitive) == 0
                                    || r.partName.trimmed().compare(bare, Qt::CaseInsensitive) == 0
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

    // The Keys sheet: empty staves hide (first system included) and the bass staff is set to "Always hide"
    if (starscore::applyKeysStaffRules(ms) > 0) {
        if (INotationProjectPtr project = globalContext()->currentProject()) {
            project->markAsUnsaved();
        }
        master->notation()->notationChanged().notify();
    }

    return restyled;
}

// Entries inside the .starscore zip holding the solo scores and the reference PDFs
static const QString STARSCORE_SOLOS_DIR("StarScoreSolos");
static const QString STARSCORE_REFS_DIR("StarScoreReferences");

// ---------------------------------------------------------------------------
//  Reference PDFs
// ---------------------------------------------------------------------------

std::vector<StarScoreReference> StarScoreService::references() const
{
    if (!m_mainProject) {
        return {};
    }
    return loadFrom(m_mainProject->masterNotation()->masterScore()).references;
}

io::path_t StarScoreService::referencePath(const QString& referenceId) const
{
    return io::path_t(m_workDir + "/ref-" + referenceId + ".pdf");
}

RetVal<QString> StarScoreService::addReference(const io::path_t& pdfFile)
{
    if (!m_mainProject || m_workDir.isEmpty()) {
        return RetVal<QString>::make_ret(Ret::Code::InternalError);
    }
    const QFileInfo fi(pdfFile.toQString());
    if (!fi.exists()) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "File not found."));
    }

    Data data = loadFrom(m_mainProject->masterNotation()->masterScore());
    QStringList ids;
    QStringList names;
    for (const StarScoreReference& r : data.references) {
        ids << r.id;
        names << r.name.toLower();
    }

    StarScoreReference ref;
    ref.id = uniqueId(ids, "ref");
    ref.name = fi.completeBaseName();
    for (int n = 2; names.contains(ref.name.toLower()); ++n) {
        ref.name = QString("%1 (%2)").arg(fi.completeBaseName()).arg(n);
    }
    ref.file = STARSCORE_REFS_DIR + "/" + ref.id + ".pdf";

    QFile::remove(referencePath(ref.id).toQString());
    if (!QFile::copy(fi.absoluteFilePath(), referencePath(ref.id).toQString())) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "Couldn't copy the file."));
    }

    data.references.push_back(ref);
    storeTo(m_mainProject->masterNotation()->masterScore(), data, m_mainProject);
    return RetVal<QString>::make_ok(ref.id);
}

QString StarScoreService::identicalReferenceName(const io::path_t& pdfFile) const
{
    QFile in(pdfFile.toQString());
    if (!in.open(QIODevice::ReadOnly)) {
        return QString();
    }
    const QByteArray data = in.readAll();
    for (const StarScoreReference& r : references()) {
        QFile existing(referencePath(r.id).toQString());
        if (existing.size() == data.size() && existing.open(QIODevice::ReadOnly) && existing.readAll() == data) {
            return r.name;
        }
    }
    return QString();
}

QString StarScoreService::currentReferenceId() const
{
    const std::vector<StarScoreReference> refs = references();
    for (const StarScoreReference& r : refs) {
        if (r.id == m_currentReferenceId) {
            return r.id;
        }
    }
    return refs.empty() ? QString() : refs.front().id;
}

// ---------------------------------------------------------------------------
//  Reference PDF view per part score: which PDF is showing, or that the panel is closed.
//  Kept in the app settings per file, so it comes back after quitting even without saving the score.
// ---------------------------------------------------------------------------

static const QString STARSCORE_REFERENCE_PANEL("starscoreReferencePanel");
// one entry for the whole file: "open" or "closed" (the panel when the file was last closed)
static const QString REFERENCE_PANEL_KEY("(panel)");

//! Settings keys for this file: by its permanent id (survives moving and renaming) and, as a fallback for
//! files that haven't been saved with an id yet, by its path
QStringList StarScoreService::referenceViewSettingsKeys() const
{
    QStringList keys;
    if (!m_mainProject) {
        return keys;
    }
    const QString fileId = loadFrom(m_mainProject->masterNotation()->masterScore()).fileId;
    if (!fileId.isEmpty()) {
        keys << "StarScore/referenceView/id-" + fileId;
    }
    const QByteArray path = m_mainProject->path().toQString().toUtf8();
    keys << "StarScore/referenceView/" + QString::fromLatin1(QCryptographicHash::hash(path, QCryptographicHash::Md5).toHex());
    return keys;
}

QJsonObject StarScoreService::loadReferenceView() const
{
    QSettings settings;
    for (const QString& key : referenceViewSettingsKeys()) {
        if (settings.contains(key)) {
            return QJsonDocument::fromJson(settings.value(key).toByteArray()).object();
        }
    }
    return QJsonObject();
}

//! Give the main score a permanent id if it has none (saved with the file the next time it is saved).
//! Doesn't mark the score as changed.
void StarScoreService::ensureFileId()
{
    if (!m_mainProject) {
        return;
    }
    engraving::MasterScore* ms = m_mainProject->masterNotation()->masterScore();
    if (!ms || ms->metaTag(STARSCORE_META_TAG).isEmpty()) {
        return;
    }
    Data data = fromJson(ms->metaTag(STARSCORE_META_TAG).toQString());
    if (!data.fileId.isEmpty()) {
        return;
    }
    // Carry over the reference PDF memory stored by path before the file had an id
    const QJsonObject view = loadReferenceView();
    data.fileId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    ms->setMetaTag(STARSCORE_META_TAG, String::fromQString(toJson(data)));
    if (!view.isEmpty()) {
        QSettings().setValue("StarScore/referenceView/id-" + data.fileId, QJsonDocument(view).toJson(QJsonDocument::Compact));
    }
}

//! Remembers, for this file: whether the reference panel is open (one setting for the whole file, so it opens
//! with the file only if it was open when the file was last closed), and which PDF shows with this score or part
void StarScoreService::recordReferenceView()
{
    if (m_restoringReferenceView || !m_mainProject) {
        return;
    }
    const QStringList settingsKeys = referenceViewSettingsKeys();
    muse::dock::IDockWindow* window = dockWindowProvider()->window();
    if (settingsKeys.isEmpty() || !window) {
        return;
    }
    const bool open = window->isDockOpen(STARSCORE_REFERENCE_PANEL);
    QJsonObject view = loadReferenceView();
    const QJsonObject before = view;
    view[REFERENCE_PANEL_KEY] = open ? QString("open") : QString("closed");
    const QString id = currentReferenceId();
    if (open && !id.isEmpty()) {
        view[currentScoreKey().isEmpty() ? QString("(main score)") : currentScoreKey()] = id;
    }
    if (view != before) {
        QSettings settings;
        for (const QString& settingsKey : settingsKeys) {
            settings.setValue(settingsKey, QJsonDocument(view).toJson(QJsonDocument::Compact));
        }
    }
}

void StarScoreService::listenReferencePanel()
{
    muse::dock::IDockWindow* window = dockWindowProvider()->window();
    if (!window) {
        return;
    }
    window->docksOpenStatusChanged().onReceive(this, [this](const QStringList& names) {
        if (!names.contains(STARSCORE_REFERENCE_PANEL) || m_restoringReferenceView) {
            return;
        }
        // Every panel at once: the notation page has just been (re)loaded, which starts the panel closed. Put it
        // the way this file has it.
        if (names.size() > 1) {
            QTimer::singleShot(0, &m_timerGuard, [this]() { applyReferencePanelState(); });
            return;
        }
        // One panel: you opened or closed it
        m_referencePanelTouched = true;
        recordReferenceView();
    });
}

//! Opens or closes the reference panel the way this file had it when it was last closed (closed if never opened)
void StarScoreService::applyReferencePanelState()
{
    muse::dock::IDockWindow* window = dockWindowProvider()->window();
    if (!m_mainProject || !window) {
        return;
    }
    const Data data = loadFrom(m_mainProject->masterNotation()->masterScore());
    const bool wantOpen = !data.references.empty() && loadReferenceView().value(REFERENCE_PANEL_KEY).toString() == "open";
    if (window->isDockOpen(STARSCORE_REFERENCE_PANEL) != wantOpen) {
        m_restoringReferenceView = true;
        window->setDockOpen(STARSCORE_REFERENCE_PANEL, wantOpen);
        m_restoringReferenceView = false;
    }
}

void StarScoreService::setCurrentReferenceId(const QString& referenceId)
{
    const bool changed = m_currentReferenceId != referenceId;
    m_currentReferenceId = referenceId;
    recordReferenceView();
    if (changed) {
        scheduleChanged();
    }
}

QString StarScoreService::currentScoreKey() const
{
    INotationPtr current = globalContext()->currentNotation();
    if (!current || !m_mainProject || current == m_mainProject->masterNotation()->notation()) {
        return QString();
    }
    return current->name();
}

//! Which reference PDF shows with the current score or part: the one last shown with it, or one tagged with
//! one of its instruments. Just after a file opens (until you open or close the panel yourself), also opens or
//! closes the panel the way the file had it.
void StarScoreService::pickReferenceForCurrentScore()
{
    if (!m_mainProject) {
        return;
    }
    const Data data = loadFrom(m_mainProject->masterNotation()->masterScore());
    const bool opening = !m_referencePanelTouched && m_projectOpenedMs > 0
                         && QDateTime::currentMSecsSinceEpoch() - m_projectOpenedMs < 6000;
    if (opening) {
        applyReferencePanelState();
    }
    if (data.references.empty()) {
        return;
    }
    auto exists = [&](const QString& id) {
        return std::any_of(data.references.begin(), data.references.end(), [&](const StarScoreReference& r) { return r.id == id; });
    };

    const QString scoreKey = currentScoreKey().isEmpty() ? QString("(main score)") : currentScoreKey();
    QString chosen = loadReferenceView().value(scoreKey).toString();
    if (!exists(chosen)) {
        chosen.clear();
    }
    if (chosen.isEmpty()) {
        if (INotationPtr current = globalContext()->currentNotation()) {
            QStringList names;
            if (current->elements() && current->elements()->msScore()) {
                for (const engraving::Part* p : current->elements()->msScore()->parts()) {
                    names << p->partName().toQString().toLower() << p->instrument()->nameAsPlainText().toQString().toLower();
                }
            }
            for (const StarScoreReference& r : data.references) {
                const QString tag = r.instrument.trimmed().toLower();
                if (tag.isEmpty()) {
                    continue;
                }
                if (std::any_of(names.begin(), names.end(), [&](const QString& n) { return n == tag || n.contains(tag); })) {
                    chosen = r.id;
                    break;
                }
            }
        }
    }
    if (!chosen.isEmpty() && chosen != m_currentReferenceId) {
        m_currentReferenceId = chosen;
        scheduleChanged();
    }
}

void StarScoreService::renameReference(const QString& referenceId, const QString& name)
{
    const QString n = name.simplified();
    if (!m_mainProject || n.isEmpty()) {
        return;
    }
    Data data = loadFrom(m_mainProject->masterNotation()->masterScore());
    for (StarScoreReference& r : data.references) {
        if (r.id == referenceId && r.name != n) {
            r.name = n;
            storeTo(m_mainProject->masterNotation()->masterScore(), data, m_mainProject);
            return;
        }
    }
}

void StarScoreService::setReferenceInstrument(const QString& referenceId, const QString& instrument)
{
    if (!m_mainProject) {
        return;
    }
    Data data = loadFrom(m_mainProject->masterNotation()->masterScore());
    for (StarScoreReference& r : data.references) {
        if (r.id == referenceId && r.instrument != instrument) {
            r.instrument = instrument;
            storeTo(m_mainProject->masterNotation()->masterScore(), data, m_mainProject);
            return;
        }
    }
}

void StarScoreService::moveReference(const QString& referenceId, int newIndex)
{
    if (!m_mainProject) {
        return;
    }
    Data data = loadFrom(m_mainProject->masterNotation()->masterScore());
    auto it = std::find_if(data.references.begin(), data.references.end(), [&](const StarScoreReference& r) { return r.id == referenceId; });
    if (it == data.references.end() || newIndex < 0 || newIndex >= int(data.references.size())) {
        return;
    }
    StarScoreReference ref = *it;
    data.references.erase(it);
    data.references.insert(data.references.begin() + newIndex, ref);
    storeTo(m_mainProject->masterNotation()->masterScore(), data, m_mainProject);
}

QStringList StarScoreService::referenceInstrumentChoices() const
{
    QStringList names;
    if (!m_mainProject) {
        return names;
    }
    for (const engraving::Part* p : m_mainProject->masterNotation()->masterScore()->parts()) {
        QString n = p->instrument()->nameAsPlainText().toQString().trimmed();
        static const QRegularExpression trailingNumberRe("\\s+\\d+$");
        n.remove(trailingNumberRe);   // "Trumpet 2" -> "Trumpet"
        if (n.isEmpty()) {
            n = p->partName().toQString();
        }
        if (!names.contains(n, Qt::CaseInsensitive)) {
            names << n;
        }
    }
    return names;
}

int StarScoreService::referencePageCount(const QString& referenceId) const
{
    return starscore::pdfPageCount(referencePath(referenceId).toQString());
}

QString StarScoreService::referencePageImage(const QString& referenceId, int page, int widthPx) const
{
    // Cached per width, in steps of 100 px so resizing the panel doesn't redraw every pixel
    const int w = std::clamp(((widthPx + 99) / 100) * 100, 200, 4000);
    bool invert = true;
    for (const StarScoreReference& r : references()) {
        if (r.id == referenceId) {
            invert = r.invert;
        }
    }
    const QString png = QString("%1/ref-%2-p%3-w%4%5.png").arg(m_workDir, referenceId).arg(page).arg(w).arg(invert ? "-inv" : "");
    if (QFileInfo::exists(png)) {
        return png;
    }
    if (m_workDir.isEmpty() || !starscore::renderPdfPage(referencePath(referenceId).toQString(), page, w, png)) {
        return QString();
    }
    if (invert) {
        // white-on-black to match the app's dark theme; the stored PDF and exports are unchanged
        QImage image(png);
        if (!image.isNull()) {
            image.invertPixels(QImage::InvertRgb);
            image.save(png, "PNG");
        }
    }
    return png;
}

void StarScoreService::setReferenceInvert(const QString& referenceId, bool invert)
{
    if (!m_mainProject) {
        return;
    }
    Data data = loadFrom(m_mainProject->masterNotation()->masterScore());
    for (StarScoreReference& r : data.references) {
        if (r.id == referenceId && r.invert != invert) {
            r.invert = invert;
            storeTo(m_mainProject->masterNotation()->masterScore(), data, m_mainProject);
            return;
        }
    }
}

void StarScoreService::removeReference(const QString& referenceId)
{
    if (!m_mainProject) {
        return;
    }
    Data data = loadFrom(m_mainProject->masterNotation()->masterScore());
    auto it = std::remove_if(data.references.begin(), data.references.end(),
                             [&](const StarScoreReference& r) { return r.id == referenceId; });
    if (it == data.references.end()) {
        return;
    }
    data.references.erase(it, data.references.end());
    storeTo(m_mainProject->masterNotation()->masterScore(), data, m_mainProject);
    QFile::remove(referencePath(referenceId).toQString());
    for (const QString& png : QDir(m_workDir).entryList({ "ref-" + referenceId + "-p*.png" }, QDir::Files)) {
        QFile::remove(m_workDir + "/" + png);
    }
}

// ---------------------------------------------------------------------------
//  Solo transcriptions (and the project switching they need)
// ---------------------------------------------------------------------------

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
    if (m_listening) {
        // a different score: nothing of the old one to restore
        m_listening = false;
        m_listenVisibility.clear();
        m_listenEndTick = -1;
        m_listeningChanged.notify();
    }

    m_mainProject->saveComplited().onReceive(this, [this](const io::path_t& path, SaveMode mode) {
        if (mode == SaveMode::Save || mode == SaveMode::SaveAs || mode == SaveMode::SaveCopy) {
            if (!solos().empty() || !references().empty()) {
                Ret ret = injectSolos(path);
                if (!ret) {
                    LOGE() << "[starscore] could not store solos in " << path << ": " << ret.toString();
                }
            }
        }
    });

    extractSolos();
    ensureFileId();
    // once the score is fully set up
    QTimer::singleShot(0, &m_timerGuard, [this]() { tidyOpenedScore(); });
    if (autotestRequested()) {
        startAutotest();
    }
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
    const std::vector<StarScoreReference> refs = references();
    if ((list.empty() && refs.empty()) || !QFileInfo::exists(m_mainProject->path().toQString())) {
        return;
    }

    ZipReader zip(m_mainProject->path());
    for (const StarScoreReference& ref : refs) {
        ByteArray data = zip.fileData(ref.file.toStdString());
        if (data.empty()) {
            LOGW() << "[starscore] reference file missing from the .starscore: " << ref.file;
            continue;
        }
        QFile out(referencePath(ref.id).toQString());
        if (out.open(QIODevice::WriteOnly)) {
            out.write(data.toQByteArrayNoCopy());
        }
    }
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
    globalContext()->setCurrentProject(project.val);   // its handler schedules changed()
    m_switching = false;
    listenCurrentProject();
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
    globalContext()->setCurrentProject(m_mainProject);   // its handler schedules changed()
    m_switching = false;
    listenCurrentProject();
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
        scheduleChanged();
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
            if (!info.isFile || QString::fromStdString(name).startsWith(STARSCORE_SOLOS_DIR + "/")
                || QString::fromStdString(name).startsWith(STARSCORE_REFS_DIR + "/")) {
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
        for (const StarScoreReference& ref : references()) {
            QFile f(referencePath(ref.id).toQString());
            if (!f.open(QIODevice::ReadOnly)) {
                LOGW() << "[starscore] missing working copy of reference " << ref.id;
                continue;
            }
            writer.addFile(ref.file.toStdString(), ByteArray::fromQByteArray(f.readAll()));
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
        // a damaged main score isn't saved over the file from here (Save from the main score asks first, as
        // MuseScore does): this was the one way to save it without that question
        const Ret canSave = m_mainProject->canSave();
        if (!canSave) {
            return Ret(static_cast<int>(Ret::Code::UnknownError),
                       muse::trc("starscore", "The main score is damaged, so it wasn't saved. Switch to the main score and "
                                              "save from there to see what's wrong.") + "\n\n" + canSave.text());
        }
        // saving the main score triggers injectSolos() through saveComplited
        return m_mainProject->save(io::path_t(), SaveMode::Save, true);
    }

    Ret ret = injectSolos(m_mainProject->path());
    scheduleChanged();
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
    syncMinMajDefaults(data);

    if (!created.empty()) {
        const StyleSettings settings = loadStyleSettings();
        const QString version = scoreVersion();
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
            starscore::applyHouseStyle(n->elements()->msScore(), false, version);
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
    syncMinMajDefaults(load());
}

void StarScoreService::syncMinMajDefaults(const Data& data)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return;
    }

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
    scheduleChanged();
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
    scheduleChanged();
}

muse::async::Notification StarScoreService::panelVisibleChanged() const
{
    return m_panelVisibleChanged;
}

//! The part score on screen: its composer credit clear of the title, subtitle and arrangement label. Measured on the
//! score as shown (laid out), so it holds however the part score was made; changes nothing when they're clear.
void StarScoreService::clearComposerInCurrentScore()
{
    INotationPtr n = globalContext()->currentNotation();
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!n || !master || n == master->notation() || !m_mainProject || globalContext()->currentProject() != m_mainProject) {
        return;
    }
    engraving::Score* score = n->elements() ? n->elements()->msScore() : nullptr;
    if (!score || load().sections.empty()) {
        return;
    }
    // part scores with a sheet title (instrument name top left; horn sheets also an arrangement label)
    bool hasTitle = false;
    for (engraving::MeasureBase* mb = score->first(); mb && !mb->isMeasure(); mb = mb->next()) {
        for (engraving::EngravingItem* e : mb->el()) {
            if (e && e->isText() && engraving::toText(e)->textStyleType() == engraving::TextStyleType::INSTRUMENT_EXCERPT) {
                hasTitle = true;
            }
        }
    }
    if (!hasTitle) {
        return;
    }
    score->doLayout();
    n->undoStack()->prepareChanges(TranslatableString::untranslatable("Composer clear of the arrangement label"));
    // the label on the instrument name's line (an export could leave it moved), then the credit clear of it
    starscore::levelArrangementLabel(score);
    starscore::clearComposerCredit(score);
    n->undoStack()->commitChanges();
    n->notationChanged().notify();
}

//! Opens the part scores of these parts (one instrument each), the first one showing
void StarScoreService::openPartBooks(const QStringList& partIds)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        return;
    }
    std::vector<INotationPtr> books;
    for (const QString& pid : partIds) {
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            INotationPtr n = e->notation();
            engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
            if (!es || es->parts().size() != 1) {
                continue;
            }
            const std::vector<engraving::Part*> parts = masterPartsOf(es, ms);
            if (parts.size() == 1 && idText(parts.front()) == pid) {
                books.push_back(n);
                break;
            }
        }
    }
    for (const INotationPtr& n : books) {
        master->setExcerptIsOpen(n, true);
    }
    if (!books.empty()) {
        globalContext()->setCurrentNotation(books.front());
    }
}
