/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — rename a section/arrangement, or choose a section's
 * instruments / an arrangement's sections.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    property string mode: ""
    property string itemId: ""
    property string extra: ""

    title: editModel.dialogTitle

    contentWidth: 420
    contentHeight: editModel.showList ? 480 : 150
    margins: 16

    StarScoreEditModel {
        id: editModel
    }

    Component.onCompleted: {
        editModel.load(root.mode, root.itemId)
        nameField.ensureActiveFocus()
    }

    function applyAndClose() {
        if (editModel.apply()) {
            root.ret = { "errcode": 0 }
            root.hide()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            text: qsTrc("starscore", "Name")
            font: ui.theme.bodyBoldFont
            horizontalAlignment: Text.AlignLeft
        }

        TextInputField {
            id: nameField
            Layout.fillWidth: true
            currentText: editModel.name

            onTextEdited: function(newTextValue) {
                editModel.name = newTextValue
            }
            onAccepted: {
                root.applyAndClose()
            }
        }

        StyledTextLabel {
            visible: editModel.showList
            text: editModel.listTitle
            font: ui.theme.bodyBoldFont
            horizontalAlignment: Text.AlignLeft
        }

        Rectangle {
            visible: editModel.showList
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: ui.theme.textFieldColor
            border.width: 1
            border.color: ui.theme.strokeColor
            radius: 3

            StyledListView {
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2
                model: editModel.items

                delegate: RowLayout {
                    required property var modelData
                    required property int index

                    width: ListView.view.width
                    spacing: 8

                    CheckBox {
                        Layout.fillWidth: true
                        text: modelData.title
                        checked: modelData.checked

                        onClicked: {
                            editModel.setChecked(index, !checked)
                        }
                    }

                    StyledTextLabel {
                        visible: modelData.note !== ""
                        text: modelData.note
                        color: ui.theme.fontSecondaryColor
                    }
                }
            }
        }

        ButtonBox {
            Layout.fillWidth: true

            buttons: [ ButtonBoxModel.Cancel, ButtonBoxModel.Ok ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Ok) {
                    root.applyAndClose()
                } else if (buttonId === ButtonBoxModel.Cancel) {
                    root.reject()
                }
            }
        }
    }
}
