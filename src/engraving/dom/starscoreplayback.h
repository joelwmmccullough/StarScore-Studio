/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a playback transposition (1.25.0, Joel 4 Nov 2026): a Solo Transcription's key part score
 * plays in its own key while it's the one showing. Added to every note's playing pitch and spelling
 * (Note::ppitch, playingTpc, playingOctave); the main score plays as written (no interval).
 */
#pragma once

#include "interval.h"

namespace mu::engraving {
class MasterScore;
}

namespace mu::engraving::starscore {
void setPlaybackTranspose(const MasterScore* ms, const Interval& iv);
Interval playbackTranspose(const MasterScore* ms);
}
