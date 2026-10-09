/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Add › Player annotations: a band member's own note on a sheet (mode "add"), or the selected
 * markings made theirs (mode "mark"). Exports print the sheet without them, and one more copy per player with
 * their notes, in the song's Annotated Sheets folder.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    property string mode: "add"
    readonly property bool adding: root.mode === "add"

    title: root.adding ? qsTrc("starscore", "Add player annotation") : qsTrc("starscore", "Mark as player annotation")

    contentWidth: 460
    contentHeight: 400
    margins: 20

    property string player: ""
    property string otherName: ""
    property string noteText: ""
    readonly property string someoneElse: qsTrc("starscore", "Someone else…")
    readonly property string chosenPlayer: root.player === root.someoneElse ? root.otherName : root.player

    PlayerAnnotationModel {
        id: annModel
    }

    Component.onCompleted: {
        root.player = annModel.defaultPlayer !== "" ? annModel.defaultPlayer
                      : (annModel.players.length > 0 ? annModel.players[0] : root.someoneElse)
        if (root.adding) {
            textField.ensureActiveFocus()
        }
    }

    function applyAndClose() {
        var err = root.adding ? annModel.add(root.chosenPlayer, root.noteText) : annModel.mark(root.chosenPlayer)
        if (err === "") {
            root.ret = { "errcode": 0 }
            root.hide()
        } else {
            errorLabel.text = err
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: annModel.problem !== "" ? annModel.problem
                  : root.adding ? qsTrc("starscore", "A note on %1 only, above the selected note or rest. Exports print the "
                                        + "sheet without it, and a copy with this player's notes in the song's "
                                        + "Annotated Sheets folder.").arg(annModel.sheet)
                  : annModel.selectionText
        }

        ColumnLayout {
            visible: annModel.problem === ""
            spacing: 6

            StyledTextLabel { text: qsTrc("starscore", "Whose notes"); font: ui.theme.bodyBoldFont }
            RowLayout {
                spacing: 10
                StyledDropdown {
                    id: playerBox
                    Layout.preferredWidth: 200
                    model: {
                        var items = []
                        for (var i = 0; i < annModel.players.length; ++i) {
                            items.push({ "text": annModel.players[i], "value": annModel.players[i] })
                        }
                        items.push({ "text": root.someoneElse, "value": root.someoneElse })
                        return items
                    }
                    currentIndex: indexOfValue(root.player)
                    onActivated: function(index, value) { root.player = value }
                }
                TextInputField {
                    visible: root.player === root.someoneElse
                    Layout.preferredWidth: 180
                    hint: qsTrc("starscore", "Name")
                    currentText: root.otherName
                    onTextEdited: function(t) { root.otherName = t }
                }
            }
        }

        ColumnLayout {
            visible: root.adding && annModel.problem === ""
            spacing: 6

            StyledTextLabel { text: qsTrc("starscore", "Annotation"); font: ui.theme.bodyBoldFont }
            TextInputField {
                id: textField
                Layout.fillWidth: true
                enabled: annModel.canAdd
                hint: annModel.canAdd ? qsTrc("starscore", "e.g. breathe, watch the drummer, cue: guitar")
                                      : qsTrc("starscore", "Click a note or rest on the sheet first")
                currentText: root.noteText
                onTextEdited: function(t) { root.noteText = t }
                onAccepted: root.applyAndClose()
            }
            StyledTextLabel {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignLeft
                opacity: 0.7
                text: qsTrc("starscore", "For other kinds of notes (a dynamic, a fingering, a breath mark, a line), add them "
                            + "to the sheet as usual, select them, and use Add › Player annotations › Mark as player annotation.")
            }
        }

        StyledTextLabel {
            id: errorLabel
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            color: "#E0463A"
        }

        Item { Layout.fillHeight: true }

        StyledTextLabel {
            visible: annModel.summary !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            opacity: 0.7
            text: qsTrc("starscore", "Annotations in this song:") + "\n" + annModel.summary
        }

        ButtonBox {
            Layout.fillWidth: true
            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: root.adding ? qsTrc("starscore", "Add") : qsTrc("starscore", "Mark")
                accentButton: true
                enabled: annModel.problem === "" && root.chosenPlayer.trim() !== ""
                         && (root.adding ? (annModel.canAdd && root.noteText.trim() !== "") : annModel.count > 0)
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: root.applyAndClose()
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) {
                    root.reject()
                }
            }
        }
    }
}
