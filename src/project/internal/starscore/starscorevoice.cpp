/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * Vocal covers (Joel, 19 Oct 2026). A vocal cover has a Voice section (one "Voice" part, treble clef, at sounding
 * pitch) with its own status. Its sheet goes in the lead sheet's folder, which is then called "1 Lead Sheet + Voice".
 *
 * "+ Voice" part scores: the Voice in a small staff above another part score's instruments ("Bass Guitar + Voice",
 * "Lead Sheet + Voice"). Each is formatted like the part score it's made from (its style, system and page breaks,
 * system locks, text positions and bar widths), again at every export, so the changes made to that part score
 * carry over. The lead sheet's is made by itself once the Voice and the lead sheet are both Finished; the others
 * come from "Add Voice to a part…". They're exported beside the part's own sheet ("CODE - Bass + Voice.pdf"), once
 * the Voice is Finished.
 *
 * A + Voice part score is told by its name (ending " + Voice") and its instruments (the Voice and at least one more).
 */
#include "starscoreservice.h"
#include "starscoreengraving.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QUuid>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/layoutbreak.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/style/style.h"
#include "io/file.h"

#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationstyle.h"
#include "notation/inotation.h"
#include "translation.h"

#include "log.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;

static const QString VOICE_SUFFIX = QStringLiteral(" + Voice");
static const QString LEAD_SHEET_VOICE = QStringLiteral("Lead Sheet + Voice");

QString StarScoreService::voicePartIdOf(const Data& data)
{
    for (const StarScoreSection& s : data.sections) {
        if (s.templateKey == "voice" && !s.partIds.isEmpty()) {
            return s.partIds.front();
        }
    }
    return QString();
}

bool StarScoreService::hasVoiceSection() const
{
    return !voicePartIdOf(load()).isEmpty();
}

std::vector<StarScoreService::VoiceScore> StarScoreService::voiceScores(const IMasterNotationPtr& master) const
{
    std::vector<VoiceScore> out;
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms) {
        return out;
    }
    const QString voice = voicePartIdOf(loadFrom(ms));
    if (voice.isEmpty()) {
        return out;
    }
    auto idsOf = [&](const IExcerptNotationPtr& e) {
        QStringList ids;
        INotationPtr n = e ? e->notation() : nullptr;
        const engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
        if (es) {
            for (const engraving::Part* p : masterPartsOf(es, ms)) {
                ids << idText(p);
            }
        }
        return ids;
    };
    const ExcerptNotationList excerpts = master->excerpts();
    for (const IExcerptNotationPtr& e : excerpts) {
        if (!e->name().endsWith(VOICE_SUFFIX)) {
            continue;
        }
        QStringList ids = idsOf(e);
        if (ids.size() < 2 || !ids.contains(voice)) {
            continue;
        }
        ids.removeAll(voice);
        VoiceScore v;
        v.score = e;
        v.voicePartId = voice;
        v.sourcePartIds = ids;
        // the part score of exactly those instruments, preferably the one named like it
        const QString base = e->name().left(e->name().size() - VOICE_SUFFIX.size());
        for (const IExcerptNotationPtr& s : excerpts) {
            if (s == e) {
                continue;
            }
            const QStringList sids = idsOf(s);
            if (sids.size() != ids.size() || !std::all_of(sids.begin(), sids.end(), [&](const QString& id) { return ids.contains(id); })) {
                continue;
            }
            if (!v.source || s->name() == base) {
                v.source = s->notation();
                v.sourceName = s->name();
            }
        }
        out.push_back(v);
    }
    return out;
}

QStringList StarScoreService::voicePartScoreChoices() const
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms) {
        return {};
    }
    const Data data = loadFrom(ms);
    const QString voice = voicePartIdOf(data);
    if (voice.isEmpty()) {
        return {};
    }
    std::set<QString> arrangementScores;
    for (const StarScoreArrangement& a : data.arrangements) {
        arrangementScores.insert(a.scoreName);
    }
    std::set<QString> haveVoice;
    for (const VoiceScore& v : voiceScores(master)) {
        haveVoice.insert(v.sourceName);
    }
    const std::vector<RhythmBook> books = [&]() {
        QStringList all;
        for (const engraving::Part* p : ms->parts()) {
            all << idText(p);
        }
        return rhythmBooks(ms, all);
    }();
    QStringList out;
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        INotationPtr n = e->notation();
        const engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
        if (!es || e->name().endsWith(VOICE_SUFFIX) || arrangementScores.count(e->name()) || haveVoice.count(e->name())) {
            continue;
        }
        const std::vector<engraving::Part*> parts = masterPartsOf(es, ms);
        if (parts.empty() || std::any_of(parts.begin(), parts.end(), [&](const engraving::Part* p) { return idText(p) == voice; })) {
            continue;
        }
        // one instrument, or a player's rhythm book ("Keys": Clavinet and Bass Synth)
        const bool book = std::any_of(books.begin(), books.end(), [&](const RhythmBook& b) { return b.name == e->name(); });
        if (parts.size() == 1 || book) {
            out << e->name();
        }
    }
    return out;
}

INotationPtr StarScoreService::makeVoiceScore(const IMasterNotationPtr& master, const QString& sourceName, const QString& name)
{
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms) {
        return nullptr;
    }
    const QString voice = voicePartIdOf(loadFrom(ms));
    engraving::Part* voicePart = voice.isEmpty() ? nullptr : ms->partById(ID(voice));
    ExcerptNotationList excerpts = master->excerpts();
    const engraving::Score* source = nullptr;
    for (const IExcerptNotationPtr& e : excerpts) {
        if (e->name() == sourceName && e->notation() && e->notation()->elements()) {
            source = e->notation()->elements()->msScore();
        }
        if (e->name() == name) {
            return nullptr;   // there already
        }
    }
    if (!voicePart || !source) {
        return nullptr;
    }
    // the Voice on top, then the part score's instruments in its order
    std::vector<engraving::Part*> parts { voicePart };
    for (engraving::Part* p : masterPartsOf(source, ms)) {
        if (p != voicePart) {
            parts.push_back(p);
        }
    }
    if (parts.size() < 2) {
        return nullptr;
    }
    IExcerptNotationPtr score = master->createEmptyExcerpt(name);
    score->setMasterParts(parts);
    excerpts.push_back(score);
    master->setExcerpts(excerpts);
    LOGI() << "[starscore] made " << name << " from " << sourceName;
    return score->notation();
}

bool StarScoreService::formatVoiceScore(const VoiceScore& v)
{
    INotationPtr n = v.score ? v.score->notation() : nullptr;
    engraving::Score* dst = n && n->elements() ? n->elements()->msScore() : nullptr;
    const engraving::Score* src = v.source && v.source->elements() ? v.source->elements()->msScore() : nullptr;
    if (!dst || !src) {
        return false;
    }
    // the source's style (staff size, spacing, fonts, page) straight into the part score
    const QString mss = QDir::tempPath() + "/starscore-voice-" + QUuid::createUuid().toString(QUuid::Id128) + ".mss";
    if (v.source->style()->saveStyle(io::path_t(mss))) {
        muse::io::File styleFile { io::path_t(mss) };
        dst->loadStyle(styleFile, true);
    }
    QFile::remove(mss);
    // the Voice staff small
    for (engraving::Part* p : dst->parts()) {
        for (engraving::Staff* st : p->staves()) {
            const engraving::Staff* linked = st->findLinkedInScore(dst->masterScore());
            if (linked && idText(linked->part()) == v.voicePartId && !st->staffType(engraving::Fraction(0, 1))->isSmall()) {
                st->undoChangeProperty(engraving::Pid::SMALL, true);
            }
        }
    }
    // the instruments named without their section ("Tenor Sax", not "3H: Tenor Sax") beside the staves: the sheet
    // has two staves, so the names show (here only, not in the main score)
    static const QRegularExpression sectionPrefix("^[^:]+:\\s+");
    for (engraving::Part* p : dst->parts()) {
        const QString name = p->longName().toQString();
        if (sectionPrefix.match(name).hasMatch()) {
            p->setLongNameAll(muse::String::fromQString(QString(name).remove(sectionPrefix)));
        }
    }
    dst->setLayoutAll();
    dst->doLayout();
    // its systems as in the part score (line breaks, system locks, "keep together"); not its page breaks: with the
    // Voice above, a page of the part score's systems doesn't fit on a page, and each page would run onto one more
    starscore::LayoutCopyOptions options;
    options.pageBreaks = false;
    for (engraving::Measure* m = dst->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->pageBreak()) {
            m->undoSetBreak(false, engraving::LayoutBreakType::PAGE);
        }
    }
    starscore::copyLayout(src, { dst }, options);
    starscore::copyTextPositions(src, dst);
    starscore::copyMeasureWidths(src, dst);
    return true;
}

int StarScoreService::refreshVoiceScores()
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    const std::vector<VoiceScore> scores = voiceScores(master);
    if (scores.empty()) {
        return 0;
    }
    int done = 0;
    master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Format the + Voice part scores"));
    for (const VoiceScore& v : scores) {
        if (formatVoiceScore(v)) {
            ++done;
        } else {
            LOGW() << "[starscore] " << v.score->name() << ": the part score it's made from is gone";
        }
    }
    master->notation()->undoStack()->commitChanges();
    for (const VoiceScore& v : scores) {
        if (INotationPtr n = v.score->notation()) {
            n->style()->styleChanged().notify();
            n->notationChanged().notify();
        }
    }
    return done;
}

RetVal<QString> StarScoreService::addVoiceToPartScore(const QString& partScoreName)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (!master || !hasVoiceSection()) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError, muse::trc("starscore", "This song has no Voice section."));
    }
    if (!voicePartScoreChoices().contains(partScoreName)) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError,
                                         muse::qtrc("starscore", "“%1” can't get the Voice added (it has it already, or "
                                                                 "it's a score of several instruments).").arg(partScoreName).toStdString());
    }
    const QString name = partScoreName + VOICE_SUFFIX;
    INotationPtr n = makeVoiceScore(master, partScoreName, name);
    if (!n) {
        return RetVal<QString>::make_ret(Ret::Code::UnknownError,
                                         muse::qtrc("starscore", "“%1” couldn't be made.").arg(name).toStdString());
    }
    for (const VoiceScore& v : voiceScores(master)) {
        if (v.score->notation() == n) {
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Format the + Voice part score"));
            formatVoiceScore(v);
            master->notation()->undoStack()->commitChanges();
            n->style()->styleChanged().notify();
            n->notationChanged().notify();
        }
    }
    master->setExcerptIsOpen(n, true);
    globalContext()->setCurrentNotation(n);
    scheduleChanged();
    return RetVal<QString>::make_ok(name);
}

bool StarScoreService::makeLeadSheetVoiceWhenReady()
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms || m_exportSource) {
        return false;
    }
    const Data data = loadFrom(ms);
    const QString voice = voicePartIdOf(data);
    if (voice.isEmpty()) {
        return false;
    }
    bool voiceDone = false;
    QString leadPid;
    bool leadDone = false;
    for (const StarScoreSection& s : data.sections) {
        if (s.templateKey == "voice") {
            voiceDone = s.status == StarScoreStatus::Finished;
        } else if (s.templateKey == "lead-sheet" && !s.partIds.isEmpty() && leadPid.isEmpty()) {
            leadPid = s.partIds.front();
            leadDone = s.status == StarScoreStatus::Finished;
        }
    }
    if (!voiceDone || !leadDone || leadPid.isEmpty()) {
        return false;
    }
    // there already (under any name: one with the Voice over the lead part)
    for (const VoiceScore& v : voiceScores(master)) {
        if (v.sourcePartIds == QStringList { leadPid }) {
            return false;
        }
    }
    // the lead part's part score, preferably the one named like the part
    QString sourceName;
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        INotationPtr n = e->notation();
        const engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
        if (!es || e->name().endsWith(VOICE_SUFFIX)) {
            continue;
        }
        const std::vector<engraving::Part*> parts = masterPartsOf(es, ms);
        if (parts.size() == 1 && idText(parts.front()) == leadPid
            && (sourceName.isEmpty() || e->name() == parts.front()->partName().toQString())) {
            sourceName = e->name();
        }
    }
    if (sourceName.isEmpty()) {
        return false;
    }
    INotationPtr n = makeVoiceScore(master, sourceName, LEAD_SHEET_VOICE);
    if (!n) {
        return false;
    }
    for (const VoiceScore& v : voiceScores(master)) {
        if (v.score->notation() == n) {
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Format the Lead Sheet + Voice"));
            formatVoiceScore(v);
            master->notation()->undoStack()->commitChanges();
            n->style()->styleChanged().notify();
            n->notationChanged().notify();
        }
    }
    master->setExcerptIsOpen(n, true);
    scheduleChanged();
    return true;
}
