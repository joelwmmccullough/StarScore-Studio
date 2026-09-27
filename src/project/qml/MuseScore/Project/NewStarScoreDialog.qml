/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — New StarScore
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "New StarScore")

    contentWidth: 460
    contentHeight: 470
    margins: 20

    NewStarScoreModel {
        id: newModel
    }

    property string scoreTitle: ""
    property string composer: ""
    property int keyFifths: 0
    property int timeNum: 4
    property int timeDen: 4
    property int tempo: 120
    property int measures: 32
    property string arrangementKey: "3-horn-standard"

    Component.onCompleted: {
        titleField.ensureActiveFocus()
    }

    function create() {
        if (newModel.create({
            "title": root.scoreTitle,
            "composer": root.composer,
            "keyFifths": root.keyFifths,
            "timeSigNumerator": root.timeNum,
            "timeSigDenominator": root.timeDen,
            "tempoBpm": root.tempo,
            "measures": root.measures,
            "arrangementTemplateKey": root.arrangementKey
        })) {
            root.ret = { "errcode": 0 }
            root.hide()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel { text: qsTrc("starscore", "Title"); font: ui.theme.bodyBoldFont }
        TextInputField {
            id: titleField
            Layout.fillWidth: true
            hint: qsTrc("starscore", "Song title")
            onTextEdited: function(t) { root.scoreTitle = t }
            onAccepted: root.create()
        }

        StyledTextLabel { text: qsTrc("starscore", "Composer"); font: ui.theme.bodyBoldFont }
        TextInputField {
            Layout.fillWidth: true
            hint: qsTrc("starscore", "Composer")
            onTextEdited: function(t) { root.composer = t }
            onAccepted: root.create()
        }

        StyledTextLabel { text: qsTrc("starscore", "Starting arrangement"); font: ui.theme.bodyBoldFont }
        StyledDropdown {
            Layout.fillWidth: true
            model: newModel.arrangementTemplates
            currentIndex: indexOfValue(root.arrangementKey)
            onActivated: function(index, value) { root.arrangementKey = value }
        }

        StyledTextLabel { text: qsTrc("starscore", "Key signature"); font: ui.theme.bodyBoldFont }
        StyledDropdown {
            Layout.fillWidth: true
            model: newModel.keys
            currentIndex: indexOfValue(root.keyFifths)
            onActivated: function(index, value) { root.keyFifths = value }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                StyledTextLabel { text: qsTrc("starscore", "Time signature"); font: ui.theme.bodyBoldFont }
                RowLayout {
                    IncrementalPropertyControl {
                        Layout.preferredWidth: 64
                        currentValue: root.timeNum
                        step: 1; decimals: 0; minValue: 1; maxValue: 63
                        onValueEdited: function(v) { root.timeNum = v }
                    }
                    StyledTextLabel { text: "/" }
                    StyledDropdown {
                        Layout.preferredWidth: 64
                        model: [ { "text": "1", "value": 1 }, { "text": "2", "value": 2 }, { "text": "4", "value": 4 },
                                 { "text": "8", "value": 8 }, { "text": "16", "value": 16 }, { "text": "32", "value": 32 } ]
                        currentIndex: indexOfValue(root.timeDen)
                        onActivated: function(index, value) { root.timeDen = value }
                    }
                }
            }

            ColumnLayout {
                StyledTextLabel { text: qsTrc("starscore", "Tempo (♩ =)"); font: ui.theme.bodyBoldFont }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: root.tempo
                    step: 1; decimals: 0; minValue: 20; maxValue: 400
                    onValueEdited: function(v) { root.tempo = v }
                }
            }

            ColumnLayout {
                StyledTextLabel { text: qsTrc("starscore", "Bars"); font: ui.theme.bodyBoldFont }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: root.measures
                    step: 1; decimals: 0; minValue: 1; maxValue: 999
                    onValueEdited: function(v) { root.measures = v }
                }
            }
        }

        Item { Layout.fillHeight: true }

        ButtonBox {
            Layout.fillWidth: true

            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("starscore", "Create")
                accentButton: true
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: root.create()
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) {
                    root.reject()
                }
            }
        }
    }
}
