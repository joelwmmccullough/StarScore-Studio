/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Mutes for this part…: tick the mutes a brass part plays with (and type in any other). Its sheets
 * print them under the instrument name top left: "Trumpet in B♭" / "with Harmon Mute" / "and Cup Mute".
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Mutes for this part")

    contentWidth: 420
    contentHeight: 520
    margins: 20

    property var ticked: []
    property string customText: ""

    PartMutesModel {
        id: mutesModel
    }

    Component.onCompleted: {
        root.ticked = mutesModel.chosen.slice()
        root.customText = mutesModel.custom
    }

    function toggle(name, on) {
        var list = root.ticked.filter(function(m) { return m !== name })
        if (on) {
            list.push(name)
        }
        root.ticked = list
    }

    function applyAndClose() {
        mutesModel.apply(root.ticked, root.customText)
        root.ret = { "errcode": 0 }
        root.hide()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: mutesModel.problem !== "" ? mutesModel.problem
                  : mutesModel.flexible
                    ? qsTrc("starscore", "%1 is a Flexible chair: its trumpet, trombone, tuba and other brass sheets show the mutes "
                            + "that instrument has; its sax, flute, clarinet and string sheets show none.").arg(mutesModel.partName)
                    : qsTrc("starscore", "The mutes %1 plays with, printed under the instrument name on its sheets in the "
                            + "order you tick them. Copying this part to the same instrument in another arrangement copies "
                            + "them too.").arg(mutesModel.partName)
        }

        StyledFlickable {
            visible: mutesModel.problem === ""
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: muteColumn.implicitHeight
            clip: true

            Column {
                id: muteColumn
                width: parent.width
                spacing: 6

                Repeater {
                    model: mutesModel.options
                    CheckBox {
                        text: modelData
                        checked: root.ticked.indexOf(modelData) >= 0
                        onClicked: root.toggle(modelData, !checked)
                    }
                }
            }
        }

        ColumnLayout {
            visible: mutesModel.problem === ""
            spacing: 6

            StyledTextLabel { text: qsTrc("starscore", "Other"); font: ui.theme.bodyBoldFont }
            TextInputField {
                Layout.fillWidth: true
                hint: qsTrc("starscore", "e.g. Wah-Wah, Felt Hat (\"Mute\" is added; separate several with commas)")
                currentText: root.customText
                onTextEdited: function(t) { root.customText = t }
                onAccepted: root.applyAndClose()
            }
        }

        StyledTextLabel {
            visible: mutesModel.problem === ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            opacity: 0.7
            readonly property string lines: mutesModel.preview(root.ticked, root.customText)
            text: lines === "" ? qsTrc("starscore", "No mutes: the sheet shows just the instrument name.")
                               : qsTrc("starscore", "Under the instrument name:") + "\n" + lines
        }

        ButtonBox {
            Layout.fillWidth: true
            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("global", "OK")
                accentButton: true
                enabled: mutesModel.problem === ""
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
