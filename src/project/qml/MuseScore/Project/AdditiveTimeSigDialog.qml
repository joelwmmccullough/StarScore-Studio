/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — additive time signature (e.g. 4+4+4+3 / 8)
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Additive time signature")

    contentWidth: 440
    contentHeight: 300
    margins: 20

    property string numerators: "4+4+4+3"
    property int denominator: 8

    AdditiveTimeSigModel {
        id: tsModel
    }

    Component.onCompleted: topField.ensureActiveFocus()

    function applyAndClose() {
        var err = tsModel.apply(root.numerators, root.denominator)
        if (err === "") {
            root.ret = { "errcode": 0 }
            root.hide()
        } else {
            errorLabel.text = err
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("starscore", "4+4+4+3 over 8 means three bars of 4/8 then a bar of 3/8, repeating. "
                        + "The first bar shows 4+4+4+3/8; the later changes are hidden.")
        }

        RowLayout {
            spacing: 10
            ColumnLayout {
                StyledTextLabel { text: qsTrc("starscore", "Top"); font: ui.theme.bodyBoldFont }
                TextInputField {
                    id: topField
                    Layout.preferredWidth: 180
                    currentText: root.numerators
                    onTextEdited: function(t) { root.numerators = t }
                    onAccepted: root.applyAndClose()
                }
            }
            ColumnLayout {
                StyledTextLabel { text: qsTrc("starscore", "Bottom"); font: ui.theme.bodyBoldFont }
                StyledDropdown {
                    Layout.preferredWidth: 80
                    model: [ { "text": "2", "value": 2 }, { "text": "4", "value": 4 }, { "text": "8", "value": 8 },
                             { "text": "16", "value": 16 }, { "text": "32", "value": 32 } ]
                    currentIndex: indexOfValue(root.denominator)
                    onActivated: function(index, value) { root.denominator = value }
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            color: ui.theme.fontSecondaryColor
            text: tsModel.rangeText
        }

        StyledTextLabel {
            id: errorLabel
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            color: "#E0463A"
        }

        Item { Layout.fillHeight: true }

        ButtonBox {
            Layout.fillWidth: true
            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("starscore", "Apply")
                accentButton: true
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
