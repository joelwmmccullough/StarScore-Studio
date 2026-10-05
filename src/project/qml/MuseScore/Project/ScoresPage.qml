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
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project
import Muse.Cloud

import "internal/ScoresPage"

FocusScope {
    id: root

    QtObject {
        id: prv

        readonly property int sideMargin: 46
    }

    NavigationSection {
        id: navSec
        name: "RecentScores"
        enabled: root.enabled && root.visible
        order: 3
        onActiveChanged: {
            if (navSec.active) {
                root.forceActiveFocus()
            }
        }
    }

    ScoresPageModel {
        id: scoresPageModel
    }

    Rectangle {
        id: background

        anchors.fill: parent

        color: ui.theme.backgroundSecondaryColor
    }

    RowLayout {
        id: topLayout

        anchors.top: parent.top
        anchors.topMargin: prv.sideMargin
        anchors.left: parent.left
        anchors.leftMargin: prv.sideMargin
        anchors.right: parent.right
        anchors.rightMargin: prv.sideMargin

        spacing: 12

        NavigationPanel {
            id: navSearchPanel
            name: "HomeScoresSearch"
            enabled: topLayout.enabled && topLayout.visible
            section: navSec
            order: 1
            accessible.name: qsTrc("project", "Scores")
        }

        StyledTextLabel {
            id: pageTitle
            Layout.fillWidth: true

            text: qsTrc("project", "Scores")
            font: ui.theme.titleBoldFont
            horizontalAlignment: Text.AlignLeft
        }

        SearchField {
            id: searchField

            Layout.preferredWidth: 220

            navigation.name: "Scores Search"
            navigation.panel: navSearchPanel
            navigation.order: 1
            accessible.name: qsTrc("project", "Search recent scores")
        }
    }

    RowLayout {
        id: controlsRow

        anchors.top: topLayout.bottom
        anchors.topMargin: prv.sideMargin
        anchors.left: parent.left
        anchors.leftMargin: prv.sideMargin
        anchors.right: parent.right
        anchors.rightMargin: prv.sideMargin

        spacing: 12

        // StarScore: recent scores only (the online scores tab and the Score manager button are gone, 1.18.3)
        Item {
            Layout.fillWidth: true
        }

        NavigationPanel {
            id: viewButtonsNavPanel
            name: "ViewButtons"
            enabled: controlsRow.enabled && controlsRow.visible
            section: navSec
            order: 3
            direction: NavigationPanel.Horizontal
            accessible.name: qsTrc("project", "View buttons")
        }

        RadioButtonGroup {
            id: viewTypeRadioButtons

            property int navigationOrderStart: 1

            implicitHeight: ui.theme.defaultButtonSize

            model: [
                { "icon": IconCode.GRID, "title": qsTrc("project", "Grid view"), "value": ScoresPageModel.Grid },
                { "icon": IconCode.LIST, "title": qsTrc("project", "List view"), "value": ScoresPageModel.List }
            ]

            delegate: FlatRadioButton {
                implicitWidth: ui.theme.defaultButtonSize
                implicitHeight: ui.theme.defaultButtonSize

                checked: scoresPageModel.viewType === modelData.value

                iconCode: modelData.icon
                transparent: true
                checkedColor: ui.theme.buttonColor

                navigation.name: "ViewType_" + modelData.title
                navigation.panel: viewButtonsNavPanel
                navigation.order: viewTypeRadioButtons.navigationOrderStart + model.index
                navigation.accessible.name: modelData.title

                onToggled: {
                    scoresPageModel.viewType = modelData.value
                }
            }
        }
    }

    Loader {
        id: contentLoader

        anchors.top: controlsRow.bottom
        anchors.topMargin: 24
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: buttonsPanel.top

        sourceComponent: newAndRecentComp
    }

    Component {
        id: newAndRecentComp

        RecentScoresView {
            anchors.fill: parent

            viewType: scoresPageModel.viewType
            searchText: searchField.searchText

            backgroundColor: background.color
            sideMargin: prv.sideMargin

            navigationSection: navSec
            navigationOrder: 4

            onCreateNewScoreRequested: {
                scoresPageModel.createNewScore()
            }

            onOpenScoreRequested: function(scorePath, displayName) {
                Qt.callLater(scoresPageModel.openScore, scorePath, displayName)
            }
        }
    }

    Rectangle {
        id: buttonsPanel

        anchors.bottom: parent.bottom

        height: 100
        width: parent.width

        color: ui.theme.backgroundSecondaryColor

        NavigationPanel {
            id: navBottomPanel
            name: "RecentScoresBottom"
            section: navSec
            direction: NavigationPanel.Horizontal
            order: 5

            //: accessibility name for the panel at the bottom of the "Scores" page
            accessible.name: qsTrc("project", "Scores actions")
        }

        Row {
            anchors.right : parent.right
            anchors.rightMargin: prv.sideMargin
            anchors.verticalCenter: parent.verticalCenter

            spacing: 12

            FlatButton {
                navigation.name: "NewScore"
                navigation.panel: navBottomPanel
                navigation.column: 2

                text: qsTrc("project", "New")

                onClicked: {
                    scoresPageModel.createNewScore()
                }
            }

            FlatButton {
                navigation.name: "Open other Score"
                navigation.panel: navBottomPanel
                navigation.column: 3

                text: qsTrc("project", "Open other…")

                onClicked: {
                    scoresPageModel.openOther()
                }
            }
        }
    }
}
