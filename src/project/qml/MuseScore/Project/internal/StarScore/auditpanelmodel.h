/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Audit panel: automatic checks of the parts, and the horn listen-through
 */
#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <qqmlintegration.h>

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "iinteractive.h"
#include "actions/iactionsdispatcher.h"
#include "project/istarscoreservice.h"

namespace mu::project {
class AuditPanelModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    // Checks
    Q_PROPERTY(QVariantList issues READ issues NOTIFY changed)
    Q_PROPERTY(int currentIssue READ currentIssue NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantList arrangementChoices READ arrangementChoices NOTIFY changed)   // [{ text, value }], "" = all
    Q_PROPERTY(QString arrangementId READ arrangementId NOTIFY changed)
    Q_PROPERTY(QString arrangementStatus READ arrangementStatus NOTIFY changed)
    Q_PROPERTY(bool arrangementAudited READ arrangementAudited NOTIFY changed)
    Q_PROPERTY(QVariantList referenceChoices READ referenceChoices NOTIFY changed)       // [{ text, value }]
    Q_PROPERTY(QString referenceId READ referenceId NOTIFY changed)
    Q_PROPERTY(bool showMinor READ showMinor NOTIFY changed)
    Q_PROPERTY(bool showIntentional READ showIntentional NOTIFY changed)
    Q_PROPERTY(bool hasScore READ hasScore NOTIFY changed)

    // Listen-through
    Q_PROPERTY(QVariantList steps READ steps NOTIFY changed)               // the current rehearsal mark's steps
    Q_PROPERTY(int currentStep READ currentStep NOTIFY changed)           // index into steps
    Q_PROPERTY(QString rehearsalTitle READ rehearsalTitle NOTIFY changed)
    Q_PROPERTY(QString listenProgress READ listenProgress NOTIFY changed)
    Q_PROPERTY(bool listenDone READ listenDone NOTIFY changed)
    Q_PROPERTY(bool listening READ listening NOTIFY changed)
    Q_PROPERTY(bool withRhythm READ withRhythm NOTIFY changed)
    Q_PROPERTY(bool autoPlay READ autoPlay NOTIFY changed)
    Q_PROPERTY(bool hasListenSteps READ hasListenSteps NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };

public:
    explicit AuditPanelModel(QObject* parent = nullptr);

    QVariantList issues() const;
    int currentIssue() const;
    QString summary() const;
    QVariantList arrangementChoices() const;
    QString arrangementId() const;
    QString arrangementStatus() const;
    bool arrangementAudited() const;
    QVariantList referenceChoices() const;
    QString referenceId() const;
    bool showMinor() const;
    bool showIntentional() const;
    bool hasScore() const;

    QVariantList steps() const;
    int currentStep() const;
    QString rehearsalTitle() const;
    QString listenProgress() const;
    bool listenDone() const;
    bool listening() const;
    bool withRhythm() const;
    bool autoPlay() const;
    bool hasListenSteps() const;

    Q_INVOKABLE void load();
    Q_INVOKABLE void setActive(bool active);
    Q_INVOKABLE void recheck();

    Q_INVOKABLE void selectIssue(int index);
    Q_INVOKABLE void nextIssue();
    Q_INVOKABLE void previousIssue();
    Q_INVOKABLE void setIntentional(int index, bool intentional);
    Q_INVOKABLE void setArrangementId(const QString& id);
    Q_INVOKABLE void setReferenceId(const QString& id);
    Q_INVOKABLE void setShowMinor(bool show);
    Q_INVOKABLE void setShowIntentional(bool show);
    Q_INVOKABLE void markArrangementAudited(bool audited);
    Q_INVOKABLE void openLibrary();

    Q_INVOKABLE void selectStep(int index);
    Q_INVOKABLE void playStep();
    Q_INVOKABLE void stopPlaying();
    Q_INVOKABLE void approveStep();
    Q_INVOKABLE void unapproveStep(int index);
    Q_INVOKABLE void skipStep();
    Q_INVOKABLE void previousStep();
    Q_INVOKABLE void previousRehearsal();
    Q_INVOKABLE void nextRehearsal();
    Q_INVOKABLE void finishListening();
    Q_INVOKABLE void setWithRhythm(bool on);
    Q_INVOKABLE void setAutoPlay(bool on);

signals:
    void changed();

private:
    void refresh();
    void scheduleRefresh();
    std::vector<int> filteredIssueIndexes() const;
    std::vector<int> stepsOfCurrentGroup() const;
    int firstUnapproved(int from) const;
    void goToStep(int allIndex, bool play);

    StarScoreAuditReport m_report;
    std::vector<StarScoreArrangement> m_arrangements;
    QString m_currentIssueKey;
    QString m_arrangementId;
    bool m_showMinor = false;
    bool m_showIntentional = false;
    QString m_currentStepKey;
    bool m_withRhythm = false;
    bool m_autoPlay = true;
    bool m_active = true;
    bool m_dirty = false;
    QTimer m_timer;
};
}
