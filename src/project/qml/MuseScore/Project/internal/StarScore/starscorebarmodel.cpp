/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "starscorebarmodel.h"

#include <QFileInfo>
#include <QUrl>

#include "types/uri.h"
#include "log.h"

using namespace mu::project;
using namespace muse;

static const QString STARSCORE_EDIT_URI("musescore://starscore/edit");
static const QString STARSCORE_COPY_LAYOUT_URI("musescore://starscore/copylayout");

StarScoreBarModel::StarScoreBarModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void StarScoreBarModel::load()
{
    starScore()->changed().onNotify(this, [this]() {
        if (m_holdUpdates) {
            m_pendingUpdate = true;
            return;
        }
        refresh();
    });
    refresh();
}

void StarScoreBarModel::refresh()
{
    QVariantList arrangements = buildArrangements();
    QVariantList sections = buildSections();
    QVariantList solos = buildSolos();
    const bool arrangementsDiffer = arrangements != m_arrangements;
    const bool sectionsDiffer = sections != m_sections;
    const bool solosDiffer = solos != m_solos;
    m_arrangements = std::move(arrangements);
    m_sections = std::move(sections);
    m_solos = std::move(solos);

    emit changed();
    if (arrangementsDiffer) {
        emit arrangementsChanged();
    }
    if (sectionsDiffer) {
        emit sectionsChanged();
    }
    if (solosDiffer) {
        emit solosChanged();
    }
}

void StarScoreBarModel::setHoldUpdates(bool hold)
{
    m_holdUpdates = hold;
    if (!hold && m_pendingUpdate) {
        m_pendingUpdate = false;
        refresh();
    }
}

bool StarScoreBarModel::decoOn() const
{
    return starScore()->decoOn();
}

void StarScoreBarModel::toggleDeco()
{
    starScore()->toggleDeco();
    emit changed();
}

bool StarScoreBarModel::hasScore() const
{
    return starScore()->hasScore();
}

bool StarScoreBarModel::isStarScoreFile() const
{
    return starScore()->isStarScoreFile();
}

QVariantList StarScoreBarModel::arrangements() const
{
    return m_arrangements;
}

QVariantList StarScoreBarModel::sections() const
{
    return m_sections;
}

QVariantList StarScoreBarModel::solos() const
{
    return m_solos;
}

QVariantList StarScoreBarModel::buildArrangements() const
{
    QVariantList result;
    const QString active = starScore()->activeArrangementId();
    const std::vector<StarScoreSection> sections = starScore()->sections();

    for (const StarScoreArrangement& a : starScore()->arrangements()) {
        QStringList sectionNames;
        for (const QString& id : a.sectionIds) {
            for (const StarScoreSection& s : sections) {
                if (s.id == id) {
                    sectionNames << s.name;
                }
            }
        }

        QVariantMap item;
        item["id"] = a.id;
        item["name"] = a.name;
        item["active"] = (a.id == active);
        item["status"] = int(starScore()->arrangementStatus(a.id));
        item["sections"] = sectionNames.join(" + ");
        result << item;
    }
    return result;
}

QVariantList StarScoreBarModel::buildSections() const
{
    QVariantList result;
    for (const StarScoreSection& s : starScore()->sections()) {
        QVariantMap item;
        item["id"] = s.id;
        item["name"] = s.name;
        item["on"] = s.on;
        item["status"] = int(s.status);
        item["instrumentCount"] = s.partIds.size();
        // Finished with drums / percussion / keys reading the lead sheet: its own dot colour and description
        item["leadSheet"] = s.leadSheetFinish;
        item["statusText"] = s.leadSheetFinish ? leadSheetText(s.autoSkipSheets) : statusName(int(s.status));
        result << item;
    }
    return result;
}

QVariantList StarScoreBarModel::buildSolos() const
{
    QVariantList result;
    const QString current = starScore()->currentSoloId();
    for (const StarScoreSolo& solo : starScore()->solos()) {
        QString info = solo.passes > 1
                       ? muse::qtrc("starscore", "%1 bars · over bars %2–%3 × %4").arg(solo.soloBars).arg(solo.startBar).arg(solo.endBar).arg(solo.passes)
                       : muse::qtrc("starscore", "%1 bars · bars %2–%3").arg(solo.soloBars).arg(solo.startBar).arg(solo.endBar);
        result << QVariantMap { { "id", solo.id }, { "name", solo.name }, { "info", info }, { "active", solo.id == current } };
    }
    return result;
}

bool StarScoreBarModel::isSoloView() const
{
    return !starScore()->currentSoloId().isEmpty();
}

bool StarScoreBarModel::canAddSolos() const
{
    return starScore()->canAddSolos();
}

void StarScoreBarModel::showSolo(const QString& id)
{
    muse::Ret ret = starScore()->showSolo(id);
    if (!ret) {
        interactive()->error(muse::trc("starscore", "Couldn't open the solo"), ret.toString());
    }
}

void StarScoreBarModel::showMainScore()
{
    starScore()->showMainScore();
}

QVariantList StarScoreBarModel::soloMenu(const QString& id) const
{
    return {
        QVariantMap { { "id", "solo-show:" + id }, { "title", muse::qtrc("starscore", "Show this solo") }, { "enabled", true } },
        QVariantMap { { "id", "solo-refresh:" + id }, { "title", muse::qtrc("starscore", "Refresh band parts from the main score") },
                      { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "solo-rename:" + id }, { "title", muse::qtrc("starscore", "Rename…") }, { "enabled", true } },
        QVariantMap { { "id", "solo-export:" + id }, { "title", muse::qtrc("starscore", "Export as MuseScore file (.mscz)…") },
                      { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "solo-remove:" + id }, { "title", muse::qtrc("starscore", "Remove solo from this file") }, { "enabled", true } },
    };
}

void StarScoreBarModel::showArrangement(const QString& id)
{
    starScore()->showArrangement(id);
}

void StarScoreBarModel::toggleSection(const QString& id)
{
    for (const StarScoreSection& s : starScore()->sections()) {
        if (s.id == id) {
            starScore()->setSectionOn(id, !s.on);
            return;
        }
    }
}

void StarScoreBarModel::soloSection(const QString& id)
{
    starScore()->soloSection(id);
}

QString StarScoreBarModel::statusColor(int status) const
{
    switch (static_cast<StarScoreStatus>(status)) {
    case StarScoreStatus::Empty: return "#8A8A8A";
    case StarScoreStatus::Sketch: return "#E0463A";
    case StarScoreStatus::InProgress: return "#F29B30";
    case StarScoreStatus::NeedsReview: return "#3C8CE7";
    case StarScoreStatus::Finished: return "#3FB05A";
    }
    return "#8A8A8A";
}

QString StarScoreBarModel::statusName(int status) const
{
    switch (static_cast<StarScoreStatus>(status)) {
    case StarScoreStatus::Empty: return muse::qtrc("starscore", "Empty");
    case StarScoreStatus::Sketch: return muse::qtrc("starscore", "Sketch");
    case StarScoreStatus::InProgress: return muse::qtrc("starscore", "In progress");
    case StarScoreStatus::NeedsReview: return muse::qtrc("starscore", "Needs review");
    case StarScoreStatus::Finished: return muse::qtrc("starscore", "Finished");
    }
    return QString();
}

QString StarScoreBarModel::leadSheetText(const QStringList& kinds) const
{
    QStringList names;
    for (const QString& k : { QString("drums"), QString("percussion"), QString("keys") }) {
        if (kinds.contains(k)) {
            names << (k == "drums" ? muse::qtrc("starscore", "drums") : k == "percussion" ? muse::qtrc("starscore", "percussion")
                      : muse::qtrc("starscore", "keys"));
        }
    }
    const QString who = names.size() <= 1 ? names.join("") : names.mid(0, names.size() - 1).join(", ") + " " + muse::qtrc("starscore", "and") + " " + names.last();
    return muse::qtrc("starscore", "Finished — %1 use the lead sheet").arg(who);
}

QVariantList StarScoreBarModel::statusSubmenu(const QString& prefix, int current, bool rhythm) const
{
    QVariantList items;
    for (int i = 0; i <= int(StarScoreStatus::Finished); ++i) {
        items << QVariantMap {
            { "id", prefix + QString::number(i) }, { "title", statusName(i) },
            { "checkable", true }, { "checked", i == current }, { "enabled", true },
            { "keepOpen", true }   // the menu stays open, so several things can be ticked in a row
        };
    }
    return items;
}

QVariantList StarScoreBarModel::arrangementMenu(const QString& id) const
{
    return {
        QVariantMap { { "id", "arr-show:" + id }, { "title", muse::qtrc("starscore", "Show this arrangement") }, { "enabled", true } },
        QVariantMap { { "id", "arr-score:" + id }, { "title", muse::qtrc("starscore", "Open this arrangement's score") }, { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "arr-edit:" + id }, { "title", muse::qtrc("starscore", "Choose sections…") }, { "enabled", true } },
        QVariantMap { { "id", "arr-rename:" + id }, { "title", muse::qtrc("starscore", "Rename…") }, { "enabled", true } },
        QVariantMap { { "id", "arr-export:" + id }, { "title", muse::qtrc("starscore", "Export as MuseScore file (.mscz)…") }, { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "arr-left:" + id }, { "title", muse::qtrc("starscore", "Move left") }, { "enabled", true } },
        QVariantMap { { "id", "arr-right:" + id }, { "title", muse::qtrc("starscore", "Move right") }, { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "arr-delete:" + id }, { "title", muse::qtrc("starscore", "Delete arrangement (keeps its sections)") }, { "enabled", true } },
    };
}

QVariantList StarScoreBarModel::sectionMenu(const QString& id) const
{
    int status = 0;
    bool rhythm = false;
    bool autoStatus = false;
    bool leadSheet = false;
    QStringList autoSkip;
    QStringList skip;
    QStringList partIds;
    for (const StarScoreSection& s : starScore()->sections()) {
        if (s.id == id) {
            status = int(s.status);
            rhythm = s.templateKey == "rhythm" || s.templateKey == "bigband-rhythm";
            skip = s.skipSheets;
            autoStatus = s.autoStatus;
            partIds = s.partIds;
            leadSheet = s.leadSheetFinish;
            autoSkip = s.autoSkipSheets;
        }
    }

    // "Auto" first: the section's status follows its parts' tags
    QVariantList statusItems;
    statusItems << QVariantMap {
        { "id", "sec-auto:" + id },
        { "title", autoStatus ? muse::qtrc("starscore", "Auto (from its parts: %1)").arg(leadSheet ? leadSheetText(autoSkip) : statusName(status))
          : muse::qtrc("starscore", "Auto (from its parts)") },
        { "checkable", true }, { "checked", autoStatus }, { "enabled", true }, { "keepOpen", true }
    };
    statusItems << QVariantMap {};
    for (QVariant& v : statusSubmenu("sec-status:" + id + ":", autoStatus ? -1 : status, rhythm)) {
        statusItems << v;
    }

    // Each part's own tag
    const std::map<QString, StarScoreStatus> tags = starScore()->partStatuses();
    std::map<QString, QString> names;
    for (const StarScorePartInfo& p : starScore()->parts()) {
        names[p.partId] = p.name;
    }
    QVariantList partItems;
    QStringList usedTitles;
    for (const QString& pid : partIds) {
        QString title = names.count(pid) ? names[pid] : pid;
        const QString base = title;
        for (int n = 2; usedTitles.contains(title); ++n) {
            title = QString("%1 (%2)").arg(base).arg(n);   // two parts with the same name
        }
        usedTitles << title;
        auto it = tags.find(pid);
        const int current = it == tags.end() ? -1 : int(it->second);
        QVariantList tagItems;
        tagItems << QVariantMap {
            { "id", "part-status:" + pid + ":-1" }, { "title", muse::qtrc("starscore", "No tag") },
            { "checkable", true }, { "checked", current < 0 }, { "enabled", true }, { "keepOpen", true }
        };
        tagItems << QVariantMap {};
        for (QVariant& v : statusSubmenu("part-status:" + pid + ":", current, false)) {
            tagItems << v;
        }
        partItems << QVariantMap { { "title", title }, { "subitems", tagItems }, { "enabled", true } };
    }
    if (rhythm) {
        // Finished rhythm section: players who read from the lead sheet get no sheet of their own by default
        const bool finished = status == int(StarScoreStatus::Finished);
        statusItems << QVariantMap {};
        const std::vector<std::pair<QString, QString> > sheets {
            { "percussion", muse::qtrc("starscore", "No Percussion Sheet") },
            { "drums", muse::qtrc("starscore", "No Drums Sheet") },
            { "keys", muse::qtrc("starscore", "No Keys Sheet") },
        };
        for (const auto& [key, title] : sheets) {
            statusItems << QVariantMap {
                { "id", "sec-skip:" + id + ":" + key }, { "title", title },
                { "checkable", true }, { "checked", finished && skip.contains(key) }, { "enabled", finished },
                { "keepOpen", true }
            };
        }
    }

    // the parts with stand-in versions: a 7-Horn section's main low horn (the bass trombone, or whatever 7th horn the
    // song was made with), a piccolo
    QStringList versionMains;
    for (const auto& [pid, name] : starScore()->versionMains(id)) {
        versionMains << name;
    }

    QVariantList items {
        QVariantMap { { "id", "sec-solo:" + id }, { "title", muse::qtrc("starscore", "Show only this section") }, { "enabled", true } },
        QVariantMap { { "title", muse::qtrc("starscore", "Status") }, { "subitems", statusItems },
                      { "enabled", true } },
        QVariantMap { { "title", muse::qtrc("starscore", "Part status") }, { "subitems", partItems },
                      { "enabled", !partItems.isEmpty() } },
        QVariantMap {},
        QVariantMap { { "id", "sec-edit:" + id }, { "title", muse::qtrc("starscore", "Choose instruments…") }, { "enabled", true } },
        QVariantMap { { "id", "sec-rename:" + id }, { "title", muse::qtrc("starscore", "Rename…") }, { "enabled", true } },
        QVariantMap { { "id", "sec-left:" + id }, { "title", muse::qtrc("starscore", "Move left") }, { "enabled", true } },
        QVariantMap { { "id", "sec-right:" + id }, { "title", muse::qtrc("starscore", "Move right") }, { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "sec-delete:" + id }, { "title", muse::qtrc("starscore", "Delete section (keep its instruments)") },
                      { "enabled", true } },
        QVariantMap { { "id", "sec-delete-all:" + id }, { "title", muse::qtrc("starscore", "Delete section and its instruments") },
                      { "enabled", true } },
    };
    // Flexible sections: any of their sheets made into a part of its own, to edit by hand
    QVariantList flexItems;
    for (const StarScoreFlexibleSheet& sheet : starScore()->flexibleSheets(id)) {
        flexItems << QVariantMap { { "id", "sec-flexsheet:" + id + "|" + sheet.name },
                                   { "title", sheet.partId.isEmpty() ? sheet.name
                                     : muse::qtrc("starscore", "%1 (edited by hand: open)").arg(sheet.name) },
                                   { "enabled", true } };
    }
    if (!flexItems.isEmpty()) {
        items.insert(4, QVariantMap { { "title", muse::qtrc("starscore", "Edit one sheet by hand") }, { "subitems", flexItems },
                                      { "enabled", true } });
    }
    if (!versionMains.isEmpty()) {
        // the other versions (Bari Sax, Bass Sax, Bassoon… of the low horn; a Flute of a piccolo): only the ones the
        // section doesn't have
        const QString who = versionMains.size() == 1 ? versionMains.first()
                            : versionMains.mid(0, versionMains.size() - 1).join(", ") + muse::qtrc("starscore", " and ")
                            + versionMains.last();
        items.insert(4, QVariantMap { { "id", "sec-bass-versions:" + id },
                                      { "title", versionMains.size() == 1 && versionMains.first() == "Piccolo"
                                        ? muse::qtrc("starscore", "Make the Piccolo's Flute version…")
                                        : muse::qtrc("starscore", "Make the %1's other versions…").arg(who) },
                                      { "enabled", true } });
    }
    return items;
}

QVariantList StarScoreBarModel::addArrangementMenu() const
{
    QVariantList items;
    items << QVariantMap { { "id", "arr-new-blank" }, { "title", muse::qtrc("starscore", "Blank arrangement…") }, { "enabled", true } };
    items << QVariantMap {};

    const std::vector<StarScoreSection> existing = starScore()->sections();
    std::vector<StarScoreSectionTemplate> sectionTpls = starScore()->sectionTemplates();

    for (const StarScoreArrangementTemplate& t : starScore()->arrangementTemplates()) {
        QStringList toCreate;
        for (const QString& key : t.sectionKeys) {
            const bool exists = std::any_of(existing.begin(), existing.end(), [&](const StarScoreSection& s) {
                return s.templateKey == key;
            });
            if (!exists) {
                for (const StarScoreSectionTemplate& st : sectionTpls) {
                    if (st.key == key) {
                        toCreate << st.name;
                    }
                }
            }
        }

        QString title = t.name;
        if (toCreate.isEmpty()) {
            title += "   " + muse::qtrc("starscore", "(uses existing sections)");
        } else {
            title += "   " + muse::qtrc("starscore", "(adds %1)").arg(toCreate.join(", "));
        }

        items << QVariantMap { { "id", "arr-new-tpl:" + t.key }, { "title", title }, { "enabled", true } };
    }

    return items;
}

QVariantList StarScoreBarModel::addSectionMenu() const
{
    QVariantList items;
    for (const StarScoreSectionTemplate& t : starScore()->sectionTemplates()) {
        QStringList names;
        for (const StarScoreInstrument& i : t.instruments) {
            names << i.partName;
        }
        items << QVariantMap { { "id", "sec-new-tpl:" + t.key }, { "title", t.name + "   (" + names.join(", ") + ")" },
                               { "enabled", true } };
    }
    items << QVariantMap {};
    items << QVariantMap { { "id", "sec-new-custom" }, { "title", muse::qtrc("starscore", "New instruments…") }, { "enabled", true } };
    items << QVariantMap { { "id", "sec-new-existing" }, { "title", muse::qtrc("starscore", "From instruments already in the score…") },
                           { "enabled", true } };
    return items;
}

QVariantList StarScoreBarModel::moreMenu() const
{
    // Reference PDFs (e.g. the original chart of a cover), kept inside the .starscore
    QVariantList refItems;
    refItems << QVariantMap { { "id", "ref-add" }, { "title", muse::qtrc("starscore", "Add reference PDF…") }, { "enabled", true } };
    const std::vector<StarScoreReference> refs = starScore()->references();
    if (!refs.empty()) {
        refItems << QVariantMap {};
    }
    for (const StarScoreReference& r : refs) {
        refItems << QVariantMap {
            { "title", r.name }, { "enabled", true },
            { "subitems", QVariantList {
                  QVariantMap { { "id", "ref-open:" + r.id }, { "title", muse::qtrc("starscore", "Show beside the score") },
                                { "enabled", true } },
                  QVariantMap { { "id", "ref-external:" + r.id }, { "title", muse::qtrc("starscore", "Open in another app") },
                                { "enabled", true } },
                  QVariantMap { { "id", "ref-remove:" + r.id }, { "title", muse::qtrc("starscore", "Remove from this score") },
                                { "enabled", true } },
              } }
        };
    }

    return {
        QVariantMap { { "title", refs.empty() ? muse::qtrc("starscore", "Reference PDFs")
                                              : muse::qtrc("starscore", "Reference PDFs (%1)").arg(refs.size()) },
                      { "subitems", refItems }, { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "all-on" }, { "title", muse::qtrc("starscore", "Show all sections") }, { "enabled", true } },
        QVariantMap { { "id", "all-off" }, { "title", muse::qtrc("starscore", "Hide all sections") }, { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "detect" }, { "title", muse::qtrc("starscore", "Make sections from existing part books") },
                      { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "layout-to" }, { "title", muse::qtrc("starscore", "Copy part formatting…") },
                      { "enabled", true } },
        QVariantMap { { "id", "part-styles" }, { "title", muse::qtrc("starscore", "Part styles…") }, { "enabled", true } },
        QVariantMap { { "id", "apply-styles" }, { "title", muse::qtrc("starscore", "Apply part styles now") }, { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "toggle-deco" }, { "title", muse::qtrc("starscore", "StarScore Deco font") },
                      { "checkable", true }, { "checked", starScore()->decoOn() }, { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "color-notes" }, { "title", muse::qtrc("starscore", "Color notes by pitch") }, { "enabled", true } },
        QVariantMap { { "id", "uncolor-notes" }, { "title", muse::qtrc("starscore", "Remove note colors") }, { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "compare-parts" }, { "title", muse::qtrc("starscore", "Compare parts…") }, { "enabled", true } },
        QVariantMap { { "id", "toggle-minmaj" },
                      { "title", starScore()->minMajSymbolInCurrentScore()
                        ? muse::qtrc("starscore", "Minor-major / diminished-major symbols in this part: on (switch off)")
                        : muse::qtrc("starscore", "Minor-major / diminished-major symbols in this part: off (switch on)") },
                      { "enabled", true } },
        QVariantMap { { "id", "voice-order" }, { "title", muse::qtrc("starscore", "Check voice order…") }, { "enabled", true } },
        QVariantMap { { "id", "audit" }, { "title", muse::qtrc("starscore", "Audit this song") }, { "enabled", true } },
        QVariantMap { { "id", "audit-library" }, { "title", muse::qtrc("starscore", "Audit library…") }, { "enabled", true } },
        QVariantMap { { "id", "sync-scores" }, { "title", muse::qtrc("starscore", "Make / update arrangement scores") }, { "enabled", true } },
        QVariantMap { { "id", "check-ranges" }, { "title", muse::qtrc("starscore", "Check instrument ranges") }, { "enabled", true } },
        QVariantMap { { "id", "set-version" }, { "title", muse::qtrc("starscore", "Version number (%1)…").arg(starScore()->scoreVersion()) },
                      { "enabled", true } },
        QVariantMap { { "id", "export-band" }, { "title", muse::qtrc("starscore", "Export to Sheets and Demos…") }, { "enabled", true } },
        QVariantMap { { "id", "recordings" }, { "title", muse::qtrc("starscore", "Recordings…") }, { "enabled", true } },
        QVariantMap { { "id", "organize" }, { "title", muse::qtrc("starscore", "Run folder organization…") }, { "enabled", true } },
        QVariantMap { { "id", "export-arrangements" }, { "title", muse::qtrc("starscore", "Export arrangements as MuseScore files…") },
                      { "enabled", !starScore()->arrangements().empty() } },
    };
}

void StarScoreBarModel::openEditDialog(const QString& mode, const QString& id, const QString& extra)
{
    UriQuery query(STARSCORE_EDIT_URI.toStdString());
    query.addParam("mode", Val(mode.toStdString()));
    query.addParam("itemId", Val(id.toStdString()));
    query.addParam("extra", Val(extra.toStdString()));
    interactive()->open(query);
}

void StarScoreBarModel::createCustomSection()
{
    auto promise = selectInstrumentsScenario()->selectInstruments();
    promise.onResolve(this, [this](const notation::PartInstrumentListScoreOrder& result) {
        std::vector<StarScoreInstrument> instruments;
        for (const notation::PartInstrument& pi : result.instruments) {
            if (pi.isExistingPart) {
                continue;
            }
            StarScoreInstrument inst;
            inst.instrumentId = pi.instrumentTemplate.id.toQString();
            instruments.push_back(inst);
        }
        if (instruments.empty()) {
            return;
        }

        RetVal<QString> created = starScore()->createSection("custom", muse::qtrc("starscore", "New Section"), instruments);
        if (created.ret) {
            openEditDialog("rename-section", created.val);
        }
    });
}

void StarScoreBarModel::exportArrangement(const QString& id)
{
    QString arrangementName;
    for (const StarScoreArrangement& a : starScore()->arrangements()) {
        if (a.id == id) {
            arrangementName = a.name;
        }
    }

    QString dir;
    QString base = arrangementName;
    if (auto project = globalContext()->currentProject()) {
        const QFileInfo fi(project->path().toQString());
        dir = fi.absolutePath();
        base = project->displayName() + " - " + arrangementName;
    }

    const io::path_t defaultPath = io::path_t(dir).appendingComponent(base).appendingSuffix("mscz");
    const io::path_t path = interactive()->selectSavingFileSync(muse::trc("starscore", "Export arrangement"), defaultPath,
                                                                { muse::trc("project", "MuseScore file") + " (*.mscz)" });
    if (path.empty()) {
        return;
    }

    Ret ret = starScore()->exportArrangement(id, path);
    if (!ret) {
        interactive()->error(muse::trc("starscore", "Couldn't export the arrangement"), ret.toString());
    }
}

void StarScoreBarModel::handleMenuItem(const QString& itemId)
{
    const int colon = itemId.indexOf(':');
    const QString action = colon >= 0 ? itemId.left(colon) : itemId;
    const QString arg = colon >= 0 ? itemId.mid(colon + 1) : QString();

    auto indexOfSection = [this](const QString& id) {
        const auto list = starScore()->sections();
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].id == id) {
                return int(i);
            }
        }
        return -1;
    };
    auto indexOfArrangement = [this](const QString& id) {
        const auto list = starScore()->arrangements();
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].id == id) {
                return int(i);
            }
        }
        return -1;
    };

    if (action == "arr-show") {
        starScore()->showArrangement(arg);
    } else if (action == "arr-score") {
        starScore()->openArrangementScore(arg);
    } else if (action == "arr-edit") {
        openEditDialog("arrangement", arg);
    } else if (action == "arr-rename") {
        openEditDialog("rename-arrangement", arg);
    } else if (action == "arr-export") {
        exportArrangement(arg);
    } else if (action == "arr-left") {
        starScore()->moveArrangement(arg, indexOfArrangement(arg) - 1);
    } else if (action == "arr-right") {
        starScore()->moveArrangement(arg, indexOfArrangement(arg) + 1);
    } else if (action == "arr-delete") {
        starScore()->removeArrangement(arg);
    } else if (action == "arr-new-blank") {
        openEditDialog("arrangement", QString());
    } else if (action == "arr-new-tpl") {
        // A Standard arrangement of 3 or more horns whose horn section is new asks what the doubler plays (and the
        // 7th horn of a 7-Horn), as File › New does; everything else is made at once
        bool asks = false;
        if (arg.endsWith("-horn-standard") && arg.left(arg.indexOf('-')).toInt() >= 3) {
            const QString hornKey = QString("%1-horn").arg(arg.left(arg.indexOf('-')).toInt());
            const std::vector<StarScoreSection> sections = starScore()->sections();
            asks = std::none_of(sections.begin(), sections.end(), [&](const StarScoreSection& s) { return s.templateKey == hornKey; });
        }
        if (asks) {
            UriQuery query("musescore://starscore/addarrangement");
            query.addParam("arrangementKey", Val(arg.toStdString()));
            interactive()->open(query);
        } else {
            RetVal<QString> ret = starScore()->createArrangementFromTemplate(arg);
            if (!ret.ret) {
                interactive()->error(muse::trc("starscore", "Couldn't create the arrangement"), ret.ret.toString());
            }
        }
    } else if (action == "sec-flexsheet") {
        const int bar = arg.indexOf('|');
        const QString sectionId = arg.left(bar);
        const QString sheet = arg.mid(bar + 1);
        bool made = false;
        for (const StarScoreFlexibleSheet& s : starScore()->flexibleSheets(sectionId)) {
            made |= s.name == sheet && !s.partId.isEmpty();
        }
        if (!made) {
            IInteractive::Result res = interactive()->questionSync(
                muse::qtrc("starscore", "Edit %1 by hand?").arg(sheet).toStdString(),
                muse::trc("starscore", "This sheet becomes a part of its own in the section, with the chair's music on its "
                                       "instrument, hidden in the score. Its part score opens: edit it there. From then on the "
                                       "export prints it as it is, and it no longer follows changes to the chair. Delete the "
                                       "part to go back to the sheet made from the chair."),
                { IInteractive::Button::Cancel, IInteractive::Button::Ok }, IInteractive::Button::Ok);
            if (res.standardButton() != IInteractive::Button::Ok) {
                return;
            }
        }
        RetVal<QString> ret = starScore()->makeFlexibleSheetPart(sectionId, sheet);
        if (!ret.ret) {
            interactive()->error(muse::trc("starscore", "Couldn't make the part"), ret.ret.toString());
        }
    } else if (action == "sec-bass-versions") {
        starScore()->makeBassHornVersions(arg);
    } else if (action == "sec-solo") {
        starScore()->soloSection(arg);
    } else if (action == "sec-status") {
        const int c = arg.lastIndexOf(':');
        starScore()->setSectionStatus(arg.left(c), static_cast<StarScoreStatus>(arg.mid(c + 1).toInt()));
    } else if (action == "sec-auto") {
        starScore()->setSectionAutoStatus(arg);
    } else if (action == "part-status") {
        const int c = arg.lastIndexOf(':');
        starScore()->setPartStatus(arg.left(c), arg.mid(c + 1).toInt());
    } else if (action == "sec-skip") {
        const int c = arg.lastIndexOf(':');
        const QString sectionId = arg.left(c);
        const QString which = arg.mid(c + 1);
        bool skipped = false;
        for (const StarScoreSection& s : starScore()->sections()) {
            if (s.id == sectionId) {
                skipped = s.skipSheets.contains(which);
            }
        }
        starScore()->setSectionSkipSheet(sectionId, which, !skipped);
    } else if (action == "sec-edit") {
        openEditDialog("section", arg);
    } else if (action == "sec-rename") {
        openEditDialog("rename-section", arg);
    } else if (action == "sec-left") {
        starScore()->moveSection(arg, indexOfSection(arg) - 1);
    } else if (action == "sec-right") {
        starScore()->moveSection(arg, indexOfSection(arg) + 1);
    } else if (action == "sec-delete") {
        starScore()->removeSection(arg, false);
    } else if (action == "sec-delete-all") {
        IInteractive::Result res = interactive()->warningSync(
            muse::trc("starscore", "Delete this section and its instruments?"),
            muse::trc("starscore", "The instruments, their music and their part books are deleted. You can undo this."),
            { IInteractive::Button::Cancel, IInteractive::Button::Ok }, IInteractive::Button::Cancel);
        if (res.standardButton() == IInteractive::Button::Ok) {
            starScore()->removeSection(arg, true);
        }
    } else if (action == "sec-new-tpl") {
        // A section whose arrangements aren't in the song yet (a 4-Horn Section with no 4-Horn Standard): probably
        // meant as a new arrangement. Asked, never refused.
        std::vector<StarScoreArrangementTemplate> owners;
        for (const StarScoreArrangementTemplate& t : starScore()->arrangementTemplates()) {
            if (t.sectionKeys.contains(arg)) {
                owners.push_back(t);
            }
        }
        bool haveOwner = false;
        for (const StarScoreArrangement& a : starScore()->arrangements()) {
            for (const StarScoreArrangementTemplate& t : owners) {
                haveOwner |= a.templateKey == t.key;
            }
        }
        if (!owners.empty() && !haveOwner) {
            constexpr int Section = static_cast<int>(IInteractive::Button::CustomButton) + 1;
            constexpr int Arrangement = static_cast<int>(IInteractive::Button::CustomButton) + 2;
            std::vector<IInteractive::ButtonData> buttons;
            if (owners.size() == 1) {
                buttons.push_back(IInteractive::ButtonData(Arrangement, muse::qtrc("starscore", "Add the %1 arrangement")
                                                           .arg(owners.front().name).toStdString()));
            }
            buttons.push_back(IInteractive::ButtonData(Section, muse::trc("starscore", "Add the section"), true));
            const IInteractive::Result answer = interactive()->questionSync(
                muse::trc("starscore", "Did you mean to create a new arrangement?"),
                muse::trc("starscore", "This section only belongs to arrangements this song doesn't have yet."),
                buttons, Section);
            if (answer.button() == Arrangement) {
                handleMenuItem("arr-new-tpl:" + owners.front().key);
                return;
            }
        }
        RetVal<QString> ret = starScore()->createSectionFromTemplate(arg);
        if (!ret.ret) {
            interactive()->error(muse::trc("starscore", "Couldn't create the section"), ret.ret.toString());
        }
    } else if (action == "sec-new-custom") {
        createCustomSection();
    } else if (action == "sec-new-existing") {
        openEditDialog("section", QString());
    } else if (action == "all-on") {
        starScore()->setAllSectionsOn(true);
    } else if (action == "all-off") {
        starScore()->setAllSectionsOn(false);
    } else if (action == "detect") {
        const int added = starScore()->detectSections();
        interactive()->info(muse::trc("starscore", "Make sections from part books"),
                            added > 0
                            ? muse::qtrc("starscore", "Added %n section(s).", nullptr, added).toStdString()
                            : muse::trc("starscore", "No new sections found. Sections come from part books named like "
                                                     "“3-Horn Arrangement” or “Lead Sheet”, plus rhythm-section instruments."));
    } else if (action == "solo-show") {
        showSolo(arg);
    } else if (action == "solo-refresh") {
        muse::Ret ret = starScore()->refreshSoloBand(arg);
        if (!ret) {
            interactive()->error(muse::trc("starscore", "Couldn't refresh the band parts"), ret.toString());
        }
    } else if (action == "solo-rename") {
        openEditDialog("rename-solo", arg);
    } else if (action == "ref-add") {
        const QFileInfo mainFile(starScore()->mainProjectPath().toQString());
        const muse::io::paths_t paths = interactive()->selectOpeningFilesSync(
            muse::trc("starscore", "Add reference PDFs"), muse::io::path_t(mainFile.absolutePath()), { "PDF (*.pdf)" });
        for (const muse::io::path_t& p : paths) {
        const QString same = starScore()->identicalReferenceName(p);
        if (!same.isEmpty()) {
            constexpr int Add = static_cast<int>(muse::IInteractive::Button::CustomButton) + 1;
            constexpr int Skip = static_cast<int>(muse::IInteractive::Button::CustomButton) + 2;
            const muse::IInteractive::Result answer = interactive()->questionSync(
                muse::trc("starscore", "This PDF is already here"),
                muse::qtrc("starscore", "“%1” is identical to the reference PDF “%2” already in this score. Import it anyway?")
                .arg(QFileInfo(p.toQString()).fileName(), same).toStdString(), {
                muse::IInteractive::ButtonData(Skip, muse::trc("starscore", "Don't import"), true),
                muse::IInteractive::ButtonData(Add, muse::trc("starscore", "Import anyway")),
            }, Skip);
            if (answer.button() != Add) {
                continue;
            }
        }
            muse::RetVal<QString> ret = starScore()->addReference(p);
            if (!ret.ret) {
                interactive()->error(muse::trc("starscore", "Couldn't add the reference PDF"), ret.ret.toString());
            }
        }
    } else if (action == "ref-open") {
        // shown in the Reference PDF panel beside the score
        starScore()->setCurrentReferenceId(arg);
        if (dockWindowProvider()->window() && !dockWindowProvider()->window()->isDockOpen("starscoreReferencePanel")) {
            dispatcher()->dispatch("toggle-starscore-reference");
        }
    } else if (action == "ref-external") {
        const muse::io::path_t p = starScore()->referencePath(arg);
        if (QFileInfo::exists(p.toQString())) {
            interactive()->openUrl(QUrl::fromLocalFile(p.toQString()));
        } else {
            interactive()->error(muse::trc("starscore", "Reference PDF not found"),
                                 muse::trc("starscore", "Save the score and open it again."));
        }
    } else if (action == "ref-remove") {
        starScore()->removeReference(arg);
    } else if (action == "solo-export") {
        QString name = arg;
        for (const StarScoreSolo& solo : starScore()->solos()) {
            if (solo.id == arg) {
                name = solo.name;
            }
        }
        const QFileInfo mainFile(starScore()->mainProjectPath().toQString());
        const io::path_t defaultPath = io::path_t(mainFile.absolutePath()).appendingComponent(name).appendingSuffix("mscz");
        const io::path_t path = interactive()->selectSavingFileSync(muse::trc("starscore", "Export solo"), defaultPath,
                                                                    { muse::trc("project", "MuseScore file") + " (*.mscz)" });
        if (!path.empty()) {
            muse::Ret ret = starScore()->exportSolo(arg, path);
            if (!ret) {
                interactive()->error(muse::trc("starscore", "Couldn't export the solo"), ret.toString());
            }
        }
    } else if (action == "solo-remove") {
        IInteractive::Result res = interactive()->warningSync(
            muse::trc("starscore", "Remove this solo from the .starscore?"),
            muse::trc("starscore", "Its transcription is deleted from this file when you save. Export it first if you want a copy."),
            { IInteractive::Button::Cancel, IInteractive::Button::Ok }, IInteractive::Button::Cancel);
        if (res.standardButton() == IInteractive::Button::Ok) {
            starScore()->removeSolo(arg);
        }
    } else if (action == "solo-add") {
        interactive()->open(UriQuery("musescore://starscore/addsolo"));
    } else if (action == "part-styles") {
        dispatcher()->dispatch("starscore-part-styles");
    } else if (action == "apply-styles") {
        starScore()->applyStyles();
    } else if (action == "color-notes") {
        dispatcher()->dispatch("starscore-color-notes");
    } else if (action == "uncolor-notes") {
        dispatcher()->dispatch("starscore-uncolor-notes");
    } else if (action == "copy-layout" || action == "layout-to") {
        interactive()->open(UriQuery(STARSCORE_COPY_LAYOUT_URI.toStdString() + "?mode=copy"));
    } else if (action == "layout-from") {
        interactive()->open(UriQuery(STARSCORE_COPY_LAYOUT_URI.toStdString() + "?mode=copy"));
    } else if (action == "compare-parts") {
        dispatcher()->dispatch("starscore-compare-parts");
    } else if (action == "toggle-deco") {
        starScore()->toggleDeco();
    } else if (action == "toggle-minmaj") {
        starScore()->setMinMajSymbolInCurrentScore(!starScore()->minMajSymbolInCurrentScore());
    } else if (action == "voice-order") {
        dispatcher()->dispatch("starscore-voice-order");
    } else if (action == "audit") {
        if (dockWindowProvider()->window() && !dockWindowProvider()->window()->isDockOpen("starscoreAuditPanel")) {
            dispatcher()->dispatch("toggle-starscore-audit");
        }
    } else if (action == "audit-library") {
        dispatcher()->dispatch("starscore-audit-library");
    } else if (action == "sync-scores") {
        starScore()->syncArrangementScores();
    } else if (action == "check-ranges") {
        dispatcher()->dispatch("starscore-check-ranges");
    } else if (action == "export-band") {
        dispatcher()->dispatch("starscore-export-band");
    } else if (action == "set-version") {
        openEditDialog("version", QString());
    } else if (action == "export-arrangements") {
        dispatcher()->dispatch("starscore-export-arrangements");
    } else if (action == "recordings") {
        dispatcher()->dispatch("starscore-recordings");
    } else if (action == "organize") {
        dispatcher()->dispatch("starscore-organize");
    }
}

bool StarScoreBarModel::panelVisible() const
{
    return starScore()->isPanelVisible();
}

void StarScoreBarModel::hidePanel()
{
    starScore()->setPanelVisible(false);
}
