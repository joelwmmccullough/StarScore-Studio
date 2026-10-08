/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * New horn arrangement from a neighbouring one (Joel, 7 Oct 2026): when an N-horn section is made and the song has an
 * (N-1)- or (N+1)-horn section, StarScore offers to copy the parts on the same instruments into it. The parts are
 * paired instrument by instrument in score order (5H Trumpet 1 -> 6H Trumpet 1, 5H Trumpet 2 -> 6H Trumpet 2,
 * 5H Tenor Sax -> 6H Tenor Sax 1); a part with no partner stays empty. Each copy gets the music, its part score's
 * style, layout (system and page breaks), text positions and measure widths, and is marked Sketch.
 */
#include "starscoreservice.h"
#include "starscoreengraving.h"

#include <tuple>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/rw/xmlreader.h"
#include "engraving/style/style.h"

#include "notation/iexcerptnotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationstyle.h"
#include "notation/inotation.h"
#include "translation.h"

#include "log.h"

using namespace mu::project;
using namespace mu::notation;
using namespace muse;

namespace {
int hornCount(const QString& templateKey)
{
    static const QRegularExpression key("^([1-9])-horn$");
    const QRegularExpressionMatch m = key.match(templateKey);
    return m.hasMatch() ? m.captured(1).toInt() : 0;
}
}

std::vector<std::pair<QString, QString> > StarScoreService::matchingHornParts(const QString& fromSectionId,
                                                                             const QString& toSectionId) const
{
    std::vector<std::pair<QString, QString> > pairs;
    engraving::MasterScore* ms = masterScore();
    if (!ms) {
        return pairs;
    }
    QStringList fromIds, toIds;
    for (const StarScoreSection& s : load().sections) {
        if (s.id == fromSectionId) {
            fromIds = s.partIds;
        } else if (s.id == toSectionId) {
            toIds = s.partIds;
        }
    }
    // each section's parts by instrument, in score order
    auto byInstrument = [&](const QStringList& ids) {
        std::map<QString, QStringList> out;
        for (const engraving::Part* p : ms->parts()) {
            if (ids.contains(idText(p))) {
                out[p->instrument()->id().toQString()] << idText(p);
            }
        }
        return out;
    };
    const auto from = byInstrument(fromIds);
    const auto to = byInstrument(toIds);
    // pairs in the new section's score order
    for (const engraving::Part* p : ms->parts()) {
        const QString pid = idText(p);
        if (!toIds.contains(pid)) {
            continue;
        }
        const QString inst = p->instrument()->id().toQString();
        auto f = from.find(inst);
        auto t = to.find(inst);
        if (f == from.end() || t == to.end()) {
            continue;
        }
        const int k = t->second.indexOf(pid);
        if (k >= 0 && k < f->second.size()) {
            pairs.emplace_back(f->second.at(k), pid);
        }
    }
    return pairs;
}

//! Whether any part of the section has notes
bool StarScoreService::sectionHasMusic(const StarScoreSection& section) const
{
    engraving::MasterScore* ms = masterScore();
    for (const QString& pid : section.partIds) {
        const engraving::Part* p = ms ? ms->partById(ID(pid)) : nullptr;
        if (p && starscore::partHasNotes(p)) {
            return true;
        }
    }
    return false;
}

void StarScoreService::offerMatchingHornParts(const QStringList& sectionIdsBefore)
{
    // after the dialog that made the section has closed
    QTimer::singleShot(300, &m_timerGuard, [this, sectionIdsBefore]() {
        IMasterNotationPtr master = globalContext()->currentMasterNotation();
        if (!master) {
            return;
        }
        const std::vector<StarScoreSection> all = sections();
        for (const StarScoreSection& made : all) {
            const int n = hornCount(made.templateKey);
            if (sectionIdsBefore.contains(made.id) || n == 0) {
                continue;
            }
            // the neighbour: one with music first, then the most finished, then the smaller
            const StarScoreSection* from = nullptr;
            auto rank = [&](const StarScoreSection& s) {
                return std::make_tuple(sectionHasMusic(s) ? 1 : 0, int(s.status), -hornCount(s.templateKey));
            };
            for (const StarScoreSection& s : all) {
                const int k = hornCount(s.templateKey);
                if (!sectionIdsBefore.contains(s.id) || (k != n - 1 && k != n + 1)) {
                    continue;
                }
                if (!from || rank(s) > rank(*from)) {
                    from = &s;
                }
            }
            if (!from) {
                continue;
            }
            // only parts with music to copy; nothing asked when the neighbouring section is empty (Joel, 8 Oct 2026)
            std::vector<std::pair<QString, QString> > pairs;
            engraving::MasterScore* ms = masterScore();
            for (const auto& pair : matchingHornParts(from->id, made.id)) {
                const engraving::Part* p = ms ? ms->partById(ID(pair.first)) : nullptr;
                if (p && starscore::partHasNotes(p)) {
                    pairs.push_back(pair);
                }
            }
            if (pairs.empty()) {
                continue;
            }
            QStringList lines;
            for (const auto& [src, dst] : pairs) {
                lines << partScoreName(src) + "  →  " + partScoreName(dst);
            }
            constexpr int Copy = static_cast<int>(muse::IInteractive::Button::CustomButton) + 1;
            constexpr int No = static_cast<int>(muse::IInteractive::Button::CustomButton) + 2;
            const muse::IInteractive::Result answer = interactive()->questionSync(
                muse::qtrc("starscore", "Copy the parts from the %1?").arg(from->name).toStdString(),
                (muse::qtrc("starscore", "These parts are on the same instruments:") + "\n\n" + lines.join("\n") + "\n\n"
                 + muse::qtrc("starscore", "Each copy gets the music and its part score's formatting and text positions, "
                                           "and is marked Sketch.")).toStdString(),
                { muse::IInteractive::ButtonData(No, muse::trc("starscore", "Don't copy")),
                  muse::IInteractive::ButtonData(Copy, muse::trc("starscore", "Copy the parts"), true) }, Copy);
            if (answer.button() == Copy) {
                copyHornParts(pairs);
            }
        }
    });
}

QString StarScoreService::partScoreName(const QString& partId) const
{
    engraving::MasterScore* ms = masterScore();
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    if (ms && master) {
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            INotationPtr n = e ? e->notation() : nullptr;
            engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
            if (!es || es->parts().size() != 1) {
                continue;
            }
            const std::vector<engraving::Part*> ps = masterPartsOf(es, ms);
            if (ps.size() == 1 && idText(ps.front()) == partId) {
                return e->name();
            }
        }
    }
    const engraving::Part* p = ms ? ms->partById(ID(partId)) : nullptr;
    return p ? p->partName().toQString() : partId;
}

int StarScoreService::copyHornParts(const std::vector<std::pair<QString, QString> >& pairs)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms || !ms->firstMeasure()) {
        return 0;
    }
    Data data = load();

    // the music: each source staff pasted over its partner, as one undo step; multimeasure rests off meanwhile
    // (selecting bars snaps to them)
    const bool mmRests = ms->style().styleB(engraving::Sid::createMultiMeasureRests);
    master->notation()->undoStack()->prepareChanges(TranslatableString("starscore", "Copy parts into the new arrangement"));
    if (mmRests) {
        ms->undoChangeStyleVal(engraving::Sid::createMultiMeasureRests, false);
    }
    ms->setLayoutAll();
    ms->doLayout();
    const engraving::Fraction end = ms->lastMeasure()->endTick();
    int copied = 0;
    for (const auto& [srcId, dstId] : pairs) {
        engraving::Part* from = ms->partById(ID(srcId));
        engraving::Part* to = ms->partById(ID(dstId));
        if (!from || !to || from->staves().empty() || to->staves().empty()) {
            continue;
        }
        bool ok = true;
        for (size_t k = 0; k < from->staves().size() && k < to->staves().size(); ++k) {
            const engraving::staff_idx_t src = from->staves().at(k)->idx();
            const engraving::staff_idx_t dst = to->staves().at(k)->idx();
            ms->deselectAll();
            ms->selection().setRangeTicks(engraving::Fraction(0, 1), end, src, src + 1);
            ms->selection().updateSelectedElements();
            const ByteArray mime = ms->selection().mimeData();
            ms->deselectAll();
            if (mime.empty()) {
                ok = false;
                continue;
            }
            engraving::XmlReader reader(mime);
            if (!ms->pasteStaff(reader, ms->firstMeasure()->first(engraving::SegmentType::ChordRest), dst)) {
                ok = false;
            }
            ms->deselectAll();
        }
        if (ok) {
            data.partStatus[dstId] = statusKey(StarScoreStatus::Sketch);
            ++copied;
        } else {
            LOGW() << "[starscore] copy parts: " << partScoreName(srcId).toStdString() << " not fully copied";
        }
    }
    if (mmRests) {
        ms->undoChangeStyleVal(engraving::Sid::createMultiMeasureRests, true);
    }
    master->notation()->undoStack()->commitChanges();
    master->notation()->notationChanged().notify();
    store(data);

    // the part scores: style, breaks, text positions and measure widths from the source's part score
    auto bookOf = [&](const QString& partId) -> INotationPtr {
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            INotationPtr n = e ? e->notation() : nullptr;
            engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
            if (!es || es->parts().size() != 1) {
                continue;
            }
            const std::vector<engraving::Part*> ps = masterPartsOf(es, ms);
            if (ps.size() == 1 && idText(ps.front()) == partId) {
                return n;
            }
        }
        return nullptr;
    };
    const QString mss = QDir::tempPath() + "/starscore-copy-" + QUuid::createUuid().toString(QUuid::Id128) + ".mss";
    for (const auto& [srcId, dstId] : pairs) {
        INotationPtr srcBook = bookOf(srcId);
        INotationPtr dstBook = bookOf(dstId);
        if (!srcBook || !dstBook) {
            continue;
        }
        if (srcBook->style()->saveStyle(io::path_t(mss))) {
            dstBook->style()->loadStyle(io::path_t(mss), true);
        }
        engraving::Score* srcScore = srcBook->elements()->msScore();
        engraving::Score* dstScore = dstBook->elements()->msScore();
        if (srcScore && dstScore) {
            dstBook->undoStack()->prepareChanges(TranslatableString("starscore", "Formatting from the copied part"));
            starscore::copyLayout(srcScore, { dstScore }, starscore::LayoutCopyOptions());
            starscore::copyTextPositions(srcScore, dstScore);
            starscore::copyMeasureWidths(srcScore, dstScore);
            dstBook->undoStack()->commitChanges();
            dstBook->notationChanged().notify();
        }
    }
    QFile::remove(mss);
    scheduleChanged();
    LOGI() << "[starscore] copy parts: " << copied << " of " << pairs.size() << " copied";
    return copied;
}
