/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — standardizing an older song (Joel, 6 Oct 2026: "I want ALL .starscore files to follow the
 * standard formatting. Standard sections, standard arrangements, standard everything.")
 *
 *   - "2-Horn / 3-Horn Any (all keys)" and "… Flexible (all keys)" sections, which held one part per key, become the
 *     standard 2-Horn / 3-Horn Flexible sections: one concert-pitch staff per chair, its music (and its part score's
 *     layout and status) from the old "Horn k in C" part, or the "Horn k in Bass Clef" part for a trombone chair
 *   - "Horn Section (full score)" and "Player Lead Sheets" are removed with their instruments
 *   - "Strings & Extras": its strings become a String Duo / Trio / Quartet / Quintet section ("Duo: Violin", …), the
 *     rest of its instruments are removed
 *   - a custom "N-Horn Section" takes the N-Horn template; the leftover hidden "Horn 1 (Flute)" chair of a 3-Horn
 *     Flexible section goes (its Flute sheet is made from Horn 1 since 1.18.2)
 *   - sections and arrangements made from a template take the template's name
 *   - arrangements not made from a template take the template whose sections they have, or are removed; a horn
 *     section without its standard arrangement gets one
 *   - part scores: the arrangements' scores and one part score per instrument stay; scores of several instruments,
 *     empty ones and second part scores of the same instrument are removed
 *
 * standardizeSong(false) only lists what it would do; standardizeSong(true) does it. Lines starting with "??" are
 * things left as they are because no rule fits.
 */
#include "starscoreservice.h"
#include "starscoreengraving.h"

#include <algorithm>
#include <map>
#include <optional>
#include <set>

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QUuid>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/rw/xmlreader.h"
#include "engraving/editing/editpart.h"
#include "engraving/style/style.h"

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
//! "violin", "viola", "cello", "bass" for a string instrument, else ""
QString stringKind(const mu::engraving::Part* p)
{
    const QString id = p->instrumentId().toQString().toLower();
    const QString name = p->partName().toQString().toLower();
    if (id.contains("clarinet") || id.contains("electric") || id.contains("guitar") || name.contains("clarinet")) {
        return {};
    }
    if (id.contains("violin") || name.startsWith("violin")) {
        return "violin";
    }
    if (id.contains("viola") || name.startsWith("viola")) {
        return "viola";
    }
    if (id.contains("cello") || name.contains("cello")) {
        return "cello";
    }
    if (id == "contrabass" || id.contains("double-bass") || name.startsWith("contrabass") || name.startsWith("double bass")) {
        return "bass";
    }
    return {};
}

QString stringKindOfTemplateInstrument(const QString& instrumentId)
{
    if (instrumentId == "violin") {
        return "violin";
    }
    if (instrumentId == "viola") {
        return "viola";
    }
    if (instrumentId == "violoncello") {
        return "cello";
    }
    return "bass";
}

QString bare(const QString& name)
{
    const int at = name.lastIndexOf(": ");
    return (at >= 0 ? name.mid(at + 2) : name).trimmed();
}
}

QStringList StarScoreService::standardizeSong(bool apply)
{
    QStringList report;
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    engraving::MasterScore* ms = masterScore();
    if (!master || !ms) {
        report << "?? no score open";
        return report;
    }
    Data data = load();
    if (data.sections.empty()) {
        report << "?? not a StarScore song (no sections)";
        return report;
    }

    const std::vector<StarScoreSectionTemplate> sectionTpls = sectionTemplates();
    const std::vector<StarScoreArrangementTemplate> arrangementTpls = arrangementTemplates();
    auto sectionTpl = [&](const QString& key) -> const StarScoreSectionTemplate* {
        for (const StarScoreSectionTemplate& t : sectionTpls) {
            if (t.key == key) {
                return &t;
            }
        }
        return nullptr;
    };
    auto partName = [&](const QString& pid) {
        const engraving::Part* p = ms->partById(ID(pid));
        return p ? p->partName().toQString() : QString("(missing part)");
    };
    auto namesOf = [&](const QStringList& pids) {
        QStringList out;
        for (const QString& pid : pids) {
            out << partName(pid);
        }
        return out.join(", ");
    };
    auto inScoreOrder = [&](const QStringList& pids) {
        QStringList out;
        for (const engraving::Part* p : ms->parts()) {
            if (pids.contains(idText(p))) {
                out << idText(p);
            }
        }
        return out;
    };

    // Part scores: the engraving excerpts and the notation's list are in the same order
    ExcerptNotationList books = master->excerpts();
    std::vector<std::vector<engraving::Part*> > bookParts;
    {
        const std::vector<engraving::Excerpt*>& exs = ms->excerpts();
        for (size_t i = 0; i < books.size(); ++i) {
            engraving::Excerpt* ex = i < exs.size() && exs[i]->name() == books[i]->name() ? exs[i] : nullptr;
            if (!ex) {
                for (engraving::Excerpt* e : exs) {
                    if (e->name() == books[i]->name()) {
                        ex = e;
                        break;
                    }
                }
            }
            bookParts.push_back(ex ? masterPartsOf(ex) : std::vector<engraving::Part*> {});
        }
    }
    auto bookNamed = [&](const QString& name) -> int {
        for (size_t i = 0; i < books.size(); ++i) {
            if (books[i]->name() == name) {
                return int(i);
            }
        }
        return -1;
    };

    // --- what changes ---------------------------------------------------------------------------------------------
    struct FlexConversion {
        QString legacyId;
        int horns = 0;
        std::vector<engraving::Part*> sources;   // per chair
        std::vector<int> sourceBooks;            // per chair, or -1
    };
    std::vector<FlexConversion> conversions;
    std::set<QString> removedSections;
    QStringList doomedParts;
    struct StringSection {
        QString key;
        QStringList partIds;     // template order
        QStringList newNames;
    };
    std::optional<StringSection> strings;
    std::map<QString, QString> newTemplateKey;   // section id -> key (custom N-Horn Section)

    static const QRegularExpression legacyFlexRe("^([23])-Horn (Any|Flexible) \\(all keys\\)$");
    static const QRegularExpression customHornRe("^([1-7])-Horn Section$");

    for (const StarScoreSection& s : data.sections) {
        const bool known = sectionTpl(s.templateKey) != nullptr;
        if (known) {
            continue;
        }
        const QRegularExpressionMatch flex = legacyFlexRe.match(s.name);
        const QRegularExpressionMatch hornSection = customHornRe.match(s.name);
        if (flex.hasMatch()) {
            const int n = flex.captured(1).toInt();
            const QString key = QString("%1-horn-any").arg(n);
            bool already = false;
            for (const StarScoreSection& o : data.sections) {
                already = already || o.templateKey == key;
            }
            if (already) {
                report << QString("?? \"%1\" kept: the song already has a standard %2-Horn Flexible section").arg(s.name).arg(n);
                continue;
            }
            const StarScoreSectionTemplate* t = sectionTpl(key);
            FlexConversion c;
            c.legacyId = s.id;
            c.horns = n;
            QStringList from;
            bool ok = true;
            for (int k = 1; k <= n; ++k) {
                const bool trombone = t && size_t(k - 1) < t->instruments.size() && t->instruments[k - 1].instrumentId == "trombone";
                int idx = -1;
                if (trombone) {
                    idx = bookNamed(QString("%1-Horn Arr: Horn %2 in Bass Clef").arg(n).arg(k));
                }
                if (idx < 0 || bookParts[idx].size() != 1) {
                    idx = bookNamed(QString("%1-Horn Arr: Horn %2 in C").arg(n).arg(k));
                }
                if (idx < 0 || bookParts[idx].size() != 1 || !s.partIds.contains(idText(bookParts[idx].front()))) {
                    ok = false;
                    break;
                }
                c.sources.push_back(bookParts[idx].front());
                c.sourceBooks.push_back(idx);
                from << QString("Horn %1 from \"%2\"").arg(k).arg(books[idx]->name());
            }
            if (!ok) {
                report << QString("?? \"%1\" kept: its \"%2-Horn Arr: Horn k in C\" part scores aren't all there").arg(s.name).arg(n);
                continue;
            }
            conversions.push_back(c);
            removedSections.insert(s.id);
            report << QString("\"%1\" -> standard %2-Horn Flexible section (%3); its %4 key-version parts removed")
                .arg(s.name).arg(n).arg(from.join(", ")).arg(s.partIds.size());
        } else if (s.name == "Horn Section (full score)" || s.name == "Player Lead Sheets") {
            removedSections.insert(s.id);
            report << QString("\"%1\" removed, with its instruments: %2").arg(s.name, namesOf(s.partIds));
        } else if (s.name == "Strings & Extras") {
            std::map<QString, QStringList> kinds;   // kind -> part ids, score order
            QStringList extras;
            for (const QString& pid : inScoreOrder(s.partIds)) {
                const engraving::Part* p = ms->partById(ID(pid));
                const QString kind = p ? stringKind(p) : QString();
                if (kind.isEmpty()) {
                    extras << pid;
                } else {
                    kinds[kind] << pid;
                }
            }
            const int vn = kinds["violin"].size(), va = kinds["viola"].size(), vc = kinds["cello"].size(), cb = kinds["bass"].size();
            QString key;
            if (vn == 1 && va == 0 && vc == 1 && cb == 0) {
                key = "string-duo";
            } else if (vn == 1 && va == 1 && vc == 1 && cb == 0) {
                key = "string-trio";
            } else if (vn == 1 && va == 1 && vc == 1 && cb == 1) {
                key = "string-quartet";
            } else if (vn == 2 && va == 1 && vc == 1 && cb == 1) {
                key = "string-quintet";
            }
            bool hasStrings = false;
            for (const StarScoreSection& o : data.sections) {
                hasStrings = hasStrings || o.templateKey.startsWith("string-");
            }
            const int stringCount = vn + va + vc + cb;
            if (stringCount > 0 && (key.isEmpty() || hasStrings)) {
                report << QString("?? \"%1\" kept: %2").arg(s.name, hasStrings ? QString("the song already has a string section")
                                                         : QString("no standard string section has these strings (%1)").arg(namesOf(s.partIds)));
                continue;
            }
            if (!key.isEmpty()) {
                const StarScoreSectionTemplate* t = sectionTpl(key);
                StringSection ss;
                ss.key = key;
                std::map<QString, int> used;
                for (const StarScoreInstrument& inst : t->instruments) {
                    const QString kind = stringKindOfTemplateInstrument(inst.instrumentId);
                    const int i = used[kind]++;
                    ss.partIds << kinds[kind].value(i);
                    ss.newNames << inst.partName;
                }
                QStringList moves;
                for (int i = 0; i < ss.partIds.size(); ++i) {
                    moves << QString("%1 -> \"%2\"").arg(partName(ss.partIds[i]), ss.newNames[i]);
                }
                report << QString("\"%1\": strings -> %2 section (%3)").arg(s.name, t->name, moves.join(", "));
                strings = ss;
            }
            if (!extras.isEmpty()) {
                report << QString("\"%1\": removed, with their music: %2").arg(s.name, namesOf(extras));
            }
            removedSections.insert(s.id);
        } else if (hornSection.hasMatch()) {
            const QString key = QString("%1-horn").arg(hornSection.captured(1));
            newTemplateKey[s.id] = key;
            report << QString("\"%1\" (made by hand) -> the standard %2 section").arg(s.name, sectionTpl(key)->name);
        } else {
            report << QString("?? section \"%1\" (%2) kept: no rule for it [%3]").arg(s.name, s.templateKey, namesOf(s.partIds));
        }
    }

    // parts of the removed sections that no kept section has
    std::set<QString> keptParts;
    for (const StarScoreSection& s : data.sections) {
        if (!removedSections.count(s.id)) {
            for (const QString& pid : s.partIds) {
                keptParts.insert(pid);
            }
            for (const auto& [alt, main] : s.alternates) {
                keptParts.insert(alt);
            }
        }
    }
    if (strings) {
        for (const QString& pid : strings->partIds) {
            keptParts.insert(pid);
        }
    }
    for (const StarScoreSection& s : data.sections) {
        if (!removedSections.count(s.id)) {
            continue;
        }
        for (const QString& pid : s.partIds) {
            if (!keptParts.count(pid) && !doomedParts.contains(pid)) {
                doomedParts << pid;
            }
        }
    }

    // the old hidden "Horn 1 (Flute)" chair of a 3-Horn Flexible section
    std::map<QString, QStringList> flexibleLeftovers;   // section id -> part ids
    for (const StarScoreSection& s : data.sections) {
        if (s.templateKey != "3-horn-any") {
            continue;
        }
        for (const QString& pid : s.partIds) {
            const engraving::Part* p = ms->partById(ID(pid));
            if (!p || bare(p->partName().toQString()) != "Horn 1 (Flute)" || s.alternates.count(pid)) {
                continue;
            }
            bool sheet = false;
            for (const auto& [name, sp] : s.sheetParts) {
                sheet = sheet || sp == pid;
            }
            if (sheet) {
                continue;
            }
            flexibleLeftovers[s.id] << pid;
            doomedParts << pid;
            keptParts.erase(pid);
            report << QString("\"%1\": the old \"%2\" chair removed (the Flute sheet is made from Horn 1)").arg(s.name, p->partName().toQString());
        }
    }

    // section names
    std::map<QString, QString> sectionRenames;
    {
        std::map<QString, int> perTemplate;
        for (const StarScoreSection& s : data.sections) {
            if (!removedSections.count(s.id)) {
                perTemplate[newTemplateKey.count(s.id) ? newTemplateKey[s.id] : s.templateKey]++;
            }
        }
        for (const StarScoreSection& s : data.sections) {
            const QString key = newTemplateKey.count(s.id) ? newTemplateKey[s.id] : s.templateKey;
            const StarScoreSectionTemplate* t = sectionTpl(key);
            if (removedSections.count(s.id) || !t || perTemplate[key] != 1 || s.name == t->name) {
                continue;
            }
            sectionRenames[s.id] = t->name;
            report << QString("section \"%1\" renamed \"%2\"").arg(s.name, t->name);
        }
    }

    // arrangements
    auto keyOfSection = [&](const QString& id) -> QString {
        if (newTemplateKey.count(id)) {
            return newTemplateKey[id];
        }
        for (const StarScoreSection& s : data.sections) {
            if (s.id == id) {
                return s.templateKey;
            }
        }
        return {};
    };
    std::set<QString> removedArrangements;
    std::map<QString, std::pair<QString, QString> > adoptedArrangements;   // id -> (template key, name)
    std::map<QString, QString> arrangementRenames;
    std::set<QString> arrangementTemplatesHeld;
    for (const StarScoreArrangement& a : data.arrangements) {
        QStringList keys;
        for (const QString& sid : a.sectionIds) {
            if (!removedSections.count(sid)) {
                keys << keyOfSection(sid);
            }
        }
        if (!a.templateKey.isEmpty()) {
            arrangementTemplatesHeld.insert(a.templateKey);
            continue;
        }
        const StarScoreArrangementTemplate* match = nullptr;
        for (const StarScoreArrangementTemplate& t : arrangementTpls) {
            QStringList want = t.sectionKeys;
            QStringList have = keys;
            want.sort();
            have.sort();
            if (want == have) {
                match = &t;
            }
        }
        if (match && !arrangementTemplatesHeld.count(match->key)) {
            bool heldLater = false;
            for (const StarScoreArrangement& o : data.arrangements) {
                heldLater = heldLater || o.templateKey == match->key;
            }
            if (!heldLater) {
                adoptedArrangements[a.id] = { match->key, match->name };
                arrangementTemplatesHeld.insert(match->key);
                report << QString("arrangement \"%1\" -> the standard \"%2\" arrangement").arg(a.name, match->name);
                continue;
            }
        }
        removedArrangements.insert(a.id);
        report << QString("arrangement \"%1\" removed (with its score)").arg(a.name);
    }
    {
        std::map<QString, int> perTemplate;
        for (const StarScoreArrangement& a : data.arrangements) {
            perTemplate[a.templateKey]++;
        }
        for (const StarScoreArrangement& a : data.arrangements) {
            if (a.templateKey.isEmpty() || perTemplate[a.templateKey] != 1) {
                continue;
            }
            for (const StarScoreArrangementTemplate& t : arrangementTpls) {
                if (t.key == a.templateKey && a.name != t.name) {
                    arrangementRenames[a.id] = t.name;
                    report << QString("arrangement \"%1\" renamed \"%2\"").arg(a.name, t.name);
                }
            }
        }
    }
    // horn sections without their standard arrangement
    QStringList missingArrangements;
    {
        std::set<QString> keysAfter;
        for (const StarScoreSection& s : data.sections) {
            if (!removedSections.count(s.id)) {
                keysAfter.insert(keyOfSection(s.id));
            }
        }
        for (const FlexConversion& c : conversions) {
            keysAfter.insert(QString("%1-horn-any").arg(c.horns));
        }
        static const QRegularExpression hornKeyRe("^([1-7])-horn(-any)?$");
        const std::set<QString> keysNow = keysAfter;
        for (const QString& key : keysNow) {
            const QRegularExpressionMatch m = hornKeyRe.match(key);
            if (!m.hasMatch()) {
                continue;
            }
            const QString arrKey = m.captured(2).isEmpty() ? QString("%1-horn-standard").arg(m.captured(1)) : key;
            if (arrangementTemplatesHeld.count(arrKey)) {
                continue;
            }
            QStringList made;   // sections the arrangement needs that the song doesn't have yet (made empty)
            if (!keysAfter.count("lead-sheet")) {
                made << "Lead Sheet";
            }
            if (!keysAfter.count("rhythm")) {
                made << "Rhythm Section";
            }
            missingArrangements << arrKey;
            for (const StarScoreArrangementTemplate& t : arrangementTpls) {
                if (t.key == arrKey) {
                    report << QString("arrangement \"%1\" added%2").arg(t.name, made.isEmpty() ? QString()
                                                                         : QString(", with a new empty %1").arg(made.join(" and ")));
                }
            }
            keysAfter.insert("lead-sheet");
            keysAfter.insert("rhythm");
        }
    }

    // part scores
    std::set<QString> keptScoreNames;
    for (const StarScoreArrangement& a : data.arrangements) {
        if (!removedArrangements.count(a.id) && !a.scoreName.isEmpty()) {
            keptScoreNames.insert(a.scoreName);
        }
    }
    std::set<int> doomedBooks;
    std::map<QString, int> bookForPart;   // surviving part -> its part score
    for (size_t i = 0; i < books.size(); ++i) {
        if (keptScoreNames.count(books[i]->name()) || bookParts[i].size() != 1) {
            continue;
        }
        const engraving::Part* p = bookParts[i].front();
        const QString pid = idText(p);
        if (doomedParts.contains(pid)) {
            continue;
        }
        auto it = bookForPart.find(pid);
        if (it == bookForPart.end() || (books[i]->name() == p->partName().toQString() && books[it->second]->name() != p->partName().toQString())) {
            bookForPart[pid] = int(i);
        }
    }
    QStringList removedBookNames;
    int removedWithInstruments = 0;
    for (size_t i = 0; i < books.size(); ++i) {
        const QString name = books[i]->name();
        if (keptScoreNames.count(name)) {
            continue;
        }
        const std::vector<engraving::Part*>& ps = bookParts[i];
        if (ps.size() == 1) {
            const QString pid = idText(ps.front());
            if (doomedParts.contains(pid)) {
                doomedBooks.insert(int(i));
                ++removedWithInstruments;
                continue;
            }
            if (bookForPart[pid] == int(i)) {
                continue;
            }
            doomedBooks.insert(int(i));
            removedBookNames << QString("\"%1\" (a second part score of %2)").arg(name, ps.front()->partName().toQString());
            continue;
        }
        doomedBooks.insert(int(i));
        bool arrangementScore = false;
        for (const StarScoreArrangement& a : data.arrangements) {
            arrangementScore = arrangementScore || a.scoreName == name;
        }
        if (arrangementScore) {
            continue;   // listed with its arrangement
        }
        QStringList pn;
        for (const engraving::Part* p : ps) {
            pn << p->partName().toQString();
        }
        removedBookNames << (ps.empty() ? QString("\"%1\" (empty)").arg(name)
                             : QString("\"%1\" (%2)").arg(name, pn.join(", ")));
    }
    if (!removedBookNames.isEmpty()) {
        report << "part scores removed: " + removedBookNames.join("; ");
    }
    if (removedWithInstruments > 0) {
        report << QString("%1 part score(s) of the removed instruments removed with them").arg(removedWithInstruments);
    }

    // instruments in no section
    const std::set<QString>& inSections = keptParts;
    QStringList loose;
    for (const engraving::Part* p : ms->parts()) {
        const QString pid = idText(p);
        if (!inSections.count(pid) && !doomedParts.contains(pid)) {
            loose << p->partName().toQString();
        }
    }
    if (!loose.isEmpty()) {
        report << "?? instruments in no section, kept: " + loose.join(", ");
    }

    if (!apply || report.isEmpty()) {
        return report;
    }
    bool onlyNotes = std::all_of(report.begin(), report.end(), [](const QString& l) { return l.startsWith("??"); });
    if (onlyNotes) {
        return report;
    }

    // --- doing it ---------------------------------------------------------------------------------------------------
    const QString wasActive = activeArrangementId();
    std::set<QString> wasOn;
    for (const StarScoreSection& s : sections()) {
        if (s.on) {
            wasOn.insert(s.id);
        }
    }

    LOGI() << "[starscore] standardize: 1. the standard Flexible sections";
    // 1. the standard Flexible sections, with the music of the old "in C" parts
    for (const FlexConversion& c : conversions) {
        LOGI() << "[starscore] standardize: making the " << c.horns << "-Horn Flexible section";
        const RetVal<QString> made = createSectionFromTemplate(QString("%1-horn-any").arg(c.horns));
        if (!made.ret) {
            report << QString("?? could not make the %1-Horn Flexible section").arg(c.horns);
            continue;
        }
        Data now = load();
        QStringList chairs;
        for (const StarScoreSection& s : now.sections) {
            if (s.id == made.val) {
                chairs = inScoreOrder(s.partIds);
            }
        }
        // multimeasure rests off while copying (selecting bars snaps to them)
        const bool mmRests = ms->style().styleB(engraving::Sid::createMultiMeasureRests);
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("Flexible chairs from the old parts"));
        if (mmRests) {
            ms->undoChangeStyleVal(engraving::Sid::createMultiMeasureRests, false);
        }
        ms->setLayoutAll();
        ms->doLayout();
        const engraving::Fraction end = ms->lastMeasure()->endTick();
        for (size_t k = 0; k < c.sources.size() && int(k) < chairs.size(); ++k) {
            engraving::Part* to = ms->partById(ID(chairs[int(k)]));
            engraving::Part* from = c.sources[k];
            if (!to || !from || to->staves().empty() || from->staves().empty()) {
                continue;
            }
            const engraving::staff_idx_t src = from->staves().front()->idx();
            const engraving::staff_idx_t dst = to->staves().front()->idx();
            ms->deselectAll();
            ms->selection().setRangeTicks(engraving::Fraction(0, 1), end, src, src + 1);
            ms->selection().updateSelectedElements();
            const ByteArray mime = ms->selection().mimeData();
            ms->deselectAll();
            if (mime.empty()) {
                report << QString("?? nothing copied into Horn %1").arg(k + 1);
                continue;
            }
            engraving::XmlReader reader(mime);
            engraving::Segment* start = ms->firstMeasure()->first(engraving::SegmentType::ChordRest);
            if (!ms->pasteStaff(reader, start, dst)) {
                report << QString("?? Horn %1's music could not be pasted").arg(k + 1);
            }
            ms->deselectAll();
            // the old part's status
            auto st = now.partStatus.find(idText(from));
            if (st != now.partStatus.end()) {
                now.partStatus[chairs[int(k)]] = st->second;
            }
        }
        if (mmRests) {
            ms->undoChangeStyleVal(engraving::Sid::createMultiMeasureRests, true);
        }
        master->notation()->undoStack()->commitChanges();
        master->notation()->notationChanged().notify();
        store(now);

        LOGI() << "[starscore] standardize: music copied, now the part score layouts";
        // each chair's part score takes the layout of the old part's score
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
        const QString mss = QDir::tempPath() + "/starscore-std-" + QUuid::createUuid().toString(QUuid::Id128) + ".mss";
        for (size_t k = 0; k < c.sources.size() && int(k) < chairs.size(); ++k) {
            INotationPtr srcBook = c.sourceBooks[k] >= 0 ? books[c.sourceBooks[k]]->notation() : nullptr;
            INotationPtr dstBook = bookOf(ms->partById(ID(chairs[int(k)])));
            if (!srcBook || !dstBook) {
                continue;
            }
            if (srcBook->style()->saveStyle(io::path_t(mss))) {
                dstBook->style()->loadStyle(io::path_t(mss), true);
            }
            engraving::Score* srcScore = srcBook->elements()->msScore();
            engraving::Score* dstScore = dstBook->elements()->msScore();
            if (srcScore && dstScore) {
                dstBook->undoStack()->prepareChanges(TranslatableString::untranslatable("Layout from the old part"));
                starscore::copyLayout(srcScore, { dstScore }, starscore::LayoutCopyOptions());
                starscore::copyTextPositions(srcScore, dstScore);
                starscore::copyMeasureWidths(srcScore, dstScore);
                dstBook->undoStack()->commitChanges();
                dstBook->notationChanged().notify();
            }
        }
        QFile::remove(mss);
        for (const StarScoreSection& s : load().sections) {
            if (s.id == made.val) {
                applyFlexibleView(s);
            }
        }
    }

    LOGI() << "[starscore] standardize: 2. the sections and arrangements";
    // 2. the sections and arrangements
    Data d = load();
    for (StarScoreSection& s : d.sections) {
        if (newTemplateKey.count(s.id)) {
            s.templateKey = newTemplateKey[s.id];
        }
        if (sectionRenames.count(s.id)) {
            s.name = sectionRenames[s.id];
        }
        if (flexibleLeftovers.count(s.id)) {
            for (const QString& pid : flexibleLeftovers[s.id]) {
                s.partIds.removeAll(pid);
                s.shownPartIds.removeAll(pid);
            }
        }
    }
    if (strings) {
        QStringList taken;
        for (const StarScoreSection& s : d.sections) {
            taken << s.id;
        }
        StarScoreSection ss;
        ss.id = uniqueId(taken, strings->key);
        ss.templateKey = strings->key;
        ss.name = sectionTpl(strings->key)->name;
        ss.partIds = strings->partIds;
        // after the section it came from
        auto at = std::find_if(d.sections.begin(), d.sections.end(), [&](const StarScoreSection& s) { return s.name == "Strings & Extras"; });
        d.sections.insert(at, ss);
    }
    d.sections.erase(std::remove_if(d.sections.begin(), d.sections.end(),
                                    [&](const StarScoreSection& s) { return removedSections.count(s.id) > 0; }), d.sections.end());
    for (StarScoreArrangement& a : d.arrangements) {
        QStringList kept;
        for (const QString& sid : a.sectionIds) {
            if (!removedSections.count(sid)) {
                kept << sid;
            }
        }
        a.sectionIds = kept;
        if (adoptedArrangements.count(a.id)) {
            a.templateKey = adoptedArrangements[a.id].first;
            a.name = adoptedArrangements[a.id].second;
        }
        if (arrangementRenames.count(a.id)) {
            a.name = arrangementRenames[a.id];
        }
    }
    d.arrangements.erase(std::remove_if(d.arrangements.begin(), d.arrangements.end(),
                                        [&](const StarScoreArrangement& a) { return removedArrangements.count(a.id) > 0; }),
                         d.arrangements.end());
    for (const QString& pid : doomedParts) {
        d.partStatus.erase(pid);
    }
    store(d);

    LOGI() << "[starscore] standardize: 3. the string parts";
    // 3. the string parts' names (and their part scores')
    if (strings) {
        std::vector<std::pair<engraving::Part*, QString> > renames;
        for (int i = 0; i < strings->partIds.size(); ++i) {
            if (engraving::Part* p = ms->partById(ID(strings->partIds[i]))) {
                renames.emplace_back(p, strings->newNames[i]);
            }
        }
        master->notation()->undoStack()->prepareChanges(TranslatableString::untranslatable("String section names"));
        for (const auto& [p, name] : renames) {
            engraving::EditPart::setInstrumentName(ms, p, engraving::Fraction(0, 1), String::fromQString(name));
            p->setPartName(String::fromQString(name));
        }
        master->notation()->undoStack()->commitChanges();
        for (const auto& [p, name] : renames) {
            auto it = bookForPart.find(idText(p));
            if (it != bookForPart.end()) {
                books[it->second]->setName(name);
            }
        }
    }

    LOGI() << "[starscore] standardize: 4. the part scores that go";
    // 4. the part scores that go
    {
        ExcerptNotationList kept;
        std::set<const IExcerptNotation*> doomed;
        for (int i : doomedBooks) {
            doomed.insert(books[i].get());
        }
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            if (!doomed.count(e.get())) {
                kept.push_back(e);
            }
        }
        if (kept.size() != master->excerpts().size()) {
            master->setExcerpts(kept);
        }
    }

    LOGI() << "[starscore] standardize: 5. the instruments that go";
    // 5. the instruments that go
    removePartsKeepingSystemObjects(doomedParts);

    LOGI() << "[starscore] standardize: 6. arrangement scores";
    // 6. arrangement scores, missing arrangements, and anything left empty
    syncArrangementScores();
    for (const QString& key : missingArrangements) {
        createArrangementFromTemplate(key);
    }
    {
        ExcerptNotationList kept;
        for (const IExcerptNotationPtr& e : master->excerpts()) {
            INotationPtr n = e ? e->notation() : nullptr;
            engraving::Score* es = n && n->elements() ? n->elements()->msScore() : nullptr;
            if (es && es->parts().empty()) {
                continue;
            }
            kept.push_back(e);
        }
        if (kept.size() != master->excerpts().size()) {
            master->setExcerpts(kept);
        }
    }
    standardizeHornNames();
    labelPartBooks();

    const Data after = load();
    // the sections that were showing before stay showing, the others hidden (Joel, 7 Oct 2026: IPDW opened on an
    // arrangement with no music after standardizing, and looked empty); with none of them left, the first arrangement
    bool anyOn = false;
    for (const StarScoreSection& s : after.sections) {
        anyOn = anyOn || wasOn.count(s.id) > 0;
    }
    if (anyOn) {
        for (const StarScoreSection& s : sections()) {
            const bool on = wasOn.count(s.id) > 0;
            if (s.on != on) {
                setSectionOn(s.id, on);
            }
        }
    } else {
        QString show = wasActive;
        if (std::none_of(after.arrangements.begin(), after.arrangements.end(), [&](const StarScoreArrangement& a) { return a.id == show; })) {
            show = after.arrangements.empty() ? QString() : after.arrangements.front().id;
        }
        if (!show.isEmpty()) {
            showArrangement(show);
        }
    }
    if (INotationProjectPtr project = globalContext()->currentProject()) {
        project->markAsUnsaved();
    }
    master->parts()->partsChanged().notify();
    master->notation()->notationChanged().notify();
    scheduleChanged();
    return report;
}
