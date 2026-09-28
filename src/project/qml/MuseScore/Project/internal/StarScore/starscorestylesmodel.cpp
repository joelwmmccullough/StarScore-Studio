/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "starscorestylesmodel.h"

#include <QDir>
#include <QFileInfo>

#include "translation.h"

using namespace mu::project;

StarScoreStylesModel::StarScoreStylesModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void StarScoreStylesModel::load()
{
    m_defaultStyle = starScore()->defaultStylePath();
    m_rules = starScore()->styleRules();
    emit changed();
}

QString StarScoreStylesModel::defaultStyle() const
{
    return m_defaultStyle;
}

QVariantList StarScoreStylesModel::rules() const
{
    QVariantList list;
    for (const StarScoreStyleRule& r : m_rules) {
        list << QVariantMap {
            { "section", r.sectionKey },
            { "part", r.partName },
            { "style", r.stylePath },
            { "styleName", r.stylePath.isEmpty() ? muse::qtrc("starscore", "Choose a style…") : QFileInfo(r.stylePath).fileName() },
            { "missing", !r.stylePath.isEmpty() && !QFileInfo::exists(r.stylePath) }
        };
    }
    return list;
}

QVariantList StarScoreStylesModel::sectionOptions() const
{
    QVariantList list;
    list << QVariantMap { { "text", muse::qtrc("starscore", "Any section") }, { "value", QString() } };
    for (const StarScoreSectionTemplate& t : starScore()->sectionTemplates()) {
        list << QVariantMap { { "text", t.name }, { "value", t.key } };
    }
    list << QVariantMap { { "text", muse::qtrc("starscore", "Custom sections") }, { "value", QString("custom") } };
    return list;
}

QString StarScoreStylesModel::chooseStyleFile(const QString& current) const
{
    QString dir = current.isEmpty() ? QDir::homePath() + "/Documents/MuseScore4/Styles" : QFileInfo(current).absolutePath();
    const muse::io::path_t path = interactive()->selectOpeningFileSync(
        muse::trc("starscore", "Choose a MuseScore style"), muse::io::path_t(dir),
        { muse::trc("starscore", "MuseScore style") + " (*.mss)" });
    return path.toQString();
}

void StarScoreStylesModel::chooseDefaultStyle()
{
    const QString path = chooseStyleFile(m_defaultStyle);
    if (!path.isEmpty()) {
        m_defaultStyle = path;
        save();
    }
}

void StarScoreStylesModel::clearDefaultStyle()
{
    m_defaultStyle.clear();
    save();
}

void StarScoreStylesModel::addRule()
{
    m_rules.push_back({});
    emit changed();
}

void StarScoreStylesModel::removeRule(int index)
{
    if (index >= 0 && index < int(m_rules.size())) {
        m_rules.erase(m_rules.begin() + index);
        save();
    }
}

void StarScoreStylesModel::setRuleSection(int index, const QString& sectionKey)
{
    if (index >= 0 && index < int(m_rules.size())) {
        m_rules[index].sectionKey = sectionKey;
        save();
    }
}

void StarScoreStylesModel::setRulePart(int index, const QString& partName)
{
    if (index >= 0 && index < int(m_rules.size())) {
        m_rules[index].partName = partName;
        starScore()->setStyleRules(m_rules);   // no changed(): keeps the text field focused
    }
}

void StarScoreStylesModel::chooseRuleStyle(int index)
{
    if (index < 0 || index >= int(m_rules.size())) {
        return;
    }
    const QString path = chooseStyleFile(m_rules[index].stylePath.isEmpty() ? m_defaultStyle : m_rules[index].stylePath);
    if (!path.isEmpty()) {
        m_rules[index].stylePath = path;
        save();
    }
}

void StarScoreStylesModel::save()
{
    starScore()->setDefaultStylePath(m_defaultStyle);
    starScore()->setStyleRules(m_rules);
    emit changed();
}

QString StarScoreStylesModel::applyNow()
{
    save();
    if (!starScore()->hasScore()) {
        return muse::qtrc("starscore", "Saved. Open a score to apply the styles.");
    }
    const int n = starScore()->applyStyles();
    return muse::qtrc("starscore", "Saved. Restyled %1 score(s) and part book(s).").arg(n);
}
