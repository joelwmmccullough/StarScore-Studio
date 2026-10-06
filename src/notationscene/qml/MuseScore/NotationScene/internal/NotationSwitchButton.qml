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

import Muse.UiComponents
import Muse.Ui

FlatRadioButton {
    id: root

    property bool needSave: false
    property bool isCloud: false
    // StarScore: the part score's status (a coloured dot, like the section and arrangement buttons)
    property string statusColor: ""
    property string statusName: ""

    property alias contextMenuItems: contextMenuLoader.items

    signal contextMenuItemsRequested()
    signal handleContextMenuItem(string itemId)

    signal closeRequested()

    implicitWidth: Math.min(200, implicitContentWidth)
    implicitHeight: ListView.view.height

    padding: 0

    contentItem: RowLayout {
        anchors.fill: parent
        spacing: 4

        Rectangle {
            Layout.alignment: Qt.AlignVCenter
            Layout.leftMargin: 10
            visible: root.statusColor !== ""
            width: 8
            height: 8
            radius: 4
            color: root.statusColor !== "" ? root.statusColor : "transparent"
        }

        StyledTextLabel {
            Layout.alignment: Qt.AlignLeft
            Layout.fillWidth: root.implicitContentWidth > 200
            Layout.preferredWidth: implicitWidth
            Layout.leftMargin: root.statusColor !== "" ? 2 : 12

            horizontalAlignment: Text.AlignLeft

            text: (root.needSave ? "*" : "") + root.text
        }

        StyledIconLabel {
            visible: root.isCloud

            iconCode: IconCode.CLOUD
        }

        FlatButton {
            Layout.preferredHeight: 20
            Layout.preferredWidth: height
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

            //! StarScore: no close button on the tabs, so a part isn't closed by accident (Joel, 6 Oct 2026); the
            //! tab's right-click menu still closes it
            visible: false

            transparent: true
            icon: IconCode.CLOSE_X_ROUNDED
            iconFont {
                family: ui.theme.iconsFont.family
                pixelSize: 12
            }

            navigation.name: "Close" + root.navigation.name
            navigation.panel: root.navigation.panel
            navigation.row: root.navigation.row + 1
            accessible.name: qsTrc("global", "Close")

            onClicked: {
                root.closeRequested()
            }
        }

        SeparatorLine { orientation: Qt.Vertical }
    }

    background: Rectangle {
        id: background
        anchors.fill: parent

        color: ui.theme.backgroundSecondaryColor

        Rectangle {
            id: backgroundInner
            anchors.fill: parent

            visible: false
            color: "white"
            opacity: 0.05
        }

        NavigationFocusBorder {
            navigationCtrl: root.navigation
            drawOutsideParent: false
            anchors.rightMargin: 1 // for separator
        }

        MouseArea {
            anchors.fill: parent

            acceptedButtons: Qt.RightButton | Qt.MiddleButton

            onClicked: function(mouse) {
                if (mouse.button === Qt.RightButton) {
                    root.contextMenuItemsRequested()
                    contextMenuLoader.show(Qt.point(mouse.x, mouse.y))

                    return
                }

                // StarScore: no closing with the middle button either (only from the right-click menu)
            }

            ContextMenuLoader {
                id: contextMenuLoader

                onHandleMenuItem: function(itemId) {
                    root.handleContextMenuItem(itemId)
                }
            }
        }

        states: [
            State {
                name: "SELECTED"
                when: root.checked

                PropertyChanges {
                    target: background
                    color: ui.theme.popupBackgroundColor
                }

                PropertyChanges {
                    target: backgroundInner
                    visible: true
                }
            },

            State {
                name: "HOVERED"
                when: root.hovered && !root.pressed

                PropertyChanges {
                    target: background
                    color: Utils.colorWithAlpha(ui.theme.buttonColor, ui.theme.buttonOpacityHover)
                }
            },

            State {
                name: "PRESSED"
                when: root.pressed

                PropertyChanges {
                    target: background
                    color: Utils.colorWithAlpha(ui.theme.buttonColor, ui.theme.buttonOpacityHit)
                }
            }
        ]
    }
}
