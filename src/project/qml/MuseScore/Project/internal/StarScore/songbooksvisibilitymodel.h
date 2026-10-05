/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 */
#pragma once

#include <QObject>
#include <qqmlintegration.h>

#include "async/asyncable.h"

namespace mu::project {
//! Whether Home shows the Songbooks page. Off by default, so bandmates using StarScore don't see it; turned on in
//! Preferences › General › Songbooks. Kept in this computer's settings. Every instance follows a change at once (the
//! Home menu updates while Preferences is open).
class SongbooksVisibilityModel : public QObject, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY shownChanged)

    QML_ELEMENT

public:
    explicit SongbooksVisibilityModel(QObject* parent = nullptr);

    bool shown() const;
    void setShown(bool shown);

signals:
    void shownChanged();
};
}
