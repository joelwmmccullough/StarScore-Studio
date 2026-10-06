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
import QtQuick.Window
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.AppShell
import MuseScore.NotationScene
import MuseScore.Playback
import MuseScore.Project

Item {
    id: root

    NotationStatusBarModel {
        id: model
    }

    property NavigationSection navigationSection: NavigationSection {
        id: navSec
        name: "NotationStatusBar"
        enabled: root.enabled && root.visible
        order: 8
    }

    NavigationPanel {
        id: navPanel
        name: "NotationStatusBar"
        enabled: root.enabled && root.visible
        order: 0
        direction: NavigationPanel.Horizontal
        section: navSec
    }

    RowLayout {
        id: statusBarRow

        //! StarScore: measured from the status bar's own width (the window's width was unreliable), so a narrow
        //! window moves the workspace and concert pitch controls into the menu at the right
        readonly property int eps: 100
        property int remainingSpace: root.width - (viewModeControl.width + zoomControl.width + onlineSoundsStatusView.width + eps
                                                   + (playbackLoadingInfo.visible ? playbackLoadingInfo.width : 0))

        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 4

        height: parent.height

        spacing: 4

        PlaybackLoadingInfo {
            id: playbackLoadingInfo
            Layout.fillWidth: false

            onStarted: {
                visible = true
            }

            onFinished: {
                visible = false
            }
        }

        SeparatorLine { 
            Layout.leftMargin: 2
            Layout.rightMargin: 2
            orientation: Qt.Vertical
            visible: playbackLoadingInfo.visible 
        }

        StyledTextLabel {
            id: accessibiityInfo
            Layout.alignment: Qt.AlignVCenter
            Layout.fillWidth: true

            text: model.accessibilityInfo
            horizontalAlignment: Text.AlignLeft

            visible: !hiddenControlsMenuButton.visible
        }

        OnlineSoundsStatusView {
            id: onlineSoundsStatusView

            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: 28

            navigationPanel: navPanel
            navigationOrder: 1
        }

        SeparatorLine { orientation: Qt.Vertical; visible: workspaceControl.visible }

        FlatButton {
            id: workspaceControl
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: 28

            text: model.currentWorkspaceItem.title
            icon: IconCode.WORKSPACE
            orientation: Qt.Horizontal

            transparent: true
            visible: statusBarRow.remainingSpace > width + concertPitchControl.width

            navigation.panel: navPanel
            navigation.order: 2

            onClicked: {
                menuLoader.toggleOpened(model.currentWorkspaceItem.subitems)
            }

            StyledMenuLoader {
                id: menuLoader

                menuAnchorItem: ui.rootItem

                onHandleMenuItem: function(itemId) {
                    Qt.callLater(model.handleWorkspacesMenuItem, itemId)
                }
            }
        }

        SeparatorLine { orientation: Qt.Vertical; visible: concertPitchControl.visible }

        ConcertPitchControl {
            id: concertPitchControl
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: 28

            text: model.concertPitchItem.title
            icon: model.concertPitchItem.icon
            checked: model.concertPitchItem.checked
            enabled: model.concertPitchItem.enabled
            visible: statusBarRow.remainingSpace > width

            navigation.panel: navPanel
            navigation.order: 3

            onToggleConcertPitchRequested: {
                model.toggleConcertPitch()
            }
        }

        //! StarScore: how the Flexible horns are shown while writing (Joel, 6 Oct 2026), next to concert pitch; only
        //! when the score has a Flexible section. One setting for every score; the exported sheets don't change.
        FlexibleViewModel {
            id: flexibleView
        }

        Component.onCompleted: flexibleView.load()

        SeparatorLine { orientation: Qt.Vertical; visible: flexibleViewControl.visible }

        FlatButton {
            id: flexibleViewControl
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: 28

            text: qsTrc("starscore", "Flexible horns: %1").arg(flexibleView.currentTitle)
            orientation: Qt.Horizontal
            transparent: true
            visible: flexibleView.available && statusBarRow.remainingSpace > width + concertPitchControl.width

            toolTipTitle: qsTrc("starscore", "Show Flexible horns as")
            toolTipDescription: qsTrc("starscore", "How the Flexible 2-Horn and 3-Horn chairs look while you write, in every score. Their ranges and the exported sheets don't change.")

            navigation.panel: navPanel
            navigation.order: 3

            onClicked: {
                flexibleViewMenu.toggleOpened(flexibleView.menuItems)
            }

            StyledMenuLoader {
                id: flexibleViewMenu

                menuAnchorItem: ui.rootItem

                onHandleMenuItem: function(itemId) {
                    Qt.callLater(flexibleView.choose, itemId)
                }
            }
        }

        SeparatorLine { orientation: Qt.Vertical }

        ViewModeControl {
            id: viewModeControl
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: 28

            currentViewMode: model.currentViewMode
            availableViewModeList: model.availableViewModeList

            navigation.panel: navPanel
            navigation.order: 4

            onChangeCurrentViewModeRequested: function(newViewMode) {
                model.setCurrentViewMode(newViewMode)
            }
        }

        ZoomControl {
            id: zoomControl
            Layout.alignment: Qt.AlignVCenter
            Layout.preferredHeight: 28

            enabled: model.zoomEnabled
            currentZoomPercentage: model.currentZoomPercentage
            minZoomPercentage: model.minZoomPercentage()
            maxZoomPercentage: model.maxZoomPercentage()
            availableZoomList: model.availableZoomList

            navigationPanel: navPanel
            navigationOrderMin: 5

            onChangeZoomPercentageRequested: function(newZoomPercentage) {
                model.currentZoomPercentage = newZoomPercentage
            }

            onChangeZoomRequested: function(zoomId) {
                model.setCurrentZoom(zoomId)
            }

            onZoomInRequested: {
                model.zoomIn()
            }

            onZoomOutRequested: {
                model.zoomOut()
            }
        }

        SeparatorLine { orientation: Qt.Vertical; visible: hiddenControlsMenuButton.visible }

        MenuButton {
            id: hiddenControlsMenuButton

            Layout.alignment: Qt.AlignVCenter

            visible: !concertPitchControl.visible ||
                     !workspaceControl.visible

            navigation.panel: navPanel
            navigation.order: zoomControl.navigationOrderMax + 1

            menuModel: {
                var result = []

                if (!concertPitchControl.visible) {
                    result.push(model.concertPitchItem)
                }

                if (!workspaceControl.visible) {
                    result.push(model.currentWorkspaceItem)
                }

                return result
            }

            onHandleMenuItem: function(itemId) {
                switch (itemId) {
                case model.concertPitchItem.id:
                    model.handleAction(model.concertPitchItem.code)
                    break
                case model.currentWorkspaceItem.id:
                    model.handleAction(model.currentWorkspaceItem.code)
                    break
                }
            }
        }
    }
}
