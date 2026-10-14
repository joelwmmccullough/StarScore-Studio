/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the one table of the part statuses: their colours and names.
 *
 * The statuses are numbered as StarScoreStatus (istarscoreservice.h): Empty 0, Sketch 1, In progress 2, Written
 * unformatted 3, Needs review 4, Finished 5. Their dot colours and names were repeated in the StarScore bar, the
 * to-do list and its PDF, the dashboard, the audit, the part score tabs and two QML files; when "Written,
 * unformatted" was added (1.18.38), each copy had to change. Header-only, so the part score tabs (notationscene,
 * which doesn't link the project module) can use it too.
 */
#pragma once

#include <QString>
#include <QVariantList>

#include "translation.h"

namespace mu::project::starscore {
//! How many statuses there are (Empty … Finished)
constexpr int STATUS_COUNT = 6;

//! The status's dot colour ("#8A8A8A" grey for Empty … "#3FB05A" green for Finished); grey for a number out of range
inline const char* statusColour(int status)
{
    static const char* const COLOURS[STATUS_COUNT] = { "#8A8A8A", "#E0463A", "#F29B30", "#E3C02B", "#3C8CE7", "#3FB05A" };
    return status >= 0 && status < STATUS_COUNT ? COLOURS[status] : COLOURS[0];
}

//! The status's name ("Empty", "Sketch", "In progress", "Written, unformatted", "Needs review", "Finished"),
//! translated; empty for a number out of range
inline QString statusName(int status)
{
    switch (status) {
    case 0: return muse::qtrc("starscore", "Empty");
    case 1: return muse::qtrc("starscore", "Sketch");
    case 2: return muse::qtrc("starscore", "In progress");
    case 3: return muse::qtrc("starscore", "Written, unformatted");
    case 4: return muse::qtrc("starscore", "Needs review");
    case 5: return muse::qtrc("starscore", "Finished");
    }
    return QString();
}

//! The colours in status order, for QML (StarScoreDashboard.qml and StarScoreTodoPanel.qml index them by status)
inline QVariantList statusColours()
{
    QVariantList out;
    for (int i = 0; i < STATUS_COUNT; ++i) {
        out << QString::fromLatin1(statusColour(i));
    }
    return out;
}
}
