/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Export to Sheets and Demos › "Export Audio Demos" (Joel, 5 Oct 2026): a WAV of each Standard
 * horn arrangement (2- to 7-Horn), and of Big Band, Orchestra and Marching Band, into the song's Demos folder.
 *
 * Each demo plays its arrangement's own score (the part book with all its instruments): the arrangement's sections
 * except the Lead Sheet, with the stand-in versions muted (a 7-Horn's other bass horns, a piccolo's flute) and every
 * chord-symbol track muted except on keys and guitar. The mixer's mute/solo state is put back afterwards.
 */
#include "starscoreservice.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/instrument.h"

#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationsolomutestate.h"
#include "notation/inotation.h"

#include "io/filestream.h"
#include "translation.h"
#include "log.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;

namespace {
//! Keys and guitar: the only instruments whose chord symbols play in a demo
bool starscoreKeysOrGuitar(const mu::engraving::Part* p)
{
    const QString family = p->instrument()->family().toQString();
    if (family == "keyboards" || family == "organs" || family == "synths" || family == "guitars") {
        return true;
    }
    const QString id = p->instrumentId().toQString();
    if (id.contains("bass")) {
        return false;
    }
    return id.contains("guitar") || id.contains("piano") || id.contains("keyboard") || id.contains("organ")
           || id.contains("synth") || id.contains("rhodes") || id.contains("clavinet") || id.contains("harpsichord");
}

//! The main score's id of a part (a part score's parts are copies; their staves are linked to the main score's)
QString starscoreMasterPartId(const mu::engraving::Part* p, const mu::engraving::MasterScore* ms)
{
    if (p->score() == ms || p->staves().empty()) {
        return p->id().toQString();
    }
    for (const mu::engraving::Staff* st : p->staves().front()->staffList()) {
        if (st->score() == ms) {
            return st->part()->id().toQString();
        }
    }
    return p->id().toQString();
}
}

RetVal<QString> StarScoreService::exportAudioDemos()
{
    const RetVal<StarScoreBandExportPlan> plan = planBandExport();
    if (!plan.ret) {
        return RetVal<QString>::make_ret(plan.ret);
    }
    if (plan.val.newSong || plan.val.songFolder.isEmpty()) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError,
                                         muse::trc("starscore", "This song isn't in Sheets and Demos yet."));
    }
    INotationProjectPtr project = exportSourceProject();
    IMasterNotationPtr master = project ? project->masterNotation() : nullptr;
    mu::engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    INotationWriterPtr writer = writers()->writer("wav");
    if (!ms || !writer) {
        return RetVal<QString>::make_ret(Ret::Code::InternalError);
    }

    // --- the demos: which arrangements, and the parts that play in each
    struct Demo {
        QString name;              // "3-Horn Arrangement"
        QString scoreName;         // the arrangement's own score
        std::set<QString> playing; // main-score part ids
    };
    std::vector<Demo> demos;
    QStringList skipped;
    const Data d = loadFrom(ms);
    static const QRegularExpression hornKey("^(\\d)-horn$");
    for (const StarScoreArrangement& a : d.arrangements) {
        int horns = 0;
        bool flexible = false;
        QString family;
        std::vector<const StarScoreSection*> sections;
        for (const StarScoreSection& s : d.sections) {
            if (!a.sectionIds.contains(s.id)) {
                continue;
            }
            sections.push_back(&s);
            const QRegularExpressionMatch m = hornKey.match(s.templateKey);
            if (m.hasMatch()) {
                horns = m.captured(1).toInt();
            } else if (s.templateKey.endsWith("-horn-any")) {
                flexible = true;
            } else if (s.templateKey.startsWith("bigband-")) {
                family = "Big Band";
            } else if (s.templateKey.startsWith("marching-")) {
                family = "Marching Band";
            } else if (s.templateKey.startsWith("orch-")) {
                family = "Orchestra";
            }
        }
        Demo demo;
        if (flexible) {
            continue;   // (no demos of the Flexible arrangements)
        } else if (horns >= 2) {
            demo.name = QString("%1-Horn Arrangement").arg(horns);
        } else if (!family.isEmpty()) {
            demo.name = family + " Arrangement";
        } else {
            continue;   // the 1-Horn melody sheets, a lead sheet on its own…
        }
        demo.scoreName = a.scoreName;
        for (const StarScoreSection* s : sections) {
            if (s->templateKey == "lead-sheet") {
                continue;
            }
            for (const QString& pid : s->partIds) {
                if (!s->alternates.count(pid)) {
                    demo.playing.insert(pid);
                }
            }
        }
        if (!demo.playing.empty() && std::none_of(demos.begin(), demos.end(), [&](const Demo& x) { return x.name == demo.name; })) {
            demos.push_back(demo);
        }
    }
    if (demos.empty()) {
        return RetVal<QString>::make_ok(muse::qtrc("starscore", "Audio demos: this song has no 2- to 7-Horn Standard, Big Band, "
                                                                "Orchestra or Marching Band arrangement."));
    }

    const QString demosDir = plan.val.bandFolder + "/" + plan.val.songFolder + "/Demos";
    QDir().mkpath(demosDir);

    // 48 kHz, 24-bit integer WAV (Joel, 5 Oct 2026); the export settings are put back afterwards
    const int oldRate = audioExportConfiguration()->exportSampleRate();
    const audio::AudioSampleFormat oldFormat = audioExportConfiguration()->exportWavSampleFormat();
    audioExportConfiguration()->setExportSampleRate(48000);
    audioExportConfiguration()->setExportWavSampleFormat(audio::AudioSampleFormat::Int24);

    QStringList written;
    QStringList failed;
    for (const Demo& demo : demos) {
        // the arrangement's own score: exactly its instruments, all shown (the main score may have sections hidden)
        INotationPtr n;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (!demo.scoreName.isEmpty() && e->name() == demo.scoreName && e->notation()) {
                n = e->notation();
            }
        }
        if (!n) {
            n = master->notation();
        }
        mu::engraving::Score* sc = n->elements()->msScore();
        LOGI() << "[starscore] demo " << demo.name << ": starting, from " << (n == master->notation() ? "the main score"
                                                                                : "\"" + demo.scoreName + "\"")
               << (n->isOpen() ? " (open)" : " (closed)");
        // a closed score was never laid out (bars without systems: Bet crashed making its demos, 6 Oct 2026): laid
        // out first, in page view, as an open tab would be
        if (sc && !n->isOpen()) {
            sc->setLayoutAll();
            sc->doLayout();
        }
        INotationSoloMuteStatePtr soloMute = n->soloMuteState();
        if (!sc || !soloMute) {
            failed << demo.name;
            continue;
        }

        // mute and unmute, remembering the mixer's state
        std::vector<std::pair<mu::engraving::InstrumentTrackId, INotationSoloMuteState::SoloMuteState> > before;
        auto set = [&](const mu::engraving::InstrumentTrackId& id, bool mute) {
            before.emplace_back(id, soloMute->trackSoloMuteState(id));
            INotationSoloMuteState::SoloMuteState st;
            st.mute = mute;
            st.solo = false;
            soloMute->setTrackSoloMuteState(id, st);
        };
        for (const mu::engraving::Part* p : sc->parts()) {
            const bool plays = demo.playing.count(starscoreMasterPartId(p, ms)) > 0;
            LOGI() << "[starscore] demo " << demo.name << ": " << p->partName() << (plays ? " plays" : " muted")
                   << (p->hasChordSymbol() ? (plays && starscoreKeysOrGuitar(p) ? ", chords play" : ", chords muted") : "");
            for (const auto& [tick, instrument] : p->instruments()) {
                set({ p->id(), instrument->id() }, !plays);
            }
            if (p->hasChordSymbol()) {
                set({ p->id(), String(u"chord_symbols") }, !(plays && starscoreKeysOrGuitar(p)));
            }
        }

        // written next to the old demo first, which is then replaced (deleted, not archived: Joel, 5 Oct 2026)
        const QString fileName = plan.val.code + " - " + demo.name + " Demo.wav";
        const QString target = demosDir + "/" + fileName;
        const QString tmp = demosDir + "/." + fileName + ".part";
        QFile::remove(tmp);
        Ret ret = make_ret(Ret::Code::InternalError);
        {
            io::FileStream out(tmp);
            out.setMeta("file_path", tmp.toStdString());
            if (out.open(io::IODevice::WriteOnly)) {
                ret = writer->write(n, out, {});
                out.close();
            }
        }

        for (auto it = before.rbegin(); it != before.rend(); ++it) {
            soloMute->setTrackSoloMuteState(it->first, it->second);
        }

        LOGI() << "[starscore] demo " << demo.name << ": rendered, " << (ret ? "ok" : ret.toString());
        if (ret && QFileInfo(tmp).size() > 0) {
            QFile::remove(target);
            if (QFile::rename(tmp, target)) {
                written << fileName;
                continue;
            }
        }
        LOGW() << "[starscore] demo not written: " << demo.name << " " << ret.toString();
        QFile::remove(tmp);
        failed << demo.name;
    }

    audioExportConfiguration()->setExportSampleRate(oldRate);
    audioExportConfiguration()->setExportWavSampleFormat(oldFormat);

    QString summary = muse::qtrc("starscore", "Audio demos written to %1/Demos: %2.").arg(plan.val.songFolder, written.join(", "));
    if (written.isEmpty()) {
        summary = muse::qtrc("starscore", "No audio demos were written.");
    }
    if (!failed.isEmpty()) {
        summary += "\n" + muse::qtrc("starscore", "Couldn't make the demo of: %1.").arg(failed.join(", "));
    }
    return RetVal<QString>::make_ok(summary);
}
