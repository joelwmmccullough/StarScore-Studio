/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Dashboard. Every Starsign song's arrangements (3-Horn, 2-Horn, 2- and 3-Horn Flexible,
 * 4- to 7-Horn), which are done, and the list of what to work on next in Joel's priority order.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

Rectangle {
    id: root

    color: ui.theme.backgroundSecondaryColor

    property bool showAllTasks: false
    property string filter: "all"      // "all", "originals", "covers"

    readonly property color doneColor: "#3FB05A"
    readonly property color auditColor: "#8E7CF0"
    readonly property var statusColors: ["#8A8A8A", "#E0463A", "#F29B30", "#3C8CE7", "#3FB05A"]

    function cellColor(cell) {
        if (cell.state === "done") {
            return root.doneColor
        }
        if (cell.state === "todo") {
            return cell.audit ? root.auditColor : root.statusColors[Math.max(0, Math.min(4, cell.status))]
        }
        return "transparent"
    }

    DashboardModel {
        id: dash
    }

    Component.onCompleted: {
        dash.load()
        if (dash.folder !== "" && dash.unscanned > 0) {
            dash.scan(false)
        }
    }

    StyledFlickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: content.implicitHeight + 48
        clip: true

        ColumnLayout {
            id: content
            x: 24
            y: 24
            width: flick.width - 48
            spacing: 18

            // ---------------- Header ----------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: qsTrc("starscore", "Starsign dashboard")
                        font: ui.theme.headerBoldFont
                    }
                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        text: dash.status
                        opacity: 0.8
                    }
                }
                FlatButton {
                    text: qsTrc("starscore", "Refresh")
                    toolTipTitle: qsTrc("starscore", "Refresh")
                    toolTipDescription: qsTrc("starscore", "Reads the songs that changed since they were last read")
                    accentButton: dash.unscanned > 0
                    visible: !dash.scanning
                    enabled: dash.folder !== ""
                    onClicked: dash.scan(false)
                }
                FlatButton {
                    text: qsTrc("starscore", "Read all again")
                    visible: !dash.scanning
                    enabled: dash.folder !== ""
                    onClicked: dash.scan(true)
                }
                FlatButton {
                    text: qsTrc("starscore", "Stop")
                    visible: dash.scanning
                    onClicked: dash.cancel()
                }
                FlatButton {
                    text: qsTrc("starscore", "Folder…")
                    toolTipTitle: qsTrc("starscore", "Songs folder")
                    toolTipDescription: dash.folder
                    enabled: !dash.scanning
                    onClicked: dash.chooseFolder()
                }
            }

            // ---------------- Overall ----------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                visible: dash.totalCount > 0

                RowLayout {
                    Layout.fillWidth: true
                    StyledTextLabel {
                        text: qsTrc("starscore", "Overall")
                        font: ui.theme.bodyBoldFont
                    }
                    Item { Layout.fillWidth: true }
                    StyledTextLabel {
                        text: qsTrc("starscore", "%1 of %2 arrangements done (%3%)").arg(dash.doneCount).arg(dash.totalCount)
                              .arg(dash.totalCount > 0 ? Math.round(100 * dash.doneCount / dash.totalCount) : 0)
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    height: 12
                    radius: 6
                    color: ui.theme.buttonColor
                    Rectangle {
                        width: dash.totalCount > 0 ? parent.width * dash.doneCount / dash.totalCount : 0
                        height: parent.height
                        radius: 6
                        color: root.doneColor
                    }
                }
            }

            // ---------------- Next up + priorities ----------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 18

                // Next up
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 3
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: nextColumn.implicitHeight + 24
                    radius: 6
                    color: ui.theme.backgroundPrimaryColor
                    border.color: ui.theme.strokeColor

                    ColumnLayout {
                        id: nextColumn
                        x: 12
                        y: 12
                        width: parent.width - 24
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            StyledTextLabel {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                text: qsTrc("starscore", "Next up")
                                font: ui.theme.largeBodyBoldFont
                            }
                            FlatButton {
                                text: qsTrc("starscore", "Audit these in order")
                                toolTipTitle: qsTrc("starscore", "Audit these in order")
                                toolTipDescription: qsTrc("starscore", "Opens the songs that need auditing one at a time, in this order, with the Audit panel (Next song moves on)")
                                enabled: dash.tasks.length > 0
                                onClicked: dash.auditInOrder()
                            }
                        }

                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.WordWrap
                            visible: dash.tasks.length === 0
                            text: dash.totalCount > 0 && dash.unscanned === 0 ? qsTrc("starscore", "Everything is done.")
                                                                             : qsTrc("starscore", "Nothing to show until the songs are read.")
                        }

                        Repeater {
                            model: root.showAllTasks ? dash.tasks : dash.tasks.slice(0, 10)

                            delegate: Rectangle {
                                id: taskRow
                                required property var modelData
                                required property int index

                                Layout.fillWidth: true
                                implicitHeight: taskLayout.implicitHeight + 12
                                radius: 4
                                color: taskMouse.containsMouse ? ui.theme.buttonColor : (index === 0 ? Qt.rgba(0.56, 0.49, 0.94, 0.12) : "transparent")

                                MouseArea {
                                    id: taskMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: dash.openTask(taskRow.index)
                                }

                                RowLayout {
                                    id: taskLayout
                                    x: 8
                                    y: 6
                                    width: parent.width - 16
                                    spacing: 10

                                    StyledTextLabel {
                                        Layout.preferredWidth: 28
                                        horizontalAlignment: Text.AlignRight
                                        text: taskRow.modelData.rank
                                        opacity: 0.6
                                    }
                                    Rectangle {
                                        width: 10
                                        height: 10
                                        radius: 5
                                        color: taskRow.modelData.state === "missing" ? "transparent"
                                               : taskRow.modelData.audit ? root.auditColor
                                               : root.statusColors[Math.max(0, Math.min(4, taskRow.modelData.status))]
                                        border.width: 1
                                        border.color: taskRow.modelData.state === "missing" ? ui.theme.fontSecondaryColor : "transparent"
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 1
                                        StyledTextLabel {
                                            Layout.fillWidth: true
                                            horizontalAlignment: Text.AlignLeft
                                            elide: Text.ElideRight
                                            text: taskRow.modelData.song + " — " + taskRow.modelData.action
                                            font: ui.theme.bodyBoldFont
                                        }
                                        StyledTextLabel {
                                            Layout.fillWidth: true
                                            horizontalAlignment: Text.AlignLeft
                                            elide: Text.ElideRight
                                            text: taskRow.modelData.tier + " · " + taskRow.modelData.detail
                                            opacity: 0.7
                                        }
                                    }
                                    StyledTextLabel {
                                        text: taskRow.modelData.plays > 0 ? qsTrc("starscore", "played %1× in 2026").arg(taskRow.modelData.plays) : ""
                                        opacity: 0.6
                                    }
                                }
                            }
                        }

                        FlatButton {
                            visible: dash.tasks.length > 10
                            text: root.showAllTasks ? qsTrc("starscore", "Show the first 10")
                                                    : qsTrc("starscore", "Show all %1").arg(dash.tasks.length)
                            onClicked: root.showAllTasks = !root.showAllTasks
                        }
                    }
                }

                // Priorities
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 2
                    Layout.alignment: Qt.AlignTop
                    implicitHeight: tierColumn.implicitHeight + 24
                    radius: 6
                    color: ui.theme.backgroundPrimaryColor
                    border.color: ui.theme.strokeColor

                    ColumnLayout {
                        id: tierColumn
                        x: 12
                        y: 12
                        width: parent.width - 24
                        spacing: 8

                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("starscore", "By priority")
                            font: ui.theme.largeBodyBoldFont
                        }

                        Repeater {
                            model: dash.tiers

                            delegate: ColumnLayout {
                                id: tierRow
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 2

                                RowLayout {
                                    Layout.fillWidth: true
                                    StyledTextLabel {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignLeft
                                        elide: Text.ElideRight
                                        text: (tierRow.index + 1) + ". " + tierRow.modelData.title
                                        font: tierRow.modelData.done < tierRow.modelData.total ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                                    }
                                    StyledTextLabel {
                                        text: tierRow.modelData.done + " / " + tierRow.modelData.total
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 6
                                    radius: 3
                                    color: ui.theme.buttonColor
                                    Rectangle {
                                        width: tierRow.modelData.total > 0 ? parent.width * tierRow.modelData.done / tierRow.modelData.total : 0
                                        height: parent.height
                                        radius: 3
                                        color: root.doneColor
                                    }
                                }
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    visible: tierRow.modelData.next !== ""
                                    text: qsTrc("starscore", "Next: %1").arg(tierRow.modelData.next)
                                    opacity: 0.6
                                }
                            }
                        }
                    }
                }
            }

            // ---------------- Songs grid ----------------
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: gridColumn.implicitHeight + 24
                radius: 6
                color: ui.theme.backgroundPrimaryColor
                border.color: ui.theme.strokeColor

                ColumnLayout {
                    id: gridColumn
                    x: 12
                    y: 12
                    width: parent.width - 24
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("starscore", "Every song")
                            font: ui.theme.largeBodyBoldFont
                        }
                        FlatButton {
                            text: qsTrc("starscore", "All")
                            accentButton: root.filter === "all"
                            onClicked: root.filter = "all"
                        }
                        FlatButton {
                            text: qsTrc("starscore", "Originals")
                            accentButton: root.filter === "originals"
                            onClicked: root.filter = "originals"
                        }
                        FlatButton {
                            text: qsTrc("starscore", "Covers")
                            accentButton: root.filter === "covers"
                            onClicked: root.filter = "covers"
                        }
                    }

                    // Legend
                    Flow {
                        Layout.fillWidth: true
                        spacing: 14
                        Repeater {
                            model: [
                                { "c": root.doneColor, "t": qsTrc("starscore", "Done (finished, or audited for older songs)") },
                                { "c": root.auditColor, "t": qsTrc("starscore", "To audit (number = things to look at)") },
                                { "c": root.statusColors[2], "t": qsTrc("starscore", "In progress") },
                                { "c": root.statusColors[1], "t": qsTrc("starscore", "Sketch") },
                                { "c": root.statusColors[3], "t": qsTrc("starscore", "Needs review") },
                                { "c": root.statusColors[0], "t": qsTrc("starscore", "Empty") },
                                { "c": "transparent", "t": qsTrc("starscore", "Not written yet") },
                            ]
                            delegate: Row {
                                required property var modelData
                                spacing: 5
                                Rectangle {
                                    width: 12
                                    height: 12
                                    radius: 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: modelData.c
                                    border.width: modelData.c === "transparent" ? 1 : 0
                                    border.color: ui.theme.fontSecondaryColor
                                }
                                StyledTextLabel {
                                    text: modelData.t
                                    opacity: 0.8
                                }
                            }
                        }
                    }

                    // Header row
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        spacing: 6
                        StyledTextLabel {
                            Layout.preferredWidth: 220
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("starscore", "Song")
                            opacity: 0.7
                        }
                        StyledTextLabel {
                            Layout.preferredWidth: 50
                            text: qsTrc("starscore", "Plays")
                            opacity: 0.7
                        }
                        Repeater {
                            model: dash.columns
                            delegate: StyledTextLabel {
                                required property var modelData
                                Layout.preferredWidth: 56
                                text: modelData.short
                                opacity: 0.7
                            }
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("starscore", "Audit preview")
                            opacity: 0.7
                        }
                    }

                    Repeater {
                        model: dash.songs

                        delegate: Rectangle {
                            id: songRow
                            required property var modelData

                            visible: root.filter === "all" || (root.filter === "covers") === songRow.modelData.cover
                            Layout.fillWidth: true
                            implicitHeight: visible ? 34 : 0
                            radius: 3
                            color: rowMouse.containsMouse ? ui.theme.buttonColor : "transparent"

                            MouseArea {
                                id: rowMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: dash.openSong(songRow.modelData.path, songRow.modelData.legacy)
                            }

                            RowLayout {
                                anchors.fill: parent
                                spacing: 6

                                ColumnLayout {
                                    Layout.preferredWidth: 220
                                    spacing: 0
                                    StyledTextLabel {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignLeft
                                        elide: Text.ElideRight
                                        text: songRow.modelData.title
                                        font: ui.theme.bodyBoldFont
                                    }
                                    StyledTextLabel {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignLeft
                                        elide: Text.ElideRight
                                        text: (songRow.modelData.cover ? qsTrc("starscore", "Cover") : qsTrc("starscore", "Original"))
                                              + (songRow.modelData.legacy ? "" : " · " + qsTrc("starscore", "done = Finished"))
                                        opacity: 0.55
                                    }
                                }
                                StyledTextLabel {
                                    Layout.preferredWidth: 50
                                    text: songRow.modelData.plays > 0 ? songRow.modelData.plays : "–"
                                    opacity: 0.8
                                }
                                Repeater {
                                    model: songRow.modelData.cells
                                    delegate: Item {
                                        id: cellItem
                                        required property var modelData
                                        Layout.preferredWidth: 56
                                        Layout.preferredHeight: 26

                                        Rectangle {
                                            anchors.fill: parent
                                            anchors.margins: 2
                                            radius: 4
                                            color: root.cellColor(cellItem.modelData)
                                            opacity: cellItem.modelData.state === "unscanned" ? 0.3 : 1
                                            border.width: cellItem.modelData.state === "missing" || cellItem.modelData.state === "unscanned" ? 1 : 0
                                            border.color: ui.theme.fontSecondaryColor

                                            StyledTextLabel {
                                                anchors.centerIn: parent
                                                width: parent.width - 4
                                                elide: Text.ElideRight
                                                text: cellItem.modelData.text
                                                color: cellItem.modelData.state === "done" || cellItem.modelData.state === "todo"
                                                       ? "#FFFFFF" : ui.theme.fontSecondaryColor
                                                font: ui.theme.bodyBoldFont
                                            }
                                        }

                                        MouseArea {
                                            id: cellMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: dash.openSong(songRow.modelData.path, cellItem.modelData.audit)
                                            onContainsMouseChanged: {
                                                if (containsMouse && cellItem.modelData.tip !== "") {
                                                    ui.tooltip.show(cellItem, songRow.modelData.title, cellItem.modelData.tip)
                                                } else {
                                                    ui.tooltip.hide(cellItem)
                                                }
                                            }
                                        }
                                    }
                                }
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    text: !songRow.modelData.scanned ? qsTrc("starscore", "not read yet")
                                          : songRow.modelData.error !== "" ? songRow.modelData.error
                                          : (songRow.modelData.openIssues > 0
                                             ? qsTrc("starscore", "%1 to look at (%2 likely errors)").arg(songRow.modelData.openIssues).arg(songRow.modelData.likely)
                                             : qsTrc("starscore", "no open issues"))
                                            + (songRow.modelData.listen !== "" ? " · " + qsTrc("starscore", "listened %1").arg(songRow.modelData.listen) : "")
                                    opacity: 0.75
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
