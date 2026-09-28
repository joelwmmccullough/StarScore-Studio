/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — add a solo transcription to the open .starscore
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Add solo transcription")

    contentWidth: 520
    contentHeight: 420
    margins: 20

    AddSoloModel {
        id: soloModel
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("starscore", "The transcription is stored inside this .starscore and gets its own view, numbered from bar 1. "
                        + "The band's music for those bars is copied in (repeats written out) as sections you can show, hide and hear.")
        }

        StyledTextLabel { text: qsTrc("starscore", "Transcription file"); font: ui.theme.bodyBoldFont }
        RowLayout {
            Layout.fillWidth: true
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                elide: Text.ElideMiddle
                text: soloModel.filePath !== "" ? soloModel.filePath : qsTrc("starscore", "None chosen")
            }
            FlatButton { text: qsTrc("starscore", "Choose…"); onClicked: soloModel.chooseFile() }
        }

        StyledTextLabel { text: qsTrc("starscore", "Name"); font: ui.theme.bodyBoldFont }
        TextInputField {
            Layout.fillWidth: true
            currentText: soloModel.name
            hint: qsTrc("starscore", "e.g. Christian – tenor solo")
            onTextEdited: function(t) { soloModel.name = t }
        }

        RowLayout {
            spacing: 16
            ColumnLayout {
                StyledTextLabel { text: qsTrc("starscore", "Starts at bar"); font: ui.theme.bodyBoldFont }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 100
                    currentValue: soloModel.startBar
                    step: 1; decimals: 0; minValue: 1; maxValue: 9999
                    onValueEdited: function(v) { soloModel.startBar = v }
                }
            }
            ColumnLayout {
                StyledTextLabel { text: qsTrc("starscore", "Ends at bar (0 = automatic)"); font: ui.theme.bodyBoldFont }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 100
                    currentValue: soloModel.endBar
                    step: 1; decimals: 0; minValue: 0; maxValue: 9999
                    onValueEdited: function(v) { soloModel.endBar = v }
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: soloModel.summary
        }
        StyledTextLabel {
            id: warningLabel
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            color: "#E0463A"
            text: soloModel.warning
        }

        Item { Layout.fillHeight: true }

        ButtonBox {
            Layout.fillWidth: true
            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("starscore", "Add solo")
                accentButton: true
                enabled: soloModel.canAdd
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: {
                    var err = soloModel.add()
                    if (err === "") {
                        root.ret = { "errcode": 0 }
                        root.hide()
                    } else {
                        warningLabel.text = err
                    }
                }
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) {
                    root.reject()
                }
            }
        }
    }
}
