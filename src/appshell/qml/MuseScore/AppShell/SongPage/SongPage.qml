/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Song page (1.28.0): what the open song has, and a way into each (StarScoreSongHub.qml).
 * Between Home and Score in the top bar; shown first when a .starscore is opened.
 */
pragma ComponentBehavior: Bound

import QtQuick

import Muse.Ui
import Muse.UiComponents
import Muse.Dock
import MuseScore.Project

DockPage {
    id: root

    objectName: "Song"
    uri: "musescore://song"

    central: hubComp

    Component {
        id: hubComp

        StarScoreSongHub {}
    }
}
