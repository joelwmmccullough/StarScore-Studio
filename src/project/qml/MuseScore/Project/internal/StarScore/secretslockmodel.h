/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QObject>
#include <qqmlintegration.h>

namespace mu::project {
//! Home › Joel's Secrets: the password in front of the Songbooks page. The password is chosen the first time the page
//! is opened and kept as a salted SHA-256 hash in this computer's settings; once entered, the page stays open until
//! StarScore quits. A lock against opening the page by accident or by someone else on this computer, not encryption:
//! the songbook files themselves are as readable as before.
class SecretsLockModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool hasPassword READ hasPassword NOTIFY changed)
    Q_PROPERTY(bool unlocked READ unlocked NOTIFY changed)

    QML_ELEMENT

public:
    explicit SecretsLockModel(QObject* parent = nullptr);

    bool hasPassword() const;
    bool unlocked() const;

    //! First time: sets the password (at least 4 characters) and unlocks. Returns "" or what was wrong.
    Q_INVOKABLE QString setPassword(const QString& password, const QString& again);
    //! Unlocks when the password is right. Returns whether it was.
    Q_INVOKABLE bool unlock(const QString& password);
    //! Locks the page again (until the password is entered).
    Q_INVOKABLE void lock();

signals:
    void changed();
};
}
