/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice › Listen's video (1.27.0, Joel: "the video toggle"): the players' video in a window of
 * its own, muted, kept in step with the recording (its speed, its place). On the Mac it's AVFoundation's player;
 * elsewhere there's no video window.
 */
#pragma once

#include <QString>

namespace mu::project::starscore::video {
bool available();
//! Shows the video (a window of its own, floating above StarScore); false when it can't be played
bool open(const QString& path, const QString& title);
void close();
bool isOpen();
//! Where the recording is (seconds), its speed, and whether it's playing: the video follows, re-placed when it has
//! drifted more than a few hundredths of a second; offset: the video's seconds minus the recording's
void follow(double seconds, double speed, bool playing, double offset = 0.0);
}
