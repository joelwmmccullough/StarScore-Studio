/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — copy layout breaks from one part (or the main score) to others.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    // "to": apply this part's system formatting to other parts; "from": take it from another part
    property string mode: "to"

    title: mode === "from" ? qsTrc("starscore", "Apply system formatting from another part")
                           : qsTrc("starscore", "Apply this system formatting to other parts")

    contentWidth: 480
    contentHeight: 600
    margins: 16

    CopyLayoutModel {
        id: layoutModel
    }

    Component.onCompleted: {
        layoutModel.load(root.mode)
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("starscore", "Copies system breaks, page breaks, \u201ckeep measures on the same system\u201d and system locks, matched bar by bar. Undo reverts all changes in one step.")
        }

        StyledTextLabel {
            text: qsTrc("starscore", "Copy from")
            font: ui.theme.bodyBoldFont
        }

        StyledDropdown {
            Layout.fillWidth: true
            model: layoutModel.sources.map(function(title, i) { return { "text": title, "value": i } })
            currentIndex: layoutModel.sourceIndex

            onActivated: function(index, value) {
                layoutModel.sourceIndex = index
            }
        }

        RowLayout {
            Layout.fillWidth: true

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "Copy to")
                font: ui.theme.bodyBoldFont
            }

            FlatButton {
                text: qsTrc("starscore", "All")
                onClicked: layoutModel.setAllTargets(true)
            }

            FlatButton {
                text: qsTrc("starscore", "None")
                onClicked: layoutModel.setAllTargets(false)
            }
        }

        Rectangle {
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
                model: layoutModel.targets

                delegate: CheckBox {
                    required property var modelData
                    required property int index

                    width: ListView.view.width
                    text: modelData.title
                    checked: modelData.checked
                    enabled: modelData.enabled

                    onClicked: {
                        layoutModel.setTargetChecked(index, !checked)
                    }
                }
            }
        }

        StyledTextLabel {
            text: qsTrc("starscore", "What to copy")
            font: ui.theme.bodyBoldFont
        }

        CheckBox {
            text: qsTrc("starscore", "Line breaks")
            checked: layoutModel.lineBreaks
            onClicked: layoutModel.lineBreaks = !checked
        }
        CheckBox {
            text: qsTrc("starscore", "Page breaks")
            checked: layoutModel.pageBreaks
            onClicked: layoutModel.pageBreaks = !checked
        }
        CheckBox {
            text: qsTrc("starscore", "“Keep measures on the same system” markers")
            checked: layoutModel.keepTogether
            onClicked: layoutModel.keepTogether = !checked
        }
        CheckBox {
            text: qsTrc("starscore", "System locks")
            checked: layoutModel.systemLocks
            onClicked: layoutModel.systemLocks = !checked
        }
        CheckBox {
            text: qsTrc("starscore", "Remove breaks in the targets that the source doesn't have")
            checked: layoutModel.replaceExisting
            onClicked: layoutModel.replaceExisting = !checked
        }

        StyledTextLabel {
            id: resultLabel
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            color: ui.theme.fontSecondaryColor
        }

        ButtonBox {
            Layout.fillWidth: true

            buttons: [ ButtonBoxModel.Close ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("starscore", "Copy breaks")
                accentButton: true
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1

                onClicked: {
                    resultLabel.text = layoutModel.apply()
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
