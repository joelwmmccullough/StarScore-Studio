/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — To-do panel: the open song's work in priority order. Each step shows how far along it is
 * (its least finished part) and, until it's finished, which parts still need work. "Next up" is the first step
 * that isn't finished.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

Item {
    id: root

    property NavigationSection navigationSection: null
    property int navigationOrderStart: 1

    // empty / sketch / in progress / needs review / finished, as on the StarScore bar and the Dashboard
    readonly property var statusColors: ["#8A8A8A", "#E0463A", "#F29B30", "#3C8CE7", "#3FB05A"]

    TodoPanelModel {
        id: todoModel
    }

    Component.onCompleted: todoModel.load()
    onVisibleChanged: todoModel.setActive(root.visible)

    StyledTextLabel {
        anchors.centerIn: parent
        visible: !todoModel.hasScore
        text: qsTrc("starscore", "Open a song to see its to-do list.")
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8
        visible: todoModel.hasScore

        // Next up
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: nextColumn.implicitHeight + 16
            radius: 4
            color: ui.theme.backgroundSecondaryColor
            border.color: ui.theme.accentColor
            border.width: 1

            ColumnLayout {
                id: nextColumn
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    text: todoModel.nextUp !== "" ? qsTrc("starscore", "Next up") : qsTrc("starscore", "All done")
                    color: ui.theme.fontSecondaryColor
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    font: ui.theme.largeBodyBoldFont
                    text: todoModel.nextUp !== "" ? todoModel.nextUp
                                                  : qsTrc("starscore", "Every step is marked Finished.")
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    color: ui.theme.fontSecondaryColor
                    text: qsTrc("starscore", "%1 of %2 steps finished").arg(todoModel.doneCount).arg(todoModel.totalCount)
                }
            }
        }

        StyledListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 4
            model: todoModel.items

            delegate: Rectangle {
                id: row
                required property var modelData
                width: ListView.view.width
                implicitHeight: rowColumn.implicitHeight + 10
                radius: 3
                color: modelData.title === todoModel.nextUp ? ui.theme.buttonColor : "transparent"
                opacity: modelData.done ? 0.6 : 1.0

                ColumnLayout {
                    id: rowColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        StyledTextLabel {
                            Layout.preferredWidth: 20
                            horizontalAlignment: Text.AlignRight
                            color: ui.theme.fontSecondaryColor
                            text: row.modelData.rank + "."
                        }
                        Rectangle {
                            width: 10
                            height: 10
                            radius: 5
                            color: row.modelData.missing ? "transparent"
                                                         : root.statusColors[Math.max(0, Math.min(4, row.modelData.status))]
                            border.color: row.modelData.missing ? ui.theme.fontSecondaryColor : "transparent"
                            border.width: 1
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            font: ui.theme.bodyBoldFont
                            text: row.modelData.title + (row.modelData.done ? "  ✓" : "")
                        }
                        StyledTextLabel {
                            horizontalAlignment: Text.AlignRight
                            color: ui.theme.fontSecondaryColor
                            text: row.modelData.statusText
                        }
                    }

                    // What still needs work
                    Repeater {
                        model: row.modelData.done ? [] : row.modelData.details
                        StyledTextLabel {
                            required property var modelData
                            Layout.fillWidth: true
                            Layout.leftMargin: 46
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.WordWrap
                            color: ui.theme.fontSecondaryColor
                            text: "• " + modelData
                        }
                    }
                    StyledTextLabel {
                        Layout.fillWidth: true
                        Layout.leftMargin: 46
                        visible: row.modelData.note !== ""
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        color: ui.theme.fontSecondaryColor
                        font.italic: true
                        text: row.modelData.note
                    }
                }
            }
        }
    }
}
