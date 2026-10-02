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

    // "copy": Copy part formatting (from the part being viewed, or any other, into the parts you tick).
    // "to" / "from" are the older two dialogs, kept for anything that still opens them.
    property string mode: "copy"

    title: qsTrc("starscore", "Copy part formatting")

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

        // "From another part": formatting goes into the part being viewed only
        StyledTextLabel {
            Layout.fillWidth: true
            visible: root.mode === "from"
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            text: qsTrc("starscore", "Copy into: %1").arg(layoutModel.targets.filter(function(t) { return t.checked })
                                                         .map(function(t) { return t.title }).join(", "))
        }

        Item {
            Layout.fillHeight: true
            visible: root.mode === "from"
        }

        RowLayout {
            Layout.fillWidth: true
            visible: root.mode !== "from"

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
            visible: root.mode !== "from"
            color: ui.theme.textFieldColor
            border.width: 1
            border.color: ui.theme.strokeColor
            radius: 3

            StyledListView {
                id: targetList
                anchors.fill: parent
                anchors.margins: 6
                spacing: 2
                model: layoutModel.targets

                // Ticking a part rebuilds the list, which would scroll it back to the top: keep the scroll position
                property real keptY: -1
                Connections {
                    target: layoutModel
                    function onTargetsChanged() {
                        if (targetList.keptY < 0) {
                            return
                        }
                        const y = targetList.keptY
                        targetList.keptY = -1
                        Qt.callLater(function() { targetList.contentY = y })
                    }
                }

                delegate: CheckBox {
                    required property var modelData
                    required property int index

                    width: ListView.view.width
                    text: modelData.title
                    checked: modelData.checked
                    enabled: modelData.enabled

                    onClicked: {
                        targetList.keptY = targetList.contentY
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
