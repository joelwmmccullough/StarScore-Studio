/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a song's facts for the Song page and the song library (1.29.0, Joel 6 Nov 2026): its key, time
 * signature, tempo, number of bars and length, read from the score.
 */
#pragma once

#include <QString>
#include <QVariantMap>

namespace mu::engraving {
class MasterScore;
}

namespace mu::project::starscore {
struct SongFacts {
    QString key;        // "E♭ major", "C minor" (concert, at the start)
    QString timeSig;    // "4/4"
    int bpm = 0;        // quarter notes a minute, at the start (the written tempo, not playback's speed)
    int bars = 0;
    int seconds = 0;    // played through, repeats included, at the written tempos

    bool empty() const { return key.isEmpty() && timeSig.isEmpty() && bars == 0; }
    //! "E♭ major · 4/4 · ♩ = 98 · 112 bars · 4:31"
    QString line() const;
    QVariantMap toMap() const;
    static SongFacts fromMap(const QVariantMap& m);
};

SongFacts songFacts(mu::engraving::MasterScore* ms);
}
