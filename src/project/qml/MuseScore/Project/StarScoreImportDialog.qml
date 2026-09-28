/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Import a MuseScore file: choose each instrument's section and the arrangements.
 * Cancelling closes the file; importing asks whether to standardize it and makes the next save a new .starscore.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Import into StarScore")

    contentWidth: 720
    contentHeight: 620
    margins: 16

    property bool imported: false

    StarScoreImportModel {
        id: importModel
    }

    Component.onCompleted: importModel.load()

    onClosed: {
        if (!root.imported) {
            Qt.callLater(importModel.cancelImport)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            text: qsTrc("starscore", "This is a MuseScore file. Choose the section each instrument belongs to and the arrangements to create. After importing, saving makes a new .starscore file; the original file is not changed.")
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            font: ui.theme.bodyBoldFont
            text: importModel.summary
        }

        StyledTextLabel {
            text: qsTrc("starscore", "Instruments")
            font: ui.theme.bodyBoldFont
        }

        StyledListView {
            id: partList
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            clip: true
            model: importModel.parts

            delegate: RowLayout {
                required property var modelData
                required property int index
                width: partList.width
                spacing: 12

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    text: modelData.name + (modelData.staves > 1 ? "  (" + modelData.staves + " " + qsTrc("starscore", "staves") + ")" : "")
                }

                StyledDropdown {
                    Layout.preferredWidth: 260
                    model: importModel.sectionChoices
                    currentIndex: indexOfValue(modelData.section)

                    onActivated: function(i, value) {
                        importModel.setPartSection(index, value)
                    }
                }
            }
        }

        StyledTextLabel {
            text: qsTrc("starscore", "Arrangements")
            font: ui.theme.bodyBoldFont
        }

        StyledTextLabel {
            visible: importModel.arrangements.length === 0
            color: ui.theme.fontSecondaryColor
            text: qsTrc("starscore", "Assign a horn (or other) section to make an arrangement.")
        }

        Flow {
            Layout.fillWidth: true
            spacing: 16

            Repeater {
                model: importModel.arrangements
                delegate: CheckBox {
                    required property var modelData
                    text: modelData.name
                    checked: modelData.checked
                    onClicked: importModel.setArrangementChecked(modelData.key, !checked)
                }
            }
        }

        ButtonBox {
            Layout.fillWidth: true
            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("starscore", "Import")
                accentButton: true
                buttonRole: ButtonBoxModel.AcceptRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: {
                    if (importModel.doImport()) {
                        root.imported = true
                        root.hide()
                    }
                }
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) {
                    root.hide()
                }
            }
        }
    }
}
