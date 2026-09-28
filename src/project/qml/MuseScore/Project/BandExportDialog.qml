/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Export to Sheets and Demos: tick the sheets to write.
 * Un-ticked sheets are remembered per song for the next export.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Export to Sheets and Demos")

    contentWidth: 620
    contentHeight: 620
    margins: 16

    property bool done: false

    BandExportModel {
        id: exportModel
    }

    Component.onCompleted: exportModel.load()

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
            text: exportModel.errorText !== "" ? exportModel.errorText : exportModel.heading
            wrapMode: Text.WordWrap
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            color: ui.theme.fontSecondaryColor
            visible: exportModel.errorText === "" && !root.done
            text: qsTrc("starscore", "Sheets that already exist are moved to Version History/Superseded <today> first. "
                        + "Your ticks are remembered for this song.")
        }

        RowLayout {
            visible: exportModel.errorText === "" && !root.done
            spacing: 8
            FlatButton { text: qsTrc("starscore", "Tick all"); onClicked: exportModel.setAllChecked(true) }
            FlatButton { text: qsTrc("starscore", "Untick all"); onClicked: exportModel.setAllChecked(false) }
        }

        StyledListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: exportModel.errorText === "" && !root.done
            spacing: 2
            model: exportModel.items

            delegate: Item {
                required property var modelData
                required property int index
                width: list.width
                height: modelData.header ? 34 : 28

                CheckBox {
                    anchors.left: parent.left
                    anchors.leftMargin: modelData.header ? 0 : 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.name
                    font: modelData.header ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                    checked: modelData.checked
                    onClicked: exportModel.setChecked(index, !checked)
                }
            }
        }

        StyledTextLabel {
            id: resultLabel
            Layout.fillWidth: true
            Layout.fillHeight: root.done
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignTop
            wrapMode: Text.WordWrap
            visible: root.done || (exportModel.notes !== "" && exportModel.errorText === "")
            color: root.done ? ui.theme.fontPrimaryColor : ui.theme.fontSecondaryColor
            text: exportModel.notes
        }

        ButtonBox {
            Layout.fillWidth: true
            buttons: [ ButtonBoxModel.Close ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                visible: exportModel.errorText === "" && !root.done
                enabled: exportModel.checkedCount > 0
                text: qsTrc("starscore", "Export %n sheet(s)", "", exportModel.checkedCount)
                accentButton: true
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: {
                    resultLabel.text = qsTrc("starscore", "Exporting…")
                    root.done = true
                    resultLabel.text = exportModel.exportNow()
                }
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Close) {
                    root.hide()
                }
            }
        }
    }
}
