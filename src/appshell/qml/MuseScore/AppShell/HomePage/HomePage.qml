/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents
import Muse.Dock

import Muse.Cloud
import Muse.Learn
import MuseScore.Project
import MuseScore.MuseSounds

DockPage {
    id: root

    property string section: "dashboard"
    property string subSection: ""

    property var window: null

    objectName: "Home"
    uri: "musescore://home"

    onSetParamsRequested: function(params) {
        if (Boolean(params["section"])) {
            setCurrentCentral(params["section"])

            if (Boolean(params["subSection"])) {
                subSection = params["subSection"]
            }
        }
    }

    onSectionChanged: {
        Qt.callLater(root.setCurrentCentral, section)
    }

    function setCurrentCentral(name) {
        // the recent scores are the left half of the Dashboard (StarScore 1.18.3)
        if (name === "scores") {
            name = "dashboard"
        }
        // the Songbooks page only when it's turned on in Preferences (StarScore 1.18.6)
        if (name === "songbooks" && !songbooksVisibility.shown) {
            name = "dashboard"
        }
        if (section === name || !Boolean(name)) {
            return
        }

        section = name

        switch (name) {
        case "dashboard": root.central = dashboardComp; break
        case "songbooks": root.central = songbooksComp; break
        case "plugins": root.central = extensionsComp; break // backward compatibility
        case "extensions": root.central = extensionsComp; break
        case "musesounds": root.central = museSoundsComp; break
        case "learn": root.central = learnComp; break
        case "changelog": root.central = changelogComp; break
        case "account": root.central = accountComp; break
        }
    }

    panels: [
        DockPanel {
            id: menuPanel

            objectName: "homeMenu"

            readonly property int maxFixedWidth: 260
            readonly property int minFixedWidth: 76
            readonly property bool iconsOnly: root.window
                                                ? root.window.width < (1050 + maxFixedWidth - minFixedWidth) // the window's old minimum: StarScore lets it get narrower
                                                : false
            readonly property int currentFixedWidth: iconsOnly ? minFixedWidth : maxFixedWidth

            width: currentFixedWidth
            minimumWidth: currentFixedWidth
            maximumWidth: currentFixedWidth

            floatable: false
            closable: false

            HomeMenu {
                currentPageName: root.section
                songbooksShown: songbooksVisibility.shown
                iconsOnly: menuPanel.iconsOnly

                onSelected: function(name) {
                    root.setCurrentCentral(name)
                }
            }
        }
    ]

    central: dashboardComp

    // Songbooks is hidden unless turned on in Preferences › General; turning it off while it's open goes to the Dashboard
    SongbooksVisibilityModel {
        id: songbooksVisibility

        onShownChanged: {
            if (!shown && root.section === "songbooks") {
                root.setCurrentCentral("dashboard")
            }
        }
    }

    Component {
        id: accountComp

        AccountPage {}
    }

    Component {
        id: songbooksComp

        StarScoreSongbooks {}
    }

    Component {
        id: dashboardComp

        // StarScore: the recent scores on the left, the Starsign dashboard on the right (the handle between them moves)
        SplitView {
            orientation: Qt.Horizontal

            handle: Rectangle {
                implicitWidth: 2
                color: ui.theme.strokeColor
            }

            ScoresPage {
                SplitView.preferredWidth: parent ? parent.width / 2 : 600
                SplitView.minimumWidth: 300
            }

            StarScoreDashboard {
                SplitView.fillWidth: true
                SplitView.minimumWidth: 300
            }
        }
    }

    Component {
        id: extensionsComp

        PluginsPage {}
    }

    Component {
        id: museSoundsComp

        MuseSoundsPage {}
    }

    Component {
        id: changelogComp

        StarScoreChangelogPage {}
    }

    Component {
        id: learnComp

        LearnPage {
            section: root.subSection
        }
    }
}
