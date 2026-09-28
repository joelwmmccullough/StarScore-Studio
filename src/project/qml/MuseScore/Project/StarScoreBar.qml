/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * The Arrangements / Sections bar shown above the score.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

Rectangle {
    id: root

    property bool collapsed: false

    visible: barModel.hasScore
    implicitHeight: visible ? content.implicitHeight + 12 : 0
    color: ui.theme.backgroundSecondaryColor

    StarScoreBarModel {
        id: barModel
    }

    Component.onCompleted: {
        barModel.load()
    }

    //! One pill-shaped button: an arrangement or a section
    component StarScoreChip: Rectangle {
        id: chip

        property string text: ""
        property bool active: false
        property bool dashed: false
        property color dotColor: "transparent"
        property bool showDot: true
        property bool showCheck: false
        property string toolTip: ""
        property var menuItemsProvider: null

        signal leftClicked(var mouse)
        signal menuItemChosen(string itemId)

        implicitHeight: 26
        implicitWidth: chipRow.implicitWidth + 20
        radius: height / 2

        color: chip.active ? ui.theme.accentColor : (chipMouse.containsMouse ? ui.theme.buttonColor : "transparent")
        opacity: chip.active ? 1.0 : 0.9
        border.width: 1
        border.color: chip.active ? ui.theme.accentColor : ui.theme.strokeColor

        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 6

            StyledIconLabel {
                visible: chip.showCheck
                anchors.verticalCenter: parent.verticalCenter
                iconCode: chip.active ? IconCode.EYE_OPEN : IconCode.EYE_CLOSED
                color: chip.active ? "#ffffff" : ui.theme.fontSecondaryColor
            }

            Rectangle {
                visible: chip.showDot
                anchors.verticalCenter: parent.verticalCenter
                width: 9
                height: 9
                radius: 4.5
                color: chip.dotColor
                border.width: 1
                border.color: chip.active ? "#ffffff" : Qt.darker(chip.dotColor, 1.3)
            }

            StyledTextLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.text
                font: chip.active ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                color: chip.active ? "#ffffff" : (chip.dashed ? ui.theme.fontSecondaryColor : ui.theme.fontPrimaryColor)
            }
        }

        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            onClicked: function(mouse) {
                if (mouse.button === Qt.RightButton) {
                    if (chip.menuItemsProvider) {
                        chipMenu.toggleOpened(chip.menuItemsProvider())
                    }
                    return
                }
                chip.leftClicked(mouse)
            }

            ToolTip.visible: containsMouse && chip.toolTip !== ""
            ToolTip.delay: 700
            ToolTip.text: chip.toolTip
        }

        StyledMenuLoader {
            id: chipMenu

            onHandleMenuItem: function(itemId) {
                chip.menuItemChosen(itemId)
            }
        }

        function openMenu(items) {
            chipMenu.toggleOpened(items)
        }
    }

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        anchors.topMargin: 6

        spacing: 6

        // --- Arrangements ---
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel {
                Layout.preferredWidth: 96
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.topMargin: 5
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "Arrangements")
                font: ui.theme.bodyBoldFont
            }

            Flow {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    model: barModel.arrangements

                    delegate: StarScoreChip {
                        required property var modelData

                        text: modelData.name
                        active: modelData.active
                        dotColor: barModel.statusColor(modelData.status)
                        toolTip: modelData.sections + "\n" + qsTrc("starscore", "Status: ") + barModel.statusName(modelData.status)
                                 + "\n" + qsTrc("starscore", "Right-click for options")
                        menuItemsProvider: function() { return barModel.arrangementMenu(modelData.id) }

                        onLeftClicked: function(mouse) {
                            barModel.showArrangement(modelData.id)
                        }
                        onMenuItemChosen: function(itemId) {
                            barModel.handleMenuItem(itemId)
                        }
                    }
                }

                StarScoreChip {
                    id: newArrangementChip
                    text: qsTrc("starscore", "+ New arrangement")
                    dashed: true
                    showDot: false

                    onLeftClicked: function(mouse) {
                        newArrangementChip.openMenu(barModel.addArrangementMenu())
                    }
                    onMenuItemChosen: function(itemId) {
                        barModel.handleMenuItem(itemId)
                    }
                }
            }

            FlatButton {
                Layout.alignment: Qt.AlignTop
                icon: IconCode.MENU_THREE_DOTS
                transparent: true
                toolTipTitle: qsTrc("starscore", "More StarScore options")

                onClicked: {
                    moreMenu.toggleOpened(barModel.moreMenu())
                }

                StyledMenuLoader {
                    id: moreMenu

                    onHandleMenuItem: function(itemId) {
                        barModel.handleMenuItem(itemId)
                    }
                }
            }

            FlatButton {
                Layout.alignment: Qt.AlignTop
                icon: root.collapsed ? IconCode.SMALL_ARROW_DOWN : IconCode.SMALL_ARROW_UP
                transparent: true
                toolTipTitle: root.collapsed ? qsTrc("starscore", "Show sections") : qsTrc("starscore", "Hide sections")

                onClicked: {
                    root.collapsed = !root.collapsed
                }
            }
        }

        // --- Sections ---
        RowLayout {
            Layout.fillWidth: true
            visible: !root.collapsed
            spacing: 8

            StyledTextLabel {
                Layout.preferredWidth: 96
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.topMargin: 5
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "Sections")
                font: ui.theme.bodyBoldFont
            }

            Flow {
                Layout.fillWidth: true
                spacing: 6

                Repeater {
                    model: barModel.sections

                    delegate: StarScoreChip {
                        required property var modelData

                        text: modelData.name
                        active: modelData.on
                        showCheck: true
                        dotColor: barModel.statusColor(modelData.status)
                        toolTip: qsTrc("starscore", "%1 instrument(s)").arg(modelData.instrumentCount)
                                 + "\n" + qsTrc("starscore", "Status: ") + barModel.statusName(modelData.status)
                                 + "\n" + qsTrc("starscore", "Click: show/hide · Alt-click: show only this · Right-click: options")
                        menuItemsProvider: function() { return barModel.sectionMenu(modelData.id) }

                        onLeftClicked: function(mouse) {
                            if (mouse.modifiers & (Qt.AltModifier | Qt.MetaModifier | Qt.ControlModifier)) {
                                barModel.soloSection(modelData.id)
                            } else {
                                barModel.toggleSection(modelData.id)
                            }
                        }
                        onMenuItemChosen: function(itemId) {
                            barModel.handleMenuItem(itemId)
                        }
                    }
                }

                StarScoreChip {
                    id: newSectionChip
                    text: qsTrc("starscore", "+ New section")
                    dashed: true
                    showDot: false

                    onLeftClicked: function(mouse) {
                        newSectionChip.openMenu(barModel.addSectionMenu())
                    }
                    onMenuItemChosen: function(itemId) {
                        barModel.handleMenuItem(itemId)
                    }
                }
            }
        }

        // --- Solos ---
        RowLayout {
            Layout.fillWidth: true
            visible: !root.collapsed && (barModel.solos.length > 0 || barModel.canAddSolos)
            spacing: 8

            StyledTextLabel {
                Layout.preferredWidth: 96
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.topMargin: 5
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "Solos")
                font: ui.theme.bodyBoldFont
            }

            Flow {
                Layout.fillWidth: true
                spacing: 6

                StarScoreChip {
                    visible: barModel.solos.length > 0
                    text: qsTrc("starscore", "Main score")
                    active: !barModel.isSoloView
                    showDot: false
                    onLeftClicked: function(mouse) {
                        barModel.showMainScore()
                    }
                }

                Repeater {
                    model: barModel.solos

                    delegate: StarScoreChip {
                        required property var modelData

                        text: modelData.name
                        active: modelData.active
                        showDot: false
                        toolTip: modelData.info + "\n" + qsTrc("starscore", "Right-click for options")
                        menuItemsProvider: function() { return barModel.soloMenu(modelData.id) }

                        onLeftClicked: function(mouse) {
                            barModel.showSolo(modelData.id)
                        }
                        onMenuItemChosen: function(itemId) {
                            barModel.handleMenuItem(itemId)
                        }
                    }
                }

                StarScoreChip {
                    visible: barModel.canAddSolos
                    text: qsTrc("starscore", "+ Add solo transcription")
                    dashed: true
                    showDot: false
                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("solo-add")
                    }
                }
            }
        }
    }

    SeparatorLine {
        anchors.bottom: parent.bottom
    }
}
