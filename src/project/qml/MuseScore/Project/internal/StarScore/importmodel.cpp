/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "importmodel.h"

#include "translation.h"

using namespace mu::project;
using namespace muse;

StarScoreImportModel::StarScoreImportModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariantList StarScoreImportModel::parts() const
{
    return m_parts;
}

QVariantList StarScoreImportModel::sectionChoices() const
{
    QVariantList list;
    list << QVariantMap { { "value", QString() }, { "text", muse::qtrc("starscore", "Not in a section") } };
    for (const StarScoreSectionTemplate& t : starScore()->sectionTemplates()) {
        list << QVariantMap { { "value", t.key }, { "text", t.name } };
    }
    return list;
}

QVariantList StarScoreImportModel::arrangements() const
{
    return m_arrangements;
}

QString StarScoreImportModel::summary() const
{
    return m_summary;
}

void StarScoreImportModel::load()
{
    const StarScoreImportPlan plan = starScore()->planImport();
    m_parts.clear();
    for (const StarScoreImportPart& p : plan.parts) {
        m_parts << QVariantMap {
            { "partId", p.partId },
            { "name", p.name },
            { "instrumentId", p.instrumentId },
            { "section", p.suggestedSection },
            { "staves", p.staves },
        };
    }
    m_checked = plan.suggestedArrangements;
    m_summary = plan.summary;
    refreshArrangements();
}

void StarScoreImportModel::refreshArrangements()
{
    QStringList used;
    for (const QVariant& v : m_parts) {
        const QString s = v.toMap().value("section").toString();
        if (!s.isEmpty()) {
            used << s;
        }
    }

    m_arrangements.clear();
    for (const StarScoreArrangementTemplate& a : starScore()->arrangementTemplates()) {
        bool hasMain = false, complete = true;
        for (const QString& k : a.sectionKeys) {
            const bool have = used.contains(k);
            if (k != "lead-sheet" && k != "rhythm") {
                hasMain |= have;
            }
            complete &= have || k == "lead-sheet" || k == "rhythm";
        }
        if (!(hasMain && complete)) {
            continue;
        }
        m_arrangements << QVariantMap { { "key", a.key }, { "name", a.name }, { "checked", m_checked.contains(a.key) } };
    }
    emit changed();
}

void StarScoreImportModel::setPartSection(int index, const QString& sectionKey)
{
    if (index < 0 || index >= m_parts.size()) {
        return;
    }
    QVariantMap m = m_parts[index].toMap();
    m["section"] = sectionKey;
    m_parts[index] = m;

    // A newly possible arrangement starts ticked
    const QVariantList before = m_arrangements;
    QStringList had;
    for (const QVariant& v : before) {
        had << v.toMap().value("key").toString();
    }
    refreshArrangements();
    for (const QVariant& v : m_arrangements) {
        const QString key = v.toMap().value("key").toString();
        if (!had.contains(key) && !m_checked.contains(key)) {
            m_checked << key;
        }
    }
    refreshArrangements();
}

void StarScoreImportModel::setArrangementChecked(const QString& key, bool checked)
{
    m_checked.removeAll(key);
    if (checked) {
        m_checked << key;
    }
    refreshArrangements();
}

bool StarScoreImportModel::doImport()
{
    constexpr int Yes = static_cast<int>(IInteractive::Button::CustomButton) + 1;
    constexpr int No = static_cast<int>(IInteractive::Button::CustomButton) + 2;
    IInteractive::Result result = interactive()->questionSync(
        muse::trc("starscore", "Standardize this file?"),
        muse::trc("starscore", "Standardizing names the lead part “Lead”, names the other instruments as in the "
                               "StarScore sections, adds any missing hidden instruments (such as Congas), makes a part "
                               "book for every instrument and applies the default styles."), {
        IInteractive::ButtonData(No, muse::trc("starscore", "No, keep it as it is")),
        IInteractive::ButtonData(Yes, muse::trc("starscore", "Yes, standardize"), true),
    }, Yes);

    if (result.button() != Yes && result.button() != No) {
        return false;   // dialog closed: stay in the import dialog
    }

    std::map<QString, QString> sectionByPart;
    for (const QVariant& v : m_parts) {
        const QVariantMap m = v.toMap();
        sectionByPart[m.value("partId").toString()] = m.value("section").toString();
    }
    QStringList keys;
    for (const QVariant& v : m_arrangements) {
        const QVariantMap m = v.toMap();
        if (m.value("checked").toBool()) {
            keys << m.value("key").toString();
        }
    }

    Ret ret = starScore()->applyImport(sectionByPart, keys, result.button() == Yes);
    if (!ret) {
        interactive()->error(muse::trc("starscore", "Import failed"), ret.toString());
        return false;
    }
    starScore()->saveAsNewStarScore();
    return true;
}

void StarScoreImportModel::cancelImport()
{
    dispatcher()->dispatch("file-close");
}
