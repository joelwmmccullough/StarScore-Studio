/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Compare parts: for each instrument used in more than one section, one strip of
 * bars per part. Orange bars differ from the reference part; grey bars match; pale bars are rests in both.
 * Click a part's name to make it the reference; click a bar to select it in the main score.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Compare parts")

    contentWidth: 980
    contentHeight: 640
    margins: 16

    readonly property color sameColor: ui.theme.buttonColor
    readonly property color diffColor: "#E8892B"
    readonly property color emptyColor: Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g, ui.theme.fontPrimaryColor.b, 0.06)

    ComparePartsModel {
        id: compareModel
    }

    Component.onCompleted: compareModel.load()

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            text: compareModel.groups.length === 0
                  ? qsTrc("starscore", "No instrument is used in more than one section.")
                  : qsTrc("starscore", "Orange bars differ from the reference part (★). Grey bars are the same; pale bars are rests in both. "
                          + "Click a part's name to compare the others with it. Click a bar to select it in the main score.")
        }

        StyledFlickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: groupsColumn.height
            clip: true

            Column {
                id: groupsColumn
                width: parent.width
                spacing: 18

                Repeater {
                    model: compareModel.groups

                    delegate: Column {
                        id: group
                        required property var modelData
                        width: groupsColumn.width
                        spacing: 6

                        StyledTextLabel {
                            text: group.modelData.instrument
                            font: ui.theme.largeBodyBoldFont
                        }

                        Repeater {
                            model: group.modelData.parts

                            delegate: RowLayout {
                                id: row
                                required property var modelData
                                width: group.width
                                spacing: 10

                                Column {
                                    Layout.preferredWidth: 280
                                    spacing: 2

                                    FlatButton {
                                        width: 280
                                        transparent: true
                                        text: (row.modelData.isReference ? "★ " : "") + row.modelData.label
                                        onClicked: compareModel.setReference(group.modelData.instrument, row.modelData.partId)
                                    }
                                    StyledTextLabel {
                                        width: 280
                                        horizontalAlignment: Text.AlignLeft
                                        wrapMode: Text.WordWrap
                                        font: ui.theme.bodyFont
                                        color: ui.theme.fontSecondaryColor
                                        text: row.modelData.summary
                                    }
                                }

                                Item {
                                    id: strip
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 22
                                    readonly property int count: Math.max(1, row.modelData.bars.length)
                                    readonly property real cell: width / count

                                    Row {
                                        anchors.fill: parent
                                        Repeater {
                                            model: row.modelData.bars
                                            delegate: Rectangle {
                                                required property var modelData
                                                required property int index
                                                width: strip.cell
                                                height: strip.height
                                                color: modelData === 1 ? root.diffColor : (modelData === 2 ? root.emptyColor : root.sameColor)
                                                border.width: strip.cell > 6 ? 1 : 0
                                                border.color: ui.theme.backgroundPrimaryColor

                                                MouseArea {
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    onClicked: compareModel.selectBar(row.modelData.partId, index + 1)
                                                    ToolTip.visible: containsMouse
                                                    ToolTip.text: qsTrc("starscore", "Bar %1").arg(index + 1)
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        ButtonBox {
            Layout.fillWidth: true
            buttons: [ ButtonBoxModel.Close ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("starscore", "Refresh")
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: compareModel.load()
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Close) {
                    root.hide()
                }
            }
        }
    }
}
