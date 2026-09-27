/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "starscorebarmodel.h"

#include <QFileInfo>

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
        emit changed();
    });
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

QVariantList StarScoreBarModel::sections() const
{
    QVariantList result;
    for (const StarScoreSection& s : starScore()->sections()) {
        QVariantMap item;
        item["id"] = s.id;
        item["name"] = s.name;
        item["on"] = s.on;
        item["status"] = int(s.status);
        item["instrumentCount"] = s.partIds.size();
        result << item;
    }
    return result;
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

QVariantList StarScoreBarModel::statusSubmenu(const QString& prefix, int current) const
{
    QVariantList items;
    for (int i = 0; i <= int(StarScoreStatus::Finished); ++i) {
        items << QVariantMap {
            { "id", prefix + QString::number(i) }, { "title", statusName(i) },
            { "checkable", true }, { "checked", i == current }, { "enabled", true }
        };
    }
    return items;
}

QVariantList StarScoreBarModel::arrangementMenu(const QString& id) const
{
    return {
        QVariantMap { { "id", "arr-show:" + id }, { "title", muse::qtrc("starscore", "Show this arrangement") }, { "enabled", true } },
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
    for (const StarScoreSection& s : starScore()->sections()) {
        if (s.id == id) {
            status = int(s.status);
        }
    }

    return {
        QVariantMap { { "id", "sec-solo:" + id }, { "title", muse::qtrc("starscore", "Show only this section") }, { "enabled", true } },
        QVariantMap { { "title", muse::qtrc("starscore", "Status") }, { "subitems", statusSubmenu("sec-status:" + id + ":", status) },
                      { "enabled", true } },
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
    return {
        QVariantMap { { "id", "all-on" }, { "title", muse::qtrc("starscore", "Show all sections") }, { "enabled", true } },
        QVariantMap { { "id", "all-off" }, { "title", muse::qtrc("starscore", "Hide all sections") }, { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "detect" }, { "title", muse::qtrc("starscore", "Make sections from existing part books") },
                      { "enabled", true } },
        QVariantMap {},
        QVariantMap { { "id", "copy-layout" }, { "title", muse::qtrc("starscore", "Copy layout breaks to other parts…") },
                      { "enabled", true } },
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
        RetVal<QString> ret = starScore()->createArrangementFromTemplate(arg);
        if (!ret.ret) {
            interactive()->error(muse::trc("starscore", "Couldn't create the arrangement"), ret.ret.toString());
        }
    } else if (action == "sec-solo") {
        starScore()->soloSection(arg);
    } else if (action == "sec-status") {
        const int c = arg.lastIndexOf(':');
        starScore()->setSectionStatus(arg.left(c), static_cast<StarScoreStatus>(arg.mid(c + 1).toInt()));
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
    } else if (action == "copy-layout") {
        interactive()->open(UriQuery(STARSCORE_COPY_LAYOUT_URI.toStdString()));
    }
}
