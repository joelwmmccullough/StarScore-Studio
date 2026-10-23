/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — practice flags (Joel, 23 Oct 2026): on a part (the Keys sheet), select bars and flag them for
 * practice as a solo, comping, or written part, with a difficulty (1 easy to 4 virtuosic) and his own notes. Nothing
 * shows on the sheet. The flags are kept in the song (metaTag "starscoreFlags") and become practice items of the
 * song's library (see starscorepractice.cpp), and they are read when its practice library is written.
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"

namespace mu::project {
class PracticeFlagModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList flags READ flags NOTIFY changed)
    Q_PROPERTY(int fromBar READ fromBar WRITE setFromBar NOTIFY changed)
    Q_PROPERTY(int toBar READ toBar WRITE setToBar NOTIFY changed)
    Q_PROPERTY(QString partName READ partName NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit PracticeFlagModel(QObject* parent = nullptr);

    QVariantList flags() const;
    int fromBar() const { return m_from; }
    void setFromBar(int b);
    int toBar() const { return m_to; }
    void setToBar(int b);
    QString partName() const { return m_part; }
    QString message() const { return m_message; }

    //! The bars selected in the score on screen (a part or the full score)
    Q_INVOKABLE void useSelection();
    //! kind: "solo", "comping", or "parts"; difficulty 1-4. id empty: a new flag
    Q_INVOKABLE bool save(const QString& id, const QString& kind, int difficulty, const QString& text);
    Q_INVOKABLE void remove(const QString& id);

signals:
    void changed();

private:
    QJsonArray read() const;
    void write(const QJsonArray& flags, const QString& undoText);

    int m_from = 0;
    int m_to = 0;
    QString m_part;
    QString m_message;
};
}
