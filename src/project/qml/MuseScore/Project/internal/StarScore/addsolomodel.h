/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QObject>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "iinteractive.h"
#include "project/istarscoreservice.h"

namespace mu::project {
//! "Add solo transcription": pick a .mscz, say where it starts in the song, check the plan, add it.
class AddSoloModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString filePath READ filePath NOTIFY changed)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(int startBar READ startBar WRITE setStartBar NOTIFY changed)
    Q_PROPERTY(int endBar READ endBar WRITE setEndBar NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QString warning READ warning NOTIFY changed)
    Q_PROPERTY(bool canAdd READ canAdd NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<IStarScoreService> starScore = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit AddSoloModel(QObject* parent = nullptr);

    QString filePath() const { return m_filePath; }
    QString name() const { return m_name; }
    void setName(const QString& n);
    int startBar() const { return m_startBar; }
    void setStartBar(int b);
    int endBar() const { return m_endBar; }
    void setEndBar(int b);
    QString summary() const { return m_summary; }
    QString warning() const { return m_warning; }
    bool canAdd() const;

    Q_INVOKABLE void chooseFile();
    //! Returns an error message, or an empty string when the solo was added (and is now showing)
    Q_INVOKABLE QString add();

signals:
    void changed();

private:
    void updatePlan();

    QString m_filePath;
    QString m_name;
    int m_startBar = 1;
    int m_endBar = 0;     // 0 = work it out
    QString m_summary;
    QString m_warning;
    bool m_planOk = false;
};
}
