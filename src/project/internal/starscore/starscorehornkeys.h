/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Standard horn sections' template keys.
 *
 * The Standard horn sections are "1-horn" … "7-horn", and since 1.18.51 also the two-trumpet 4-Horn "4-horn-2tpt"
 * (Joel, 16 Oct 2026: Trumpet 1, Trumpet 2, the doubler in the alto chair, Tenor Sax). Its arrangement has the same
 * key, as the Flexible ones do ("3-horn-any"); the other Standard arrangements are "<n>-horn-standard".
 *
 * Code that asked "is this a Standard horn section" with a "^[1-7]-horn$" pattern asks hornSectionSize() instead, so
 * the two-trumpet 4-Horn is one too.
 */
#pragma once

#include <QRegularExpression>
#include <QString>

namespace mu::project::starscore {
//! The two-trumpet 4-Horn: its section's and its arrangement's key
inline const QString TWO_TRUMPET_4H = QStringLiteral("4-horn-2tpt");

//! How many horns a Standard horn section has ("5-horn" -> 5, "4-horn-2tpt" -> 4); 0 for any other section
//! (Flexible, lead sheet, rhythm, big band, custom…)
inline int hornSectionSize(const QString& sectionKey)
{
    static const QRegularExpression re("^([1-9])-horn$");
    const QRegularExpressionMatch m = re.match(sectionKey);
    if (m.hasMatch()) {
        return m.captured(1).toInt();
    }
    return sectionKey == TWO_TRUMPET_4H ? 4 : 0;
}

//! The Standard horn section of a Standard arrangement ("3-horn-standard" -> "3-horn", "4-horn-2tpt" -> itself);
//! empty for any other arrangement
inline QString hornSectionOfArrangement(const QString& arrangementKey)
{
    if (arrangementKey == TWO_TRUMPET_4H) {
        return arrangementKey;
    }
    static const QRegularExpression re("^([1-9])-horn-standard$");
    const QRegularExpressionMatch m = re.match(arrangementKey);
    return m.hasMatch() ? m.captured(1) + "-horn" : QString();
}

//! How many horns a Standard arrangement has ("5-horn-standard" -> 5, "4-horn-2tpt" -> 4); 0 for any other
inline int standardArrangementSize(const QString& arrangementKey)
{
    return hornSectionSize(hornSectionOfArrangement(arrangementKey));
}

//! The chair a Standard horn section's doubler takes: the alto in 3/4/5-Horn (and the two-trumpet 4-Horn), the
//! soprano in 6/7-Horn; empty for 1/2-Horn and anything else
inline QString doublerChairOf(const QString& sectionKey)
{
    const int n = hornSectionSize(sectionKey);
    return n >= 3 && n <= 5 ? QStringLiteral("alto-saxophone") : n >= 6 ? QStringLiteral("soprano-saxophone") : QString();
}

//! What the part names of a Standard or Flexible horn section start with: "3H", "3H Flexible", "4H 2-Trumpet";
//! empty for any other section
inline QString hornPartPrefix(const QString& sectionKey)
{
    if (sectionKey == TWO_TRUMPET_4H) {
        return QStringLiteral("4H 2-Trumpet");
    }
    static const QRegularExpression re("^(\\d)-horn(-any)?$");
    const QRegularExpressionMatch m = re.match(sectionKey);
    if (!m.hasMatch()) {
        return QString();
    }
    return m.captured(1) + "H" + (m.captured(2).isEmpty() ? QString() : QStringLiteral(" Flexible"));
}
}
