/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — flag bars for practice (see practiceflagmodel.h): select bars on the Keys sheet, say whether they're
 * a solo, comping, or a written part, how hard (Easy to Virtuosic), and anything about them. Nothing shows on the sheet.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Flag bars for practice")

    contentWidth: 560
    contentHeight: 620
    margins: 16
    resizable: true

    property string editId: ""
    property string kind: "solo"
    property int difficulty: 2

    PracticeFlagModel { id: fm }

    Component.onCompleted: fm.useSelection()

    function clearForm() {
        root.editId = ""
        root.kind = "solo"
        root.difficulty = 2
        notes.text = ""
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            opacity: 0.8
            text: qsTrc("starscore", "Select bars on the sheet, then flag them. Practice routines use the flags; nothing shows on the sheet.")
        }

        RowLayout {
            spacing: 8
            StyledTextLabel { text: qsTrc("starscore", "Bars") }
            IncrementalPropertyControl {
                Layout.preferredWidth: 90
                currentValue: fm.fromBar
                step: 1; decimals: 0; minValue: 1; maxValue: 2000
                onValueEdited: function(v) { fm.fromBar = v }
            }
            StyledTextLabel { text: qsTrc("starscore", "to") }
            IncrementalPropertyControl {
                Layout.preferredWidth: 90
                currentValue: fm.toBar
                step: 1; decimals: 0; minValue: 1; maxValue: 2000
                onValueEdited: function(v) { fm.toBar = v }
            }
            FlatButton {
                text: qsTrc("starscore", "Use the selected bars")
                onClicked: fm.useSelection()
            }
        }

        RowLayout {
            spacing: 6
            StyledTextLabel { text: qsTrc("starscore", "What it is"); Layout.preferredWidth: 80; horizontalAlignment: Text.AlignLeft }
            Repeater {
                model: [ { "v": "solo", "t": qsTrc("starscore", "Solo") }, { "v": "comping", "t": qsTrc("starscore", "Comping") },
                         { "v": "parts", "t": qsTrc("starscore", "Written part") } ]
                delegate: FlatButton {
                    required property var modelData
                    text: modelData.t
                    accentButton: root.kind === modelData.v
                    minWidth: 100
                    onClicked: root.kind = modelData.v
                }
            }
        }

        RowLayout {
            spacing: 6
            StyledTextLabel { text: qsTrc("starscore", "How hard"); Layout.preferredWidth: 80; horizontalAlignment: Text.AlignLeft }
            Repeater {
                model: [ qsTrc("starscore", "Easy"), qsTrc("starscore", "Medium"), qsTrc("starscore", "Hard"), qsTrc("starscore", "Virtuosic") ]
                delegate: FlatButton {
                    required property string modelData
                    required property int index
                    text: modelData
                    accentButton: root.difficulty === index + 1
                    minWidth: 80
                    onClicked: root.difficulty = index + 1
                }
            }
        }

        StyledTextLabel { text: qsTrc("starscore", "Notes (anything about these bars: what's hard, what to work on, how it should sound)"); horizontalAlignment: Text.AlignLeft }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 110
            color: ui.theme.textFieldColor
            border.color: ui.theme.strokeColor
            radius: 3
            ScrollView {
                anchors.fill: parent
                anchors.margins: 6
                TextArea {
                    id: notes
                    color: ui.theme.fontPrimaryColor
                    font: ui.theme.bodyFont
                    wrapMode: TextEdit.Wrap
                    placeholderText: qsTrc("starscore", "e.g. The LH stabs against the 16th line; keep the time when the RH runs.")
                }
            }
        }

        RowLayout {
            spacing: 8
            FlatButton {
                text: root.editId === "" ? qsTrc("starscore", "Flag these bars") : qsTrc("starscore", "Save changes")
                accentButton: true
                onClicked: {
                    if (fm.save(root.editId, root.kind, root.difficulty, notes.text)) {
                        root.clearForm()
                    }
                }
            }
            FlatButton {
                visible: root.editId !== ""
                text: qsTrc("starscore", "Cancel")
                onClicked: root.clearForm()
            }
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                text: fm.message
                opacity: 0.8
            }
        }

        SeparatorLine {}

        StyledTextLabel { text: qsTrc("starscore", "Flags in this song"); font: ui.theme.bodyBoldFont }
        StyledFlickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: flagColumn.implicitHeight
            clip: true
            ColumnLayout {
                id: flagColumn
                width: parent.width
                spacing: 4
                StyledTextLabel {
                    visible: fm.flags.length === 0
                    text: qsTrc("starscore", "None yet.")
                    opacity: 0.6
                }
                Repeater {
                    model: fm.flags
                    delegate: RowLayout {
                        id: flagRow
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 8
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                            text: flagRow.modelData.bars + "  ·  " + flagRow.modelData.kindName + "  ·  " + flagRow.modelData.difficultyName
                                  + (flagRow.modelData.text ? "  ·  " + flagRow.modelData.text.replace(/\n/g, " / ") : "")
                        }
                        FlatButton {
                            text: qsTrc("starscore", "Edit")
                            minWidth: 50
                            onClicked: {
                                root.editId = flagRow.modelData.id
                                root.kind = flagRow.modelData.kind
                                root.difficulty = flagRow.modelData.difficulty
                                notes.text = flagRow.modelData.text || ""
                                fm.fromBar = flagRow.modelData.from
                                fm.toBar = flagRow.modelData.to
                            }
                        }
                        FlatButton {
                            text: qsTrc("starscore", "Delete")
                            minWidth: 56
                            onClicked: fm.remove(flagRow.modelData.id)
                        }
                    }
                }
            }
        }
    }
}
