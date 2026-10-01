/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Band roster: who plays what, for the changelogs and Horn Part Guides in Sheets and Demos.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Band roster")

    contentWidth: 900
    contentHeight: 560
    margins: 16

    RosterModel { id: roster }

    Component.onCompleted: roster.load()

    readonly property var keyChoices: [
        { "text": qsTrc("starscore", "—"), "value": "" },
        { "text": "B♭", "value": "Bb" },
        { "text": "E♭", "value": "Eb" },
        { "text": "C", "value": "C" }
    ]
    readonly property var chairChoices: [
        { "text": qsTrc("starscore", "—"), "value": 0 },
        { "text": "1", "value": 1 },
        { "text": "2", "value": 2 },
        { "text": "3", "value": 3 }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            text: qsTrc("starscore", "Each player's changelog lists the changes to the parts named under Reads. Horn players also get a Horn Part "
                        + "Guide; their Any Horns chairs say which chair they sit in for a 2- or 3-horn chart, and in which key. "
                        + "Untick Current when someone leaves: their history is kept, and a new player on the same instrument inherits it.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            StyledTextLabel { Layout.preferredWidth: 110; text: qsTrc("starscore", "Name"); font: ui.theme.bodyBoldFont; horizontalAlignment: Text.AlignLeft }
            StyledTextLabel { Layout.fillWidth: true; text: qsTrc("starscore", "Reads (part names, comma separated)"); font: ui.theme.bodyBoldFont; horizontalAlignment: Text.AlignLeft }
            StyledTextLabel { Layout.preferredWidth: 64; text: qsTrc("starscore", "Current"); font: ui.theme.bodyBoldFont }
            StyledTextLabel { Layout.preferredWidth: 54; text: qsTrc("starscore", "Horn"); font: ui.theme.bodyBoldFont }
            StyledTextLabel { Layout.preferredWidth: 150; text: qsTrc("starscore", "2-horn chair"); font: ui.theme.bodyBoldFont }
            StyledTextLabel { Layout.preferredWidth: 150; text: qsTrc("starscore", "3-horn chair"); font: ui.theme.bodyBoldFont }
        }

        StyledListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 6
            model: roster.players

            delegate: RowLayout {
                id: row
                required property var modelData
                required property int index
                width: list.width
                spacing: 8

                TextInputField {
                    Layout.preferredWidth: 110
                    currentText: row.modelData.name
                    onTextEditingFinished: function(t) { roster.setField(row.index, "name", t) }
                }
                TextInputField {
                    Layout.fillWidth: true
                    currentText: row.modelData.instruments
                    onTextEditingFinished: function(t) { roster.setField(row.index, "instruments", t) }
                }
                CheckBox {
                    Layout.preferredWidth: 64
                    checked: row.modelData.current
                    onClicked: roster.setField(row.index, "current", !checked)
                }
                CheckBox {
                    Layout.preferredWidth: 54
                    checked: row.modelData.horn
                    onClicked: roster.setField(row.index, "horn", !checked)
                }
                RowLayout {
                    Layout.preferredWidth: 150
                    spacing: 4
                    enabled: row.modelData.horn
                    StyledDropdown {
                        Layout.preferredWidth: 64
                        model: root.chairChoices
                        currentIndex: indexOfValue(row.modelData.chair2)
                        onActivated: function(i, value) { roster.setField(row.index, "chair2", value) }
                    }
                    StyledDropdown {
                        Layout.preferredWidth: 74
                        model: root.keyChoices
                        currentIndex: indexOfValue(row.modelData.key2)
                        onActivated: function(i, value) { roster.setField(row.index, "key2", value) }
                    }
                }
                RowLayout {
                    Layout.preferredWidth: 150
                    spacing: 4
                    enabled: row.modelData.horn
                    StyledDropdown {
                        Layout.preferredWidth: 64
                        model: root.chairChoices
                        currentIndex: indexOfValue(row.modelData.chair3)
                        onActivated: function(i, value) { roster.setField(row.index, "chair3", value) }
                    }
                    StyledDropdown {
                        Layout.preferredWidth: 74
                        model: root.keyChoices
                        currentIndex: indexOfValue(row.modelData.key3)
                        onActivated: function(i, value) { roster.setField(row.index, "key3", value) }
                    }
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            opacity: 0.8
            text: roster.status
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            FlatButton {
                text: qsTrc("starscore", "Add player")
                onClicked: roster.addPlayer()
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("starscore", "Save")
                accentButton: roster.dirty
                onClicked: roster.save()
            }
            FlatButton {
                text: qsTrc("starscore", "Close")
                onClicked: root.hide()
            }
        }
    }
}
