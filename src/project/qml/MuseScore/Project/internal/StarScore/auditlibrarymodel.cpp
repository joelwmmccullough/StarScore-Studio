/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Audit library: every .starscore in the projects folder, and what's left to audit in each
 */
#include "auditlibrarymodel.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QCoreApplication>
#include <QUrl>

#include "translation.h"

using namespace mu::project;

AuditLibraryModel::AuditLibraryModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void AuditLibraryModel::load()
{
    m_folder = starScore()->auditLibraryFolder();
    m_results = starScore()->cachedLibraryAudit(m_folder);
    sortResults();
    emit changed();
}

QString AuditLibraryModel::folder() const
{
    return m_folder;
}

void AuditLibraryModel::sortResults()
{
    // Most to look at first; songs that are done at the bottom
    std::stable_sort(m_results.begin(), m_results.end(), [](const StarScoreAuditFileSummary& a, const StarScoreAuditFileSummary& b) {
        const bool doneA = a.openIssues == 0 && a.arrangementsAudited == a.arrangements && a.listenApproved == a.listenSteps;
        const bool doneB = b.openIssues == 0 && b.arrangementsAudited == b.arrangements && b.listenApproved == b.listenSteps;
        if (doneA != doneB) {
            return !doneA;
        }
        if (a.likelyErrors != b.likelyErrors) {
            return a.likelyErrors > b.likelyErrors;
        }
        if (a.openIssues != b.openIssues) {
            return a.openIssues > b.openIssues;
        }
        return QString::localeAwareCompare(a.title, b.title) < 0;
    });
}

QVariantList AuditLibraryModel::songs() const
{
    QVariantList list;
    const QDir root(m_folder);
    for (const StarScoreAuditFileSummary& s : m_results) {
        const bool done = s.error.isEmpty() && s.openIssues == 0 && s.arrangementsAudited == s.arrangements
                          && s.listenApproved == s.listenSteps;
        QString arrangements = muse::qtrc("starscore", "%1 of %2 audited").arg(s.arrangementsAudited).arg(s.arrangements);
        if (s.arrangementsChanged > 0) {
            arrangements += " · " + muse::qtrc("starscore", "%n changed since", nullptr, s.arrangementsChanged);
        }
        list << QVariantMap {
            { "title", s.title },
            { "folder", QFileInfo(root.relativeFilePath(s.path)).path() },
            { "issues", s.error.isEmpty()
              ? (s.openIssues == 0 ? muse::qtrc("starscore", "nothing to look at")
                 : muse::qtrc("starscore", "%n to look at", nullptr, s.openIssues)
                 + (s.likelyErrors > 0 ? " " + muse::qtrc("starscore", "(%n likely error(s))", nullptr, s.likelyErrors) : QString()))
              : s.error },
            { "likely", s.likelyErrors },
            { "arrangements", arrangements },
            { "listen", s.listenSteps == 0 ? muse::qtrc("starscore", "no horn listens")
              : muse::qtrc("starscore", "%1 of %2 listens approved").arg(s.listenApproved).arg(s.listenSteps) },
            { "done", done },
            { "error", !s.error.isEmpty() },
        };
    }
    return list;
}

bool AuditLibraryModel::scanning() const
{
    return m_scanning;
}

QString AuditLibraryModel::status() const
{
    if (m_folder.isEmpty()) {
        return muse::qtrc("starscore", "Choose the folder that holds your .starscore files (for example “Projects and Sheets”).");
    }
    if (m_scanning) {
        return muse::qtrc("starscore", "Checking %1 of %2…").arg(m_total - m_queue.size()).arg(m_total);
    }
    int done = 0;
    for (const StarScoreAuditFileSummary& s : m_results) {
        done += (s.error.isEmpty() && s.openIssues == 0 && s.arrangementsAudited == s.arrangements
                 && s.listenApproved == s.listenSteps) ? 1 : 0;
    }
    if (m_results.empty()) {
        return muse::qtrc("starscore", "Press Check to audit every song in this folder.");
    }
    return muse::qtrc("starscore", "%1 of %2 songs fully audited").arg(done).arg(m_results.size());
}

void AuditLibraryModel::chooseFolder()
{
    const muse::io::path_t dir = interactive()->selectDirectory(
        muse::trc("starscore", "Choose the folder with your .starscore files"),
        muse::io::path_t(m_folder.isEmpty() ? QDir::homePath() : m_folder));
    if (dir.empty()) {
        return;
    }
    m_folder = dir.toQString();
    starScore()->setAuditLibraryFolder(m_folder);
    m_results = starScore()->cachedLibraryAudit(m_folder);
    sortResults();
    emit changed();
}

void AuditLibraryModel::scan(bool force)
{
    if (m_folder.isEmpty() || m_scanning) {
        return;
    }
    m_queue = starScore()->auditLibraryFiles(m_folder);
    m_total = m_queue.size();
    m_force = force;
    m_results.clear();
    m_scanning = true;
    emit changed();
    QTimer::singleShot(0, this, [this]() { scanNext(); });
}

void AuditLibraryModel::scanNext()
{
    if (!m_scanning) {
        return;
    }
    if (m_queue.isEmpty()) {
        m_scanning = false;
        sortResults();
        emit changed();
        return;
    }
    const QString path = m_queue.takeFirst();
    m_results.push_back(starScore()->auditFile(path, m_force));
    emit changed();
    // one file at a time, so the window stays responsive
    QTimer::singleShot(10, this, [this]() { scanNext(); });
}

void AuditLibraryModel::cancel()
{
    m_scanning = false;
    m_queue.clear();
    sortResults();
    emit changed();
}

void AuditLibraryModel::openSong(int index)
{
    if (index < 0 || index >= int(m_results.size())) {
        return;
    }
    const QString path = m_results[index].path;
    // Stop scanning without touching the list: this runs inside a click on one of the list's rows
    m_scanning = false;
    m_queue.clear();

    // Everything else happens after the click has finished. Closing the dialog deletes this model, so the
    // file is opened (and the Audit panel shown) by timers that don't use it.
    auto d = dispatcher();
    auto docks = dockWindowProvider();
    QTimer::singleShot(0, qApp, [d, path]() {
        d->dispatch("starscore-audit-open", muse::actions::ActionData::make_arg1<QUrl>(QUrl::fromLocalFile(path)));
    });
    QTimer::singleShot(2000, qApp, [d, docks]() {
        if (docks && docks->window() && !docks->window()->isDockOpen("starscoreAuditPanel")) {
            d->dispatch("toggle-starscore-audit");
        }
    });
    QTimer::singleShot(0, this, [this]() { emit closeRequested(); });
}

void AuditLibraryModel::auditAll()
{
    QStringList paths;
    for (const StarScoreAuditFileSummary& s : m_results) {
        const bool done = s.error.isEmpty() && s.openIssues == 0 && s.arrangementsAudited == s.arrangements
                          && s.listenApproved == s.listenSteps;
        if (!done) {
            paths << s.path;
        }
    }
    if (m_results.empty()) {
        // Nothing checked yet: go through every song in the folder
        paths = starScore()->auditLibraryFiles(m_folder);
    }
    if (paths.isEmpty()) {
        interactive()->info(muse::qtrc("starscore", "Audit all songs").toStdString(),
                            muse::qtrc("starscore", "Every song is fully audited.").toStdString());
        return;
    }
    m_scanning = false;
    m_queue.clear();

    // As in openSong: the dialog (and this model) closes before the first song opens
    auto ss = starScore();
    QTimer::singleShot(0, qApp, [ss, paths]() {
        if (ss) {
            ss->startAuditWalk(paths);
        }
    });
    QTimer::singleShot(0, this, [this]() { emit closeRequested(); });
}
