/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#include "addsolomodel.h"

#include <QFileInfo>

#include "translation.h"

using namespace mu::project;

AddSoloModel::AddSoloModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void AddSoloModel::setName(const QString& n)
{
    m_name = n;
}

void AddSoloModel::setStartBar(int b)
{
    if (m_startBar != b) {
        m_startBar = b;
        updatePlan();
    }
}

void AddSoloModel::setEndBar(int b)
{
    if (m_endBar != b) {
        m_endBar = b;
        updatePlan();
    }
}

bool AddSoloModel::canAdd() const
{
    return !m_filePath.isEmpty() && m_planOk;
}

void AddSoloModel::chooseFile()
{
    const QFileInfo mainFile(starScore()->mainProjectPath().toQString());
    const muse::io::path_t path = interactive()->selectOpeningFileSync(
        muse::trc("starscore", "Choose a solo transcription"), muse::io::path_t(mainFile.absolutePath()),
        { muse::trc("project", "MuseScore files") + " (*.mscz *.mscx)" });
    if (path.empty()) {
        return;
    }
    m_filePath = path.toQString();
    if (m_name.trimmed().isEmpty()) {
        m_name = QFileInfo(m_filePath).completeBaseName();
    }
    updatePlan();
}

void AddSoloModel::updatePlan()
{
    m_summary.clear();
    m_warning.clear();
    m_planOk = false;

    if (!m_filePath.isEmpty()) {
        muse::RetVal<StarScoreSoloPlan> plan = starScore()->planSolo(m_filePath, m_startBar, m_endBar);
        if (!plan.ret) {
            m_warning = muse::qtrc("starscore", "Couldn't read that file: %1").arg(QString::fromStdString(plan.ret.toString()));
        } else {
            m_summary = plan.val.summary;
            m_warning = plan.val.warning;
            m_planOk = !m_summary.isEmpty();
        }
    }

    emit changed();
}

QString AddSoloModel::add()
{
    muse::RetVal<QString> ret = starScore()->addSolo(m_filePath, m_name, m_startBar, m_endBar);
    if (!ret.ret) {
        return QString::fromStdString(ret.ret.text().empty() ? ret.ret.toString() : ret.ret.text());
    }
    starScore()->showSolo(ret.val);
    return QString();
}
