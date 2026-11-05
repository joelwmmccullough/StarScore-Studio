/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Run in Synthesia: this song's setup (1.25.5, Joel 5 Nov 2026). Which score goes to Synthesia,
 * which staff is the right hand and which the left, and each part's sound. Kept with the song, so the next
 * "Run in Synthesia" goes straight to Synthesia. Chord symbols are never sent.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Run in Synthesia")

    contentWidth: 560
    contentHeight: 520
    margins: 20

    //! opened by "Run in Synthesia" for a song set up for the first time: OK sends it too
    property bool runAfter: false

    SynthesiaModel {
        id: synthModel
    }

    function finish(run) {
        var problem = synthModel.apply(run)
        if (problem !== "") {
            problemLabel.text = problem
            return
        }
        root.ret = { "errcode": 0 }
        root.hide()
    }

    readonly property var soundChoices: synthModel.soundNames.map(function(n, i) { return { "text": n, "value": i } })

    readonly property var handChoices: [
        { "text": qsTrc("starscore", "Right hand"), "value": "R" },
        { "text": qsTrc("starscore", "Left hand"), "value": "L" },
        { "text": qsTrc("starscore", "Synthesia plays it"), "value": "B" },
        { "text": qsTrc("starscore", "Left out"), "value": "X" }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: synthModel.problem !== "" ? synthModel.problem
                  : qsTrc("starscore", "How %1 goes to Synthesia. It's kept with the song: next time, Run in Synthesia "
                          + "opens it straight away (change it with ⋯ › Synthesia setup for this song…). Chord symbols "
                          + "are never sent.").arg(synthModel.song)
        }

        StyledTextLabel { visible: synthModel.problem === ""; text: qsTrc("starscore", "Send"); font: ui.theme.bodyBoldFont }
        StyledDropdown {
            visible: synthModel.problem === ""
            Layout.fillWidth: true
            model: [
                { "text": qsTrc("starscore", "The score showing when you click (a part score, a solo's key)"), "value": true },
                { "text": qsTrc("starscore", "Always the main score"), "value": false }
            ]
            currentIndex: synthModel.sendShowing ? 0 : 1
            onActivated: function(index, value) { synthModel.choose(value) }
        }

        StyledTextLabel {
            visible: synthModel.problem === ""
            text: qsTrc("starscore", "Staves (now: %1)").arg(synthModel.scoreName)
            font: ui.theme.bodyBoldFont
        }

        StyledFlickable {
            visible: synthModel.problem === ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: trackColumn.implicitHeight
            clip: true

            Column {
                id: trackColumn
                width: parent.width
                spacing: 8

                Repeater {
                    model: synthModel.tracks

                    RowLayout {
                        width: trackColumn.width
                        spacing: 8

                        StyledTextLabel {
                            Layout.preferredWidth: 170
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                            text: modelData.name
                        }
                        StyledDropdown {
                            Layout.preferredWidth: 150
                            model: root.handChoices
                            currentIndex: indexOfValue(modelData.hand)
                            onActivated: function(i, value) { synthModel.setHand(index, value) }
                        }
                        StyledDropdown {
                            visible: modelData.firstOfPart
                            Layout.fillWidth: true
                            model: root.soundChoices
                            currentIndex: modelData.program
                            onActivated: function(i, value) { synthModel.setSound(index, value) }
                        }
                        StyledTextLabel {
                            visible: !modelData.firstOfPart
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            opacity: 0.6
                            text: qsTrc("starscore", "(the part's sound, above)")
                        }
                    }
                }
            }
        }

        StyledTextLabel {
            id: problemLabel
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            color: ui.theme.accentColor
            text: ""
            visible: text !== ""
        }

        ButtonBox {
            Layout.fillWidth: true
            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("starscore", "Save")
                visible: !root.runAfter
                enabled: synthModel.problem === ""
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: root.finish(false)
            }

            FlatButton {
                text: qsTrc("starscore", "Save and run in Synthesia")
                accentButton: true
                enabled: synthModel.problem === ""
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 2
                onClicked: root.finish(true)
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) {
                    root.reject()
                }
            }
        }
    }
}
