/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — default mixer settings. When the default style is applied, each instrument in a StarScore
 * section gets the sound, volume, pan, reverb send and (for a few) mute that Joel tuned in "Starsign Mixer Test"
 * (resources/starscore/mixer_defaults.json).
 *
 * Key: "<section template>|<instrument id>|<nth of that instrument in the section>", "…|chords" for the
 * instrument's chord-symbol track, "…|1ten" for a 6/7-Horn section with a single tenor.
 * Instruments without an entry: a Lead Sheet or Rhythm Section instrument takes the levels of the matching
 * role (keys, guitar, bass, drums, percussion) and keeps its own sound; chord-symbol tracks are muted except
 * the rhythm guitar's and keyboard's.
 */
#include "starscoreservice.h"

#include <map>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/instrument.h"

#include "log.h"

using namespace mu::project;
using namespace muse;

namespace {
using TrackMixSetting = mu::playback::IPlaybackController::TrackMixSetting;

const QJsonObject& mixerDefaults()
{
    static QJsonObject tracks;
    static bool loaded = false;
    if (!loaded) {
        loaded = true;
        QFile f(":/resources/starscore/mixer_defaults.json");
        if (f.open(QIODevice::ReadOnly)) {
            tracks = QJsonDocument::fromJson(f.readAll()).object().value("tracks").toObject();
        } else {
            LOGE() << "[starscore] mixer defaults missing";
        }
    }
    return tracks;
}

muse::audio::AudioResourceType resourceType(const QString& s)
{
    using muse::audio::AudioResourceType;
    for (const auto& [type, name] : muse::audio::RESOURCE_TYPE_MAP) {
        if (name == s) {
            return type;
        }
    }
    return AudioResourceType::Undefined;
}

TrackMixSetting settingFrom(const QJsonObject& e, bool withSound)
{
    TrackMixSetting s;
    s.setOutput = true;
    s.volumeDb = float(e.value("volumeDb").toDouble());
    s.balance = float(e.value("balance").toDouble());
    for (const QJsonValue& v : e.value("aux").toArray()) {
        s.auxSends.push_back(float(v.toDouble()));
    }
    if (e.contains("mute")) {
        s.setMute = true;
        s.mute = e.value("mute").toBool();
    }
    if (withSound) {
        const QJsonValue in = e.value("in");
        s.setInput = true;
        if (in.isString()) {
            s.autoInput = true;   // "auto": the sound profile's own choice (Muse Sounds when installed)
        } else {
            const QJsonObject r = in.toObject();
            s.resource.id = r.value("id").toString().toStdString();
            s.resource.vendor = r.value("vendor").toString().toStdString();
            s.resource.type = resourceType(r.value("type").toString());
            s.resource.hasNativeEditorSupport = r.value("hasNativeEditorSupport").toBool();
            const QJsonObject attrs = r.value("attributes").toObject();
            for (auto it = attrs.begin(); it != attrs.end(); ++it) {
                s.resource.attributes[String::fromQString(it.key())] = String::fromQString(it.value().toString());
            }
        }
    }
    return s;
}

//! Role of a Lead Sheet / Rhythm Section instrument without its own entry → the entry whose levels it takes
QString roleKey(const QString& sectionKey, const QString& iid)
{
    if (sectionKey == "lead-sheet") {
        return "lead-sheet|piano|1";
    }
    if (sectionKey != "rhythm") {
        return QString();
    }
    if (iid == "congas" || iid == "bongos" || iid == "percussion" || iid == "timbales" || iid == "cajon") {
        return "rhythm|congas|1";
    }
    if (iid == "drumset" || iid == "drum-kit") {
        return "rhythm|drumset|1";
    }
    if (iid.contains("guitar") && !iid.contains("bass")) {
        return "rhythm|electric-guitar|1";
    }
    if (iid.contains("bass") || iid == "contrabass") {
        return "rhythm|bass-guitar|1";
    }
    return "rhythm|piano|1";   // keyboards
}
}

void StarScoreService::applyMixerDefaults(const QStringList& partIds)
{
    engraving::MasterScore* ms = masterScore();
    if (!ms || !playbackController()) {
        return;
    }
    const QJsonObject& defaults = mixerDefaults();
    if (defaults.isEmpty()) {
        return;
    }

    std::map<engraving::InstrumentTrackId, TrackMixSetting> settings;
    const Data data = load();

    for (const StarScoreSection& section : data.sections) {
        // this section's instruments in score order
        std::vector<engraving::Part*> parts;
        for (engraving::Part* p : ms->parts()) {
            if (section.partIds.contains(idText(p))) {
                parts.push_back(p);
            }
        }

        int tenors = 0;
        for (const engraving::Part* p : parts) {
            tenors += p->instrumentId() == u"tenor-saxophone" ? 1 : 0;
        }
        const bool oneTenor6or7 = (section.templateKey == "6-horn" || section.templateKey == "7-horn") && tenors == 1;

        std::map<QString, int> nth;
        for (engraving::Part* p : parts) {
            const QString iid = p->instrumentId().toQString();
            const int n = ++nth[iid];
            if (!partIds.isEmpty() && !partIds.contains(idText(p))) {
                continue;
            }

            // 1-Horn melody sheets take the 4-Horn Section's levels (same four instruments)
            const QString mixTemplate = section.templateKey == "1-horn" ? QString("4-horn") : section.templateKey;
            const QString key = QString("%1|%2|%3").arg(mixTemplate, iid).arg(n);
            QString found;
            if (oneTenor6or7 && (iid == "alto-saxophone" || iid == "tenor-saxophone") && defaults.contains(key + "|1ten")) {
                found = key + "|1ten";
            } else if (defaults.contains(key)) {
                found = key;
            }

            const engraving::InstrumentTrackId mainId { p->id(), p->instrument()->id() };
            const QString role = roleKey(section.templateKey, iid);
            if (!found.isEmpty()) {
                settings[mainId] = settingFrom(defaults.value(found).toObject(), true);
            } else if (!role.isEmpty() && defaults.contains(role) && n == 1) {
                settings[mainId] = settingFrom(defaults.value(role).toObject(), false);   // levels only, own sound
            }

            if (p->hasChordSymbol()) {
                const engraving::InstrumentTrackId chordsId { p->id(), String(u"chord_symbols") };
                const QString chordKey = (found.isEmpty() ? key : QString(found).remove("|1ten")) + "|chords";
                const bool rhythmChords = section.templateKey == "rhythm"
                                          && (role == "rhythm|electric-guitar|1" || role == "rhythm|piano|1");
                if (defaults.contains(chordKey)) {
                    settings[chordsId] = settingFrom(defaults.value(chordKey).toObject(), true);
                } else if (rhythmChords && defaults.contains(role + "|chords")) {
                    settings[chordsId] = settingFrom(defaults.value(role + "|chords").toObject(), true);
                } else {
                    TrackMixSetting mute;
                    mute.setMute = true;
                    mute.mute = true;
                    settings[chordsId] = mute;
                }
            }
        }
    }

    if (!settings.empty()) {
        playbackController()->applyTrackMixSettings(settings);
    }
}
