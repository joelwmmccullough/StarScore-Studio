/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Audit panel: automatic checks of the parts, and the horn listen-through
 */
#include "auditpanelmodel.h"

#include <QFileInfo>

#include <algorithm>

#include "translation.h"

using namespace mu::project;

static QString auditCheckLabel(const QString& check)
{
    if (check == "any-keys") {
        return muse::qtrc("starscore", "Other keys");
    } else if (check == "reference") {
        return muse::qtrc("starscore", "Reference");
    } else if (check == "melody") {
        return muse::qtrc("starscore", "Melody");
    } else if (check == "structure") {
        return muse::qtrc("starscore", "Structure");
    } else if (check == "range") {
        return muse::qtrc("starscore", "Range");
    } else if (check == "crossing") {
        return muse::qtrc("starscore", "Voice order");
    } else if (check == "doubling") {
        return muse::qtrc("starscore", "Unison");
    } else if (check == "dynamics") {
        return muse::qtrc("starscore", "Dynamics");
    }
    return check;
}

static QString auditBarsText(int from, int to)
{
    return from == to ? muse::qtrc("starscore", "Bar %1").arg(from) : muse::qtrc("starscore", "Bars %1–%2").arg(from).arg(to);
}

AuditPanelModel::AuditPanelModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
    m_timer.setSingleShot(true);
    m_timer.setInterval(700);
    connect(&m_timer, &QTimer::timeout, this, [this]() { refresh(); });
}

void AuditPanelModel::load()
{
    starScore()->changed().onNotify(this, [this]() {
        scheduleRefresh();
    });
    starScore()->listeningChanged().onNotify(this, [this]() {
        emit changed();
    });
    refresh();
}

void AuditPanelModel::setActive(bool active)
{
    m_active = active;
    if (active && m_dirty) {
        refresh();
    }
}

void AuditPanelModel::scheduleRefresh()
{
    if (!m_active) {
        m_dirty = true;
        return;
    }
    m_timer.start();
}

void AuditPanelModel::recheck()
{
    refresh();
}

void AuditPanelModel::refresh()
{
    m_dirty = false;
    m_report = starScore()->hasScore() ? starScore()->audit() : StarScoreAuditReport();
    m_arrangements = starScore()->hasScore() ? starScore()->arrangements() : std::vector<StarScoreArrangement>();

    if (!m_arrangementId.isEmpty()
        && std::none_of(m_arrangements.begin(), m_arrangements.end(), [&](const StarScoreArrangement& a) { return a.id == m_arrangementId; })) {
        m_arrangementId.clear();
    }

    const bool stepExists = std::any_of(m_report.listen.begin(), m_report.listen.end(),
                                        [&](const StarScoreListenStep& s) { return s.key == m_currentStepKey; });
    if (!stepExists) {
        // the music of the step changed (or first time): the first step not approved yet
        const int i = firstUnapproved(0);
        m_currentStepKey = i >= 0 ? m_report.listen[i].key
                           : (m_report.listen.empty() ? QString() : m_report.listen.front().key);
    }
    emit changed();
}

// ---------------------------------------------------------------------------
//  Checks
// ---------------------------------------------------------------------------

std::vector<int> AuditPanelModel::filteredIssueIndexes() const
{
    QStringList sectionIds;
    for (const StarScoreArrangement& a : m_arrangements) {
        if (a.id == m_arrangementId) {
            sectionIds = a.sectionIds;
        }
    }
    std::vector<int> out;
    for (int i = 0; i < int(m_report.issues.size()); ++i) {
        const StarScoreAuditIssue& issue = m_report.issues[i];
        if (issue.severity < 1 && !m_showMinor) {
            continue;
        }
        if (issue.intentional && !m_showIntentional) {
            continue;
        }
        if (!m_arrangementId.isEmpty()
            && std::none_of(issue.sectionIds.begin(), issue.sectionIds.end(), [&](const QString& s) { return sectionIds.contains(s); })) {
            continue;
        }
        out.push_back(i);
    }
    return out;
}

QVariantList AuditPanelModel::issues() const
{
    QVariantList list;
    for (int i : filteredIssueIndexes()) {
        const StarScoreAuditIssue& issue = m_report.issues[i];
        list << QVariantMap {
            { "key", issue.key },
            { "bars", auditBarsText(issue.bar, issue.endBar) },
            { "check", auditCheckLabel(issue.check) },
            { "title", issue.title },
            { "part", issue.partLabel },
            { "message", issue.message },
            { "severity", issue.severity },
            { "intentional", issue.intentional },
        };
    }
    return list;
}

int AuditPanelModel::currentIssue() const
{
    const std::vector<int> idx = filteredIssueIndexes();
    for (int i = 0; i < int(idx.size()); ++i) {
        if (m_report.issues[idx[i]].key == m_currentIssueKey) {
            return i;
        }
    }
    return -1;
}

QString AuditPanelModel::summary() const
{
    if (!starScore()->hasScore()) {
        return muse::qtrc("starscore", "Open a score to audit it.");
    }
    QStringList sectionIds;
    for (const StarScoreArrangement& a : m_arrangements) {
        if (a.id == m_arrangementId) {
            sectionIds = a.sectionIds;
        }
    }
    int open = 0;
    int likely = 0;
    int minor = 0;
    int intentional = 0;
    for (const StarScoreAuditIssue& issue : m_report.issues) {
        if (!m_arrangementId.isEmpty()
            && std::none_of(issue.sectionIds.begin(), issue.sectionIds.end(), [&](const QString& s) { return sectionIds.contains(s); })) {
            continue;
        }
        if (issue.intentional) {
            ++intentional;
        } else if (issue.severity < 1) {
            ++minor;
        } else {
            ++open;
            likely += issue.severity == 2 ? 1 : 0;
        }
    }
    if (open == 0 && minor == 0) {
        return intentional == 0 ? muse::qtrc("starscore", "Nothing to look at.")
               : muse::qtrc("starscore", "Nothing left to look at (%1 marked intentional).").arg(intentional);
    }
    QString text = muse::qtrc("starscore", "%n to look at", nullptr, open);
    if (likely > 0) {
        text += " " + muse::qtrc("starscore", "(%n likely error(s))", nullptr, likely);
    }
    QStringList extra;
    if (minor > 0) {
        extra << muse::qtrc("starscore", "%n minor", nullptr, minor);
    }
    if (intentional > 0) {
        extra << muse::qtrc("starscore", "%n intentional", nullptr, intentional);
    }
    return extra.isEmpty() ? text : text + " · " + extra.join(" · ");
}

QVariantList AuditPanelModel::arrangementChoices() const
{
    QVariantList list;
    list << QVariantMap { { "text", muse::qtrc("starscore", "All arrangements") }, { "value", QString() } };
    for (const StarScoreAuditArrangementState& a : m_report.arrangements) {
        QString text = a.name;
        if (a.audited && !a.changedSinceAudit) {
            text += "  ✓";
        } else if (a.audited) {
            text += "  " + muse::qtrc("starscore", "(changed since audited)");
        } else if (a.openIssues > 0) {
            text += QString("  (%1)").arg(a.openIssues);
        }
        list << QVariantMap { { "text", text }, { "value", a.id } };
    }
    return list;
}

QString AuditPanelModel::arrangementId() const
{
    return m_arrangementId;
}

QString AuditPanelModel::arrangementStatus() const
{
    for (const StarScoreAuditArrangementState& a : m_report.arrangements) {
        if (a.id != m_arrangementId) {
            continue;
        }
        if (a.audited && a.changedSinceAudit) {
            return muse::qtrc("starscore", "Audited on %1, but its music has changed since.").arg(a.auditedDate);
        }
        if (a.audited) {
            return muse::qtrc("starscore", "Audited on %1.").arg(a.auditedDate);
        }
        return a.openIssues == 0 ? muse::qtrc("starscore", "Not audited yet. Nothing left to look at.")
               : muse::qtrc("starscore", "Not audited yet. %n left to look at.", nullptr, a.openIssues);
    }
    return QString();
}

bool AuditPanelModel::arrangementAudited() const
{
    for (const StarScoreAuditArrangementState& a : m_report.arrangements) {
        if (a.id == m_arrangementId) {
            return a.audited && !a.changedSinceAudit;
        }
    }
    return false;
}

QVariantList AuditPanelModel::referenceChoices() const
{
    QVariantList list;
    for (int i = 0; i < m_report.referenceChoiceIds.size(); ++i) {
        list << QVariantMap { { "text", m_report.referenceChoiceNames.value(i) }, { "value", m_report.referenceChoiceIds.at(i) } };
    }
    return list;
}

QString AuditPanelModel::referenceId() const
{
    return m_report.referenceSectionId;
}

bool AuditPanelModel::showMinor() const
{
    return m_showMinor;
}

bool AuditPanelModel::showIntentional() const
{
    return m_showIntentional;
}

bool AuditPanelModel::hasScore() const
{
    return starScore()->hasScore();
}

void AuditPanelModel::selectIssue(int index)
{
    const std::vector<int> idx = filteredIssueIndexes();
    if (index < 0 || index >= int(idx.size())) {
        return;
    }
    const StarScoreAuditIssue issue = m_report.issues[idx[index]];
    m_currentIssueKey = issue.key;
    emit changed();
    starScore()->showAuditIssue(issue);
}

void AuditPanelModel::nextIssue()
{
    const int count = int(filteredIssueIndexes().size());
    if (count == 0) {
        return;
    }
    const int cur = currentIssue();
    selectIssue(cur < 0 ? 0 : (cur + 1) % count);
}

void AuditPanelModel::previousIssue()
{
    const int count = int(filteredIssueIndexes().size());
    if (count == 0) {
        return;
    }
    const int cur = currentIssue();
    selectIssue(cur <= 0 ? count - 1 : cur - 1);
}

void AuditPanelModel::setIntentional(int index, bool intentional)
{
    const std::vector<int> idx = filteredIssueIndexes();
    if (index < 0 || index >= int(idx.size())) {
        return;
    }
    StarScoreAuditIssue& issue = m_report.issues[idx[index]];
    issue.intentional = intentional;
    starScore()->setAuditIntentional(issue.key, intentional);
    emit changed();
}

void AuditPanelModel::setArrangementId(const QString& id)
{
    m_arrangementId = id;
    emit changed();
}

void AuditPanelModel::setReferenceId(const QString& id)
{
    starScore()->setAuditReferenceSection(id);
    refresh();
}

void AuditPanelModel::setShowMinor(bool show)
{
    m_showMinor = show;
    emit changed();
}

void AuditPanelModel::setShowIntentional(bool show)
{
    m_showIntentional = show;
    emit changed();
}

void AuditPanelModel::markArrangementAudited(bool audited)
{
    if (m_arrangementId.isEmpty()) {
        return;
    }
    if (audited) {
        for (const StarScoreAuditArrangementState& a : m_report.arrangements) {
            if (a.id != m_arrangementId || a.openIssues == 0) {
                continue;
            }
            constexpr int Mark = static_cast<int>(muse::IInteractive::Button::CustomButton) + 1;
            constexpr int Cancel = static_cast<int>(muse::IInteractive::Button::CustomButton) + 2;
            const muse::IInteractive::Result answer = interactive()->questionSync(
                muse::trc("starscore", "Mark as audited?"),
                muse::qtrc("starscore", "“%1” still has %n thing(s) to look at. Mark it as audited anyway?", nullptr, a.openIssues)
                .arg(a.name).toStdString(), {
                muse::IInteractive::ButtonData(Cancel, muse::trc("starscore", "Cancel"), true),
                muse::IInteractive::ButtonData(Mark, muse::trc("starscore", "Mark as audited")),
            }, Cancel);
            if (answer.button() != Mark) {
                return;
            }
        }
    }
    starScore()->setArrangementAudited(m_arrangementId, audited);
    refresh();
}

void AuditPanelModel::openLibrary()
{
    dispatcher()->dispatch("starscore-audit-library");
}

// ---------------------------------------------------------------------------
//  Listen-through
// ---------------------------------------------------------------------------

static bool auditSameGroup(const StarScoreListenStep& a, const StarScoreListenStep& b)
{
    return a.startBar == b.startBar && a.endBar == b.endBar && a.rehearsal == b.rehearsal;
}

static int auditStepIndex(const std::vector<StarScoreListenStep>& steps, const QString& key)
{
    for (int i = 0; i < int(steps.size()); ++i) {
        if (steps[i].key == key) {
            return i;
        }
    }
    return -1;
}

int AuditPanelModel::firstUnapproved(int from) const
{
    for (int i = std::max(0, from); i < int(m_report.listen.size()); ++i) {
        if (!m_report.listen[i].approved) {
            return i;
        }
    }
    return -1;
}

std::vector<int> AuditPanelModel::stepsOfCurrentGroup() const
{
    std::vector<int> out;
    const int cur = auditStepIndex(m_report.listen, m_currentStepKey);
    if (cur < 0) {
        return out;
    }
    for (int i = 0; i < int(m_report.listen.size()); ++i) {
        if (auditSameGroup(m_report.listen[i], m_report.listen[cur])) {
            out.push_back(i);
        }
    }
    return out;
}

QVariantList AuditPanelModel::steps() const
{
    QVariantList list;
    for (int i : stepsOfCurrentGroup()) {
        const StarScoreListenStep& s = m_report.listen[i];
        list << QVariantMap { { "text", s.sectionName }, { "approved", s.approved }, { "key", s.key } };
    }
    return list;
}

int AuditPanelModel::currentStep() const
{
    const std::vector<int> group = stepsOfCurrentGroup();
    for (int i = 0; i < int(group.size()); ++i) {
        if (m_report.listen[group[i]].key == m_currentStepKey) {
            return i;
        }
    }
    return -1;
}

QString AuditPanelModel::rehearsalTitle() const
{
    const int cur = auditStepIndex(m_report.listen, m_currentStepKey);
    if (cur < 0) {
        return QString();
    }
    const StarScoreListenStep& s = m_report.listen[cur];
    const QString bars = auditBarsText(s.startBar, s.endBar);
    const bool isMark = s.rehearsal != muse::qtrc("starscore", "Start") && s.rehearsal != muse::qtrc("starscore", "Whole song");
    return isMark ? muse::qtrc("starscore", "Rehearsal mark %1 · %2").arg(s.rehearsal, bars) : QString("%1 · %2").arg(s.rehearsal, bars);
}

QString AuditPanelModel::listenProgress() const
{
    int approved = 0;
    int groups = 0;
    int groupsDone = 0;
    for (int i = 0; i < int(m_report.listen.size()); ++i) {
        const StarScoreListenStep& s = m_report.listen[i];
        approved += s.approved ? 1 : 0;
        if (i == 0 || !auditSameGroup(s, m_report.listen[i - 1])) {
            ++groups;
            bool done = true;
            for (int j = i; j < int(m_report.listen.size()) && auditSameGroup(m_report.listen[j], s); ++j) {
                done = done && m_report.listen[j].approved;
            }
            groupsDone += done ? 1 : 0;
        }
    }
    return muse::qtrc("starscore", "%1 of %2 rehearsal sections done · %3 of %4 listens approved")
           .arg(groupsDone).arg(groups).arg(approved).arg(m_report.listen.size());
}

bool AuditPanelModel::listenDone() const
{
    return !m_report.listen.empty() && firstUnapproved(0) < 0;
}

bool AuditPanelModel::listening() const
{
    return starScore()->isListening();
}

bool AuditPanelModel::withRhythm() const
{
    return m_withRhythm;
}

bool AuditPanelModel::autoPlay() const
{
    return m_autoPlay;
}

bool AuditPanelModel::hasListenSteps() const
{
    return !m_report.listen.empty();
}

void AuditPanelModel::goToStep(int allIndex, bool play)
{
    if (allIndex < 0 || allIndex >= int(m_report.listen.size())) {
        return;
    }
    m_currentStepKey = m_report.listen[allIndex].key;
    emit changed();
    if (play) {
        starScore()->playListenStep(m_report.listen[allIndex], m_withRhythm);
    }
}

void AuditPanelModel::selectStep(int index)
{
    const std::vector<int> group = stepsOfCurrentGroup();
    if (index >= 0 && index < int(group.size())) {
        goToStep(group[index], starScore()->isListening());
    }
}

void AuditPanelModel::playStep()
{
    goToStep(auditStepIndex(m_report.listen, m_currentStepKey), true);
}

void AuditPanelModel::stopPlaying()
{
    dispatcher()->dispatch("stop");
}

void AuditPanelModel::approveStep()
{
    const int cur = auditStepIndex(m_report.listen, m_currentStepKey);
    if (cur < 0) {
        return;
    }
    m_report.listen[cur].approved = true;
    starScore()->setListenApproved(m_report.listen[cur].key, true);

    int next = firstUnapproved(cur + 1);
    if (next < 0) {
        next = firstUnapproved(0);
    }
    if (next < 0) {
        // everything has been listened to
        starScore()->stopListening();
        emit changed();
        return;
    }
    goToStep(next, m_autoPlay);
}

void AuditPanelModel::unapproveStep(int index)
{
    const std::vector<int> group = stepsOfCurrentGroup();
    if (index < 0 || index >= int(group.size())) {
        return;
    }
    StarScoreListenStep& s = m_report.listen[group[index]];
    s.approved = false;
    starScore()->setListenApproved(s.key, false);
    emit changed();
}

void AuditPanelModel::skipStep()
{
    const int cur = auditStepIndex(m_report.listen, m_currentStepKey);
    if (cur >= 0 && cur + 1 < int(m_report.listen.size())) {
        goToStep(cur + 1, m_autoPlay && starScore()->isListening());
    }
}

void AuditPanelModel::previousStep()
{
    const int cur = auditStepIndex(m_report.listen, m_currentStepKey);
    if (cur > 0) {
        goToStep(cur - 1, m_autoPlay && starScore()->isListening());
    }
}

void AuditPanelModel::previousRehearsal()
{
    const int cur = auditStepIndex(m_report.listen, m_currentStepKey);
    if (cur < 0) {
        return;
    }
    int i = cur;
    while (i > 0 && auditSameGroup(m_report.listen[i - 1], m_report.listen[cur])) {
        --i;   // start of this group
    }
    if (i == 0) {
        return;
    }
    int j = i - 1;
    while (j > 0 && auditSameGroup(m_report.listen[j - 1], m_report.listen[i - 1])) {
        --j;   // start of the previous group
    }
    goToStep(j, false);
}

void AuditPanelModel::nextRehearsal()
{
    const int cur = auditStepIndex(m_report.listen, m_currentStepKey);
    if (cur < 0) {
        return;
    }
    int i = cur;
    while (i < int(m_report.listen.size()) && auditSameGroup(m_report.listen[i], m_report.listen[cur])) {
        ++i;
    }
    if (i < int(m_report.listen.size())) {
        goToStep(i, false);
    }
}

void AuditPanelModel::finishListening()
{
    starScore()->stopListening();
}

void AuditPanelModel::setWithRhythm(bool on)
{
    m_withRhythm = on;
    emit changed();
}

void AuditPanelModel::setAutoPlay(bool on)
{
    m_autoPlay = on;
    emit changed();
}

bool AuditPanelModel::walkActive() const
{
    return starScore()->auditWalkActive();
}

QString AuditPanelModel::walkText() const
{
    const QStringList paths = starScore()->auditWalkPaths();
    if (paths.isEmpty()) {
        return QString();
    }
    const int i = starScore()->auditWalkIndex();
    return muse::qtrc("starscore", "Song %1 of %2 · %3").arg(i + 1).arg(paths.size()).arg(QFileInfo(paths.at(i)).completeBaseName());
}

bool AuditPanelModel::walkHasPrevious() const
{
    return starScore()->auditWalkIndex() > 0;
}

bool AuditPanelModel::walkIsLast() const
{
    return starScore()->auditWalkIndex() >= int(starScore()->auditWalkPaths().size()) - 1;
}

void AuditPanelModel::walkNext()
{
    if (starScore()->isListening()) {
        starScore()->stopListening();
    }
    starScore()->auditWalkStep(1);
    emit changed();
}

void AuditPanelModel::walkPrevious()
{
    if (starScore()->isListening()) {
        starScore()->stopListening();
    }
    starScore()->auditWalkStep(-1);
    emit changed();
}

void AuditPanelModel::walkStop()
{
    starScore()->stopAuditWalk();
    emit changed();
}
