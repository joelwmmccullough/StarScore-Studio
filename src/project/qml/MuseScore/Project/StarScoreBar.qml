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

    visible: barModel.hasScore && barModel.panelVisible
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
        signal menuOpenChanged(bool isOpen)

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
                color: chip.active ? ui.theme.fontPrimaryColor : ui.theme.fontSecondaryColor
            }

            Rectangle {
                visible: chip.showDot
                anchors.verticalCenter: parent.verticalCenter
                width: 9
                height: 9
                radius: 4.5
                color: chip.dotColor
                border.width: 1
                border.color: chip.active ? ui.theme.fontPrimaryColor : Qt.darker(chip.dotColor, 1.3)
            }

            StyledTextLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.text
                font: chip.active ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                // (1.24.0) the dashed "+ New …" chips' text was too faint to read: the main text colour, a little lighter
                color: ui.theme.fontPrimaryColor
                opacity: chip.dashed && !chip.active ? 0.7 : 1.0
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

            // Status items: the menu stays open and shows the new ticks
            onKeepOpenItemChosen: function(itemId) {
                chip.menuItemChosen(itemId)
                if (chip.menuItemsProvider) {
                    chipMenu.refreshOpen(chip.menuItemsProvider())
                }
            }

            onOpened: chip.menuOpenChanged(true)
            onClosed: chip.menuOpenChanged(false)
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
                text: barModel.isSoloTranscription ? qsTrc("starscore", "Solo") : qsTrc("starscore", "Arrangements")
                font: ui.theme.bodyBoldFont
            }

            Flow {
                Layout.fillWidth: true
                // takes the room left beside the label and buttons and wraps within it (a narrow window kept the
                // chips on one line and pushed the buttons out of sight)
                Layout.preferredWidth: 1
                spacing: 6

                // (1.24.0) a Solo Transcription: who and what, its sheets, the song it's tied to
                StarScoreChip {
                    visible: barModel.isSoloTranscription
                    text: (barModel.soloInfo.player || "") + " – " + (barModel.soloInfo.song || "")
                          + (barModel.soloInfo.instrumentName ? " · " + barModel.soloInfo.instrumentName : "")
                    showDot: false
                    toolTip: [ barModel.soloInfo.band || "", barModel.soloInfo.recording || "", barModel.soloInfo.link || "" ]
                             .filter(function(t) { return t !== "" }).join("\n")
                }

                StarScoreChip {
                    visible: barModel.isSoloTranscription
                    text: qsTrc("starscore", "Export the 12 keys")
                    showDot: false
                    toolTip: qsTrc("starscore", "Every key's sheet, with the notes colored by pitch, into the solo's Sheets folder: in "
                                   + "concert pitch, and written for the instrument when it transposes.")
                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("solo-export-sheets")
                    }
                }

                StarScoreChip {
                    visible: barModel.isSoloTranscription && (barModel.soloInfo.tiedSongTitle || "") !== ""
                    text: qsTrc("starscore", "Song: %1").arg(barModel.soloInfo.tiedSongTitle || "")
                    showDot: false
                    toolTip: qsTrc("starscore", "Open the song")
                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("solo-open-tied")
                    }
                }

                Repeater {
                    model: barModel.isSoloTranscription ? [] : barModel.arrangements

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
                    visible: !barModel.isSoloTranscription
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
                toolTipTitle: root.collapsed ? qsTrc("starscore", "Show the section, progress and solo rows") : qsTrc("starscore", "Hide the section, progress and solo rows")

                onClicked: {
                    root.collapsed = !root.collapsed
                }
            }

            FlatButton {
                Layout.alignment: Qt.AlignTop
                icon: IconCode.CLOSE_X_ROUNDED
                transparent: true
                toolTipTitle: qsTrc("starscore", "Hide the StarScore panel")
                toolTipDescription: qsTrc("starscore", "Bring it back with View › StarScore panel")

                onClicked: {
                    barModel.hidePanel()
                }
            }
        }

        // --- Sections ---
        RowLayout {
            Layout.fillWidth: true
            visible: !root.collapsed && !barModel.isSoloTranscription
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
                // takes the room left beside the label and buttons and wraps within it (a narrow window kept the
                // chips on one line and pushed the buttons out of sight)
                Layout.preferredWidth: 1
                spacing: 6

                Repeater {
                    model: barModel.sections

                    delegate: StarScoreChip {
                        required property var modelData

                        text: modelData.name
                        active: modelData.on
                        showCheck: true
                        // teal: finished, with drums / percussion / keys reading the lead sheet
                        dotColor: modelData.leadSheet ? "#2FB5A8" : barModel.statusColor(modelData.status)
                        toolTip: qsTrc("starscore", "%1 instrument(s)").arg(modelData.instrumentCount)
                                 + "\n" + qsTrc("starscore", "Status: ") + modelData.statusText
                                 + "\n" + qsTrc("starscore", "Click: show/hide · Alt-click: show only this · Right-click: options")
                        menuItemsProvider: function() { return barModel.sectionMenu(modelData.id) }
                        // the chips are rebuilt when the panel updates, which would close the open menu
                        onMenuOpenChanged: function(isOpen) { barModel.setHoldUpdates(isOpen) }
                        Component.onDestruction: barModel.setHoldUpdates(false)

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

                // every section shown or hidden in one click (Joel, 16 Oct 2026)
                StarScoreChip {
                    text: qsTrc("starscore", "Show all")
                    dashed: true
                    showDot: false
                    toolTip: qsTrc("starscore", "Show every section")

                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("all-on")
                    }
                }

                StarScoreChip {
                    text: qsTrc("starscore", "Hide all")
                    dashed: true
                    showDot: false
                    toolTip: qsTrc("starscore", "Hide every section")

                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("all-off")
                    }
                }
            }
        }

        // --- Progress colors (Joel, 7 Oct 2026): select measures, then click a color ---
        RowLayout {
            Layout.fillWidth: true
            visible: !root.collapsed
            spacing: 8

            StyledTextLabel {
                Layout.preferredWidth: 96
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.topMargin: 5
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "Progress")
                font: ui.theme.bodyBoldFont
            }

            Flow {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 6

                StarScoreChip {
                    text: barModel.progressShown ? qsTrc("starscore", "Colors shown") : qsTrc("starscore", "Colors hidden")
                    active: barModel.progressShown
                    showDot: false
                    toolTip: qsTrc("starscore", "Show or hide the progress colors. Hiding them keeps them.\nThey never appear in exports.")
                    onLeftClicked: function(mouse) {
                        barModel.runAction("starscore-progress-colors")
                    }
                }

                Repeater {
                    model: [
                        { code: "starscore-progress-finished", name: qsTrc("starscore", "Finished"), color: "#28AA3C" },
                        { code: "starscore-progress-review", name: qsTrc("starscore", "Needs review"), color: "#FF8C00" },
                        { code: "starscore-progress-unfinished", name: qsTrc("starscore", "Unfinished"), color: "#E11E1E" },
                        { code: "starscore-progress-clear", name: qsTrc("starscore", "Clear"), color: "transparent" }
                    ]

                    delegate: StarScoreChip {
                        required property var modelData

                        text: modelData.name
                        showDot: modelData.color !== "transparent"
                        dotColor: modelData.color
                        toolTip: qsTrc("starscore", "Select measures (click, then Shift+click), then click here to color them.")
                        onLeftClicked: function(mouse) {
                            barModel.runAction(modelData.code)
                        }
                    }
                }
            }
        }

        // --- Audio (1.25.0): the score's audio and Transcribe! files, in the Audio folder beside the .starscore ---
        RowLayout {
            Layout.fillWidth: true
            visible: !root.collapsed && (barModel.audioFiles.length > 0 || barModel.canAddAudio)
            spacing: 8

            StyledTextLabel {
                Layout.preferredWidth: 96
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.topMargin: 5
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "Audio")
                font: ui.theme.bodyBoldFont
            }

            Flow {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 6

                Repeater {
                    model: barModel.audioFiles

                    delegate: StarScoreChip {
                        required property var modelData

                        text: (modelData.kind === "transcribe" ? "T! " : "♪ ") + modelData.name
                        showDot: false
                        toolTip: (modelData.kind === "transcribe" ? qsTrc("starscore", "Click to open it in Transcribe!")
                                                                  : qsTrc("starscore", "Click to play it"))
                                 + "\n" + qsTrc("starscore", "Right-click for options")
                        menuItemsProvider: function() { return barModel.audioFileMenu(modelData.path) }
                        onLeftClicked: function(mouse) {
                            barModel.handleMenuItem("audio-open:" + modelData.path)
                        }
                        onMenuItemChosen: function(itemId) {
                            barModel.handleMenuItem(itemId)
                        }
                    }
                }

                StarScoreChip {
                    visible: barModel.canAddAudio
                    text: qsTrc("starscore", "+ Audio or Transcribe! file")
                    dashed: true
                    showDot: false
                    toolTip: qsTrc("starscore", "The original recording, a stem-separated copy, a Transcribe! file…: copied into the "
                                   + "Audio folder beside this score")
                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("audio-add")
                    }
                }
            }
        }

        // --- Solos ---
        RowLayout {
            Layout.fillWidth: true
            visible: !root.collapsed && !barModel.isSoloTranscription
                     && (barModel.solos.length > 0 || barModel.canAddSolos || barModel.tiedSolos.length > 0)
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
                // takes the room left beside the label and buttons and wraps within it (a narrow window kept the
                // chips on one line and pushed the buttons out of sight)
                Layout.preferredWidth: 1
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

                // (1.24.0) the song's Solo Transcriptions, each in its own file: clicked, it opens
                Repeater {
                    model: barModel.tiedSolos

                    delegate: StarScoreChip {
                        required property var modelData

                        text: modelData.name
                        showDot: false
                        toolTip: qsTrc("starscore", "Solo transcription (its own file): click to open it")
                        onLeftClicked: function(mouse) {
                            barModel.handleMenuItem("solo-open-file:" + modelData.path)
                        }
                    }
                }

                StarScoreChip {
                    visible: barModel.canAddSolos
                    text: qsTrc("starscore", "+ New solo transcription")
                    dashed: true
                    showDot: false
                    toolTip: qsTrc("starscore", "A solo transcription in its own file, in all twelve keys, tied to this song.\n"
                                   + "Right-click to add a solo kept inside the song instead (the old way).")
                    menuItemsProvider: function() {
                        return [ { "id": "solo-add", "title": qsTrc("starscore", "Add a solo kept inside the song (the old way)…"), "enabled": true } ]
                    }
                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("solo-new")
                    }
                    onMenuItemChosen: function(itemId) {
                        barModel.handleMenuItem(itemId)
                    }
                }
            }
        }
    }

    SeparatorLine {
        anchors.bottom: parent.bottom
    }
}
