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
#include "engraving/dom/bracketItem.h"
#include "engraving/editing/editbrackets.h"
#include "engraving/editing/editpart.h"
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
    // the lyrics a word space apart, not a quarter of a space: the instrument's notes under them are spaced for that
    // (Joel, 21 Oct 2026: "Iain't'fraid ofnoghost")
    static const double LYRICS_GAP = 0.7;
    if (dst->style().styleV(engraving::Sid::lyricsMinDistance).value<engraving::Spatium>().val() < LYRICS_GAP) {
        dst->undoChangeStyleVal(engraving::Sid::lyricsMinDistance, engraving::Spatium(LYRICS_GAP));
    }
    // the Voice staff full size (Joel, 20 Oct 2026: small wasn't legible enough; it was small since 1.18.64)
    engraving::Staff* voiceStaff = nullptr;
    for (engraving::Part* p : dst->parts()) {
        for (engraving::Staff* st : p->staves()) {
            const engraving::Staff* linked = st->findLinkedInScore(dst->masterScore());
            if (linked && idText(linked->part()) == v.voicePartId) {
                voiceStaff = voiceStaff ? voiceStaff : st;
                if (st->staffType(engraving::Fraction(0, 1))->isSmall()) {
                    st->undoChangeProperty(engraving::Pid::SMALL, false);
                }
            }
        }
    }
    // The Voice and the instrument in one bracket (Joel, 20 Oct 2026). The sheet's brackets are made again: the
    // instrument's own (a lead sheet's brace) as in the part score it's made from, then the bracket outside them
    const size_t nstaves = dst->nstaves();
    for (engraving::staff_idx_t i = 0; i < nstaves; ++i) {
        engraving::Staff* st = dst->staff(i);
        const std::vector<engraving::BracketItem*> brackets = st->brackets();
        for (auto it = brackets.rbegin(); it != brackets.rend(); ++it) {
            const engraving::BracketItem* b = *it;
            if (b && b->bracketType() != engraving::BracketType::NO_BRACKET) {
                dst->undo(new engraving::RemoveBracket(st, b->column(), b->bracketType(), b->bracketSpan()));
            }
        }
    }
    size_t outer = 0;
    for (engraving::staff_idx_t i = 0; i < nstaves; ++i) {
        engraving::Staff* st = dst->staff(i);
        const engraving::Staff* from = nullptr;
        for (engraving::Staff* s2 : src->staves()) {
            if (s2->findLinkedInScore(dst) == st) {
                from = s2;
            }
        }
        if (!from) {
            continue;
        }
        for (const engraving::BracketItem* b : from->brackets()) {
            if (b && b->bracketType() != engraving::BracketType::NO_BRACKET && b->bracketType() != engraving::BracketType::NORMAL
                && b->bracketSpan() > 0) {
                dst->undoAddBracket(st, b->column(), b->bracketType(), b->bracketSpan());
                outer = std::max(outer, b->column() + 1);
            }
        }
    }
    if (nstaves > 1) {
        dst->undoAddBracket(dst->staff(0), outer, engraving::BracketType::NORMAL, nstaves);
    }
    // Rehearsal marks, tempo and system texts above the Voice, the sheet's top staff, not above the instrument
    // (Joel, 20 Oct 2026: the sheet took them from the part score's staff, under the Voice, and some didn't show)
    if (voiceStaff && voiceStaff->idx() == 0) {
        for (engraving::staff_idx_t i = 1; i < nstaves; ++i) {
            if (dst->staff(i)->isSystemObjectStaff()) {
                engraving::EditPart::moveSystemObjects(dst, dst->staff(i), voiceStaff);
            }
        }
        std::vector<engraving::EngravingItem*> below;
        auto take = [&](engraving::EngravingItem* e) {
            if (e && e->systemFlag() && e->staffIdx() > 0 && !e->isTimeSig()) {
                below.push_back(e);
            }
        };
        for (engraving::Measure* m = dst->firstMeasure(); m; m = m->nextMeasure()) {
            for (engraving::EngravingItem* e : m->el()) {
                take(e);
            }
            for (engraving::Segment* seg = m->first(); seg; seg = seg->next()) {
                for (engraving::EngravingItem* e : seg->annotations()) {
                    take(e);
                }
            }
        }
        for (const auto& [t, sp] : dst->spanner()) {
            take(sp);
        }
        for (engraving::EngravingItem* e : below) {
            e->undoChangeProperty(engraving::Pid::TRACK, engraving::staff2track(0, e->voice()));
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
    // the lyrics legible: their bars widened and systems split where the part score's layout squeezes them
    if (voiceStaff) {
        const int spaced = starscore::spaceLyrics(dst, voiceStaff->idx());
        if (spaced) {
            LOGI() << "[starscore] " << v.score->name().toStdString() << ": " << spaced << " change(s) to give the lyrics room";
        }
    }
    return true;
}

//! A "+ Voice" part score holding instruments besides the Voice that its part score doesn't (Joel, 20 Oct 2026:
//! Ghostbusters' Lead Sheet + Voice also held the Congas, so it matched no part score and was never formatted) is
//! made again from the part score named like it. Returns how many were remade.
int StarScoreService::remakeStrayVoiceScores(const IMasterNotationPtr& master)
{
    if (!master) {
        return 0;
    }
    int remade = 0;
    for (const VoiceScore& v : voiceScores(master)) {
        if (v.source) {
            continue;
        }
        const QString name = v.score->name();
        const QString base = name.left(name.size() - VOICE_SUFFIX.size());
        bool haveBase = false;
        ExcerptNotationList kept;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            haveBase |= e->name() == base;
            if (e != v.score) {
                kept.push_back(e);
            }
        }
        if (!haveBase) {
            continue;
        }
        master->setExcerpts(kept);
        INotationPtr n = makeVoiceScore(master, base, name);
        if (!n) {
            continue;
        }
        for (const VoiceScore& made : voiceScores(master)) {
            if (made.score->notation() == n) {
                master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Format the + Voice part score"));
                formatVoiceScore(made);
                master->notation()->undoStack()->commitChanges();
                n->style()->styleChanged().notify();
                n->notationChanged().notify();
            }
        }
        LOGI() << "[starscore] " << name.toStdString() << " made again from " << base.toStdString();
        ++remade;
    }
    return remade;
}

int StarScoreService::refreshVoiceScores()
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    remakeStrayVoiceScores(master);
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
    remakeStrayVoiceScores(master);
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

void StarScoreService::resetPartScore(const engraving::Score* score)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    if (!ms || !score || score->isMaster()) {
        return;
    }
    IExcerptNotationPtr excerpt;
    for (const IExcerptNotationPtr& e : master->excerpts()) {
        INotationPtr n = e ? e->notation() : nullptr;
        if (n && n->elements() && n->elements()->msScore() == score) {
            excerpt = e;
        }
    }
    if (!excerpt) {
        return;
    }
    const QString name = excerpt->name();
    constexpr int Reset = static_cast<int>(muse::IInteractive::Button::CustomButton) + 1;
    constexpr int Cancel = static_cast<int>(muse::IInteractive::Button::CustomButton) + 2;
    const muse::IInteractive::Result answer = interactive()->questionSync(
        muse::qtrc("starscore", "Reset “%1”?").arg(name).toStdString(),
        muse::qtrc("starscore", "The part score is made again from the main score, as the Parts dialog's Reset does: its "
                                "title frame comes back, and everything set in this part score only is lost (line and page "
                                "breaks, moved or hidden items, its own style changes). StarScore then gives it the "
                                "Starsign style, its sheet title and mutes again. You can undo it.").toStdString(),
        { muse::IInteractive::ButtonData(Cancel, muse::trc("starscore", "Cancel")),
          muse::IInteractive::ButtonData(Reset, muse::trc("starscore", "Reset"), true) }, Cancel);
    if (answer.button() != Reset) {
        return;
    }
    resetPartScoreNow(excerpt);
}

void StarScoreService::resetPartScoreNow(const IExcerptNotationPtr& excerpt)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = master ? master->masterScore() : nullptr;
    INotationPtr before = excerpt ? excerpt->notation() : nullptr;
    const engraving::Score* score = before && before->elements() ? before->elements()->msScore() : nullptr;
    if (!ms || !score) {
        return;
    }
    const QString name = excerpt->name();
    QStringList partIds;
    for (const engraving::Part* p : masterPartsOf(score, ms)) {
        partIds << idText(p);
    }
    master->resetExcerpt(excerpt);
    // StarScore's part of the part score: its style, its title and mutes, and a + Voice sheet's formatting
    applyStyles(partIds);
    for (const VoiceScore& v : voiceScores(master)) {
        if (v.score == excerpt) {
            master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Format the + Voice part score"));
            formatVoiceScore(v);
            master->notation()->undoStack()->commitChanges();
        }
    }
    if (INotationPtr n = excerpt->notation()) {
        n->style()->styleChanged().notify();
        n->notationChanged().notify();
    }
    scheduleChanged();
    LOGI() << "[starscore] reset " << name.toStdString();
}
