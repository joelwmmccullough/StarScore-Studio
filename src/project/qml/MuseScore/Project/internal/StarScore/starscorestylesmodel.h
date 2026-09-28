/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "iinteractive.h"
#include "project/istarscoreservice.h"

namespace mu::project {
//! Model for the "Part styles" dialog: a default style plus rules that give
//! particular part books a particular MuseScore style file (.mss).
class StarScoreStylesModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString defaultStyle READ defaultStyle NOTIFY changed)
    Q_PROPERTY(QVariantList rules READ rules NOTIFY changed)
    Q_PROPERTY(QVariantList sectionOptions READ sectionOptions CONSTANT)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit StarScoreStylesModel(QObject* parent = nullptr);

    QString defaultStyle() const;
    QVariantList rules() const;
    QVariantList sectionOptions() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void chooseDefaultStyle();
    Q_INVOKABLE void clearDefaultStyle();
    Q_INVOKABLE void addRule();
    Q_INVOKABLE void removeRule(int index);
    Q_INVOKABLE void setRuleSection(int index, const QString& sectionKey);
    Q_INVOKABLE void setRulePart(int index, const QString& partName);
    Q_INVOKABLE void chooseRuleStyle(int index);
    Q_INVOKABLE void save();
    //! Save, then restyle the open score's part books; returns a short summary
    Q_INVOKABLE QString applyNow();

signals:
    void changed();

private:
    QString chooseStyleFile(const QString& current) const;

    QString m_defaultStyle;
    std::vector<StarScoreStyleRule> m_rules;
};
}
