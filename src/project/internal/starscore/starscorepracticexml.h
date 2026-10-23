/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice (Joel, 23 Oct 2026): cutting an exercise's MusicXML into what the runner plays.
 *
 * An exercise is one MusicXML score: the keys part (P1, or the lead and keys of a song excerpt) and backing parts
 * (P2 bass and P3 drums from 1.19's libraries; B1, B2, ... from 1.20's: bass, drums, a pad). An item's parts each
 * cover some of its bars. From those the runner makes:
 *   - an example: the part's bars, every part, at today's tempo (to hear how it goes);
 *   - a play-along: the part's bars over and over with the backing parts only (his notes are on the runner's screen,
 *     he plays them), in passes that step from today's tempo up to the goal tempo, long enough for the part's minutes.
 */
#pragma once

#include <QList>
#include <QString>

namespace mu::project::practice {
//! A backing part's id (bass, drums, pad: played along to, never shown in the runner's notation)
bool isBackingPartId(const QString& id);

//! Whether the exercise has any backing part
bool hasBacking(const QString& musicXml);

//! The exercise's number of bars (in its first part)
int barCount(const QString& musicXml);

//! Bars from-to (1-based, 0 = all) of the exercise: every part, or only the backing parts
QString sliceExercise(const QString& musicXml, int from, int to, bool backingOnly = false);

struct PlayAlong {
    QString xml;           // empty: nothing to play along to (no backing parts)
    QList<int> passTempos; // the tempo of each pass (in the exercise's own beat unit), empty when it has no tempo
    QList<int> passStarts; // when each pass starts (seconds from the beginning)
    int seconds = 0;       // how long it plays
};
//! The play-along for bars from-to: passes from startTempo up to goalTempo (in the exercise's beat unit; 0: the
//! exercise's own tempo, all passes), each at least ~20 seconds, until it lasts `seconds`
PlayAlong playAlong(const QString& musicXml, int from, int to, int startTempo, int goalTempo, int seconds);
}
