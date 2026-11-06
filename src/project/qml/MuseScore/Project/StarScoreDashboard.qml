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
    // (1.29.0) the song library: what's typed in its search box, and the tag picked (empty: any)
    property string search: ""
    property string tagFilter: ""

    function libraryShows(row) {
        if (root.filter !== "all" && (root.filter === "covers") !== row.cover) {
            return false
        }
        if (root.tagFilter !== "" && (row.tags || []).indexOf(root.tagFilter) < 0) {
            return false
        }
        var q = root.search.trim().toLowerCase()
        if (q === "") {
            return true
        }
        var hay = [row.title, row.kindName, row.stageName, row.facts].concat(row.tags || []).join(" ").toLowerCase()
        return q.split(/\s+/).every(function(w) { return hay.indexOf(w) >= 0 })
    }

    function allTags() {
        var seen = {}
        var out = []
        for (var i = 0; i < dash.songs.length; ++i) {
            var t = dash.songs[i].tags || []
            for (var j = 0; j < t.length; ++j) {
                if (!seen[t[j]]) {
                    seen[t[j]] = true
                    out.push(t[j])
                }
            }
        }
        return out.sort()
    }

    readonly property color doneColor: "#3FB05A"
    readonly property color auditColor: "#8E7CF0"
    readonly property color learnedColor: "#E3A72B"   // a solo's key learned (memorized = doneColor)
    readonly property var statusColors: dash.statusColors   // (starscorestatus.h: the one table of the status colours)

    // "Every song" in a narrow Dashboard (it's half of Home, beside the recent scores): a narrower song column, no
    // Plays / Audit preview columns (the cells' tooltips say the rest), and cells that shrink to fit (down to 30 px),
    // instead of the last columns being cut off (1.24.0)
    readonly property bool narrowGrid: flick.width < 1000
    readonly property int songColumnWidth: root.narrowGrid ? 140 : 220
    readonly property int cellWidth: root.narrowGrid
                                     ? Math.max(30, Math.min(56, Math.floor((content.width - 40 - root.songColumnWidth)
                                                                            / Math.max(1, dash.columns.length)) - 6))
                                     : 56

    // dark or light text on a cell, whichever reads (white on the yellow "Written, unformatted" didn't)
    function textOn(c) {
        var k = Qt.lighter(c, 1.0)
        return (0.299 * k.r + 0.587 * k.g + 0.114 * k.b) > 0.6 ? "#000000" : "#FFFFFF"
    }

    function cellColor(cell) {
        if (cell.state === "done") {
            return root.doneColor
        }
        if (cell.state === "todo") {
            return cell.audit ? root.auditColor : root.statusColors[Math.max(0, Math.min(5, cell.status))]
        }
        return "transparent"
    }

    DashboardModel {
        id: dash
    }

    SoloLearningModel {
        id: solosModel
    }
    // (another window's export or mark is in the file, not in this window's list)
    onVisibleChanged: {
        if (visible) {
            solosModel.load()
        }
    }

    Component.onCompleted: {
        solosModel.load()
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
            // (text above, buttons below and wrapping: the dashboard is the right half of Home since 1.18.3)
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    text: qsTrc("starscore", "Songs")
                    font: ui.theme.headerBoldFont
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    text: dash.status
                    opacity: 0.8
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 10
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
            }

            // ---------------- The song library (1.29.0) ----------------
            // Every song as a card: click it to open the song on its Song page
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 10
                visible: dash.songs.length > 0

                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    TextInputField {
                        width: 220
                        hint: qsTrc("starscore", "Search songs, tags, keys…")
                        clearTextButtonVisible: true
                        onTextChanged: function(newText) { root.search = newText }
                        onTextCleared: root.search = ""
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
                // the tags used, to pick one
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: tagRepeater.count > 0
                    Repeater {
                        id: tagRepeater
                        model: root.allTags()
                        delegate: Rectangle {
                            required property string modelData
                            height: 24
                            width: tagText.implicitWidth + 18
                            radius: 12
                            color: root.tagFilter === modelData ? ui.theme.accentColor : ui.theme.buttonColor
                            StyledTextLabel {
                                id: tagText
                                anchors.centerIn: parent
                                text: modelData
                            }
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.tagFilter = root.tagFilter === modelData ? "" : modelData
                            }
                        }
                    }
                }

                Flow {
                    id: libraryFlow
                    Layout.fillWidth: true
                    spacing: 10

                    Repeater {
                        model: dash.songs
                        delegate: Rectangle {
                            id: songCard
                            required property var modelData
                            visible: root.libraryShows(modelData)
                            width: visible ? Math.max(200, Math.min(260, (libraryFlow.width - 30) / 4)) : 0
                            height: visible ? cardColumn.implicitHeight + 20 : 0
                            radius: 8
                            color: cardMouse.containsMouse ? ui.theme.buttonColor : ui.theme.backgroundPrimaryColor
                            border.color: ui.theme.strokeColor

                            MouseArea {
                                id: cardMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: dash.openSong(songCard.modelData.path, false)
                            }

                            ColumnLayout {
                                id: cardColumn
                                x: 10
                                y: 10
                                width: parent.width - 20
                                spacing: 3

                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    text: songCard.modelData.title
                                    font: ui.theme.bodyBoldFont
                                }
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    text: songCard.modelData.kindName + (songCard.modelData.stageName ? " · " + songCard.modelData.stageName : "")
                                          + (songCard.modelData.plays > 0 ? " · " + qsTrc("starscore", "played %1×").arg(songCard.modelData.plays) : "")
                                    opacity: 0.6
                                }
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    visible: songCard.modelData.facts !== ""
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    text: songCard.modelData.facts
                                    opacity: 0.6
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    visible: (songCard.modelData.tags || []).length > 0
                                    Repeater {
                                        model: songCard.modelData.tags || []
                                        delegate: Rectangle {
                                            required property string modelData
                                            height: 18
                                            width: smallTag.implicitWidth + 12
                                            radius: 9
                                            color: ui.theme.buttonColor
                                            StyledTextLabel {
                                                id: smallTag
                                                anchors.centerIn: parent
                                                text: modelData
                                                opacity: 0.85
                                            }
                                        }
                                    }
                                }
                                // how many of its arrangements are done
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.topMargin: 4
                                    visible: songCard.modelData.scanned && songCard.modelData.cellCount > 0
                                    height: 5
                                    radius: 2
                                    color: ui.theme.strokeColor
                                    Rectangle {
                                        width: parent.width * songCard.modelData.done / Math.max(1, songCard.modelData.cellCount)
                                        height: parent.height
                                        radius: 2
                                        color: root.doneColor
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // ---------------- Keeping the folders in order ----------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    opacity: 0.8
                    text: qsTrc("starscore", "Sheets and Demos and Projects and Sheets are tidied after every export. "
                                + "Run it on its own after dropping files into an Inbox.")
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 10
                    FlatButton {
                        text: qsTrc("starscore", "Run folder organization")
                        onClicked: dash.runAction("starscore-organize")
                    }
                    FlatButton {
                        text: qsTrc("starscore", "Update all sheets")
                        toolTipTitle: qsTrc("starscore", "Update all sheets")
                        toolTipDescription: qsTrc("starscore", "After a StarScore update that changes how sheets are made: makes every song's Finished sheets again and writes the ones that change, with their version raised")
                        onClicked: dash.openUpdateAllSheets()
                    }
                    FlatButton {
                        text: qsTrc("starscore", "Apply default style settings to every song")
                        toolTipTitle: qsTrc("starscore", "Apply default style settings to every song")
                        toolTipDescription: qsTrc("starscore", "Opens each song in turn, applies Format › Apply default style settings to it and saves it")
                        onClicked: dash.openApplyStylesToAll()
                    }
                    FlatButton {
                        text: qsTrc("starscore", "Rebuild everything")
                        toolTipTitle: qsTrc("starscore", "Rebuild everything")
                        toolTipDescription: qsTrc("starscore", "Rebuilds every generated PDF in both folders")
                        onClicked: dash.runAction("starscore-rebuild-all")
                    }
                    FlatButton {
                        text: qsTrc("starscore", "Band roster…")
                        onClicked: dash.runAction("starscore-band-roster")
                    }
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
            // side by side when there's room, one above the other in a narrow dashboard
            GridLayout {
                Layout.fillWidth: true
                columns: content.width >= 640 ? 2 : 1
                columnSpacing: 18
                rowSpacing: 18

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
                                toolTipDescription: dash.auditTaskCount > 0
                                                    ? qsTrc("starscore", "Opens the songs that need auditing one at a time, in this order, with the Audit panel (Next song moves on)")
                                                    : qsTrc("starscore", "Nothing in Next up needs auditing")
                                enabled: dash.auditTaskCount > 0
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

                                readonly property bool showHeading: modelData.firstOfGroup && modelData.group === "write"

                                Layout.fillWidth: true
                                implicitHeight: taskLayout.implicitHeight + 12 + (showHeading ? groupHeading.implicitHeight + 14 : 0)

                                StyledTextLabel {
                                    id: groupHeading
                                    visible: taskRow.showHeading
                                    x: 8
                                    y: 6
                                    width: parent.width - 16
                                    horizontalAlignment: Text.AlignLeft
                                    wrapMode: Text.WordWrap
                                    text: taskRow.index === 0
                                          ? qsTrc("starscore", "Every existing arrangement is done. Arrangements to write next:")
                                          : qsTrc("starscore", "Then, arrangements to write:")
                                    font: ui.theme.bodyBoldFont
                                    opacity: 0.8
                                }
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
                                    y: 6 + (taskRow.showHeading ? groupHeading.implicitHeight + 14 : 0)
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
                                               : root.statusColors[Math.max(0, Math.min(5, taskRow.modelData.status))]
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

            // ---------------- Solo transcriptions (1.25.0) ----------------
            // every solo whose sheets have been exported; each key clicked through not yet -> learned -> memorized
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: solosModel.solos.length > 0

                RowLayout {
                    Layout.fillWidth: true
                    StyledTextLabel {
                        text: qsTrc("starscore", "Solo transcriptions")
                        font: ui.theme.bodyBoldFont
                    }
                    Item { Layout.fillWidth: true }
                    StyledTextLabel {
                        text: qsTrc("starscore", "%1 of %2 keys memorized").arg(solosModel.keysMemorized).arg(solosModel.keysTotal)
                        opacity: 0.8
                    }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    text: qsTrc("starscore", "Click a key to mark it learned, again for memorized, once more to clear it.")
                    opacity: 0.6
                }

                Repeater {
                    model: solosModel.solos

                    delegate: Rectangle {
                        id: soloRow
                        required property var modelData

                        Layout.fillWidth: true
                        implicitHeight: soloLayout.implicitHeight + 16
                        radius: 6
                        color: ui.theme.backgroundPrimaryColor
                        border.width: 1
                        border.color: ui.theme.strokeColor

                        ColumnLayout {
                            id: soloLayout
                            x: 10
                            y: 8
                            width: parent.width - 20
                            spacing: 6

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8

                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    horizontalAlignment: Text.AlignLeft
                                    elide: Text.ElideRight
                                    text: soloRow.modelData.player + " – " + soloRow.modelData.song
                                          + (soloRow.modelData.instrument ? "  ·  " + soloRow.modelData.instrument : "")
                                    font: ui.theme.bodyBoldFont

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: solosModel.openSolo(soloRow.modelData.path)
                                    }
                                }
                                StyledTextLabel {
                                    text: soloRow.modelData.memorized === soloRow.modelData.keys.length
                                          ? qsTrc("starscore", "all %1 keys memorized").arg(soloRow.modelData.keys.length)
                                          : qsTrc("starscore", "%1 memorized · %2 learned").arg(soloRow.modelData.memorized)
                                            .arg(soloRow.modelData.learned)
                                    opacity: 0.75
                                }
                            }

                            // its audio and Transcribe! files (1.25.0): click to play (or open in Transcribe!)
                            Flow {
                                Layout.fillWidth: true
                                spacing: 4

                                Repeater {
                                    model: soloRow.modelData.audio || []

                                    delegate: FlatButton {
                                        required property var modelData
                                        text: (modelData.kind === "transcribe" ? "T! " : modelData.kind === "video" ? "▶ " : "♪ ") + modelData.name
                                        onClicked: solosModel.playAudio(soloRow.modelData.path, modelData.id)
                                    }
                                }
                                FlatButton {
                                    text: qsTrc("starscore", "+ Audio, video or Transcribe! file")
                                    transparent: true
                                    toolTipTitle: qsTrc("starscore", "Add recordings, a video or a Transcribe! file to the solo")
                                    toolTipDescription: qsTrc("starscore", "The full mix, the piano stem, everything but piano, the video, a .xsc…: "
                                                              + "stored inside the solo's .starscore")
                                    onClicked: solosModel.addAudio(soloRow.modelData.path)
                                }
                            }

                            Flow {
                                Layout.fillWidth: true
                                spacing: 4

                                Repeater {
                                    model: soloRow.modelData.keys

                                    delegate: Rectangle {
                                        id: keyCell
                                        required property var modelData

                                        readonly property color fill: keyCell.modelData.state === "memorized" ? root.doneColor
                                                                      : keyCell.modelData.state === "learned" ? root.learnedColor
                                                                      : "transparent"
                                        width: Math.max(44, keyLabel.implicitWidth + 14)
                                        height: 26
                                        radius: 4
                                        color: keyCell.fill
                                        border.width: keyCell.modelData.state === "" ? 1 : 0
                                        border.color: ui.theme.strokeColor

                                        StyledTextLabel {
                                            id: keyLabel
                                            anchors.centerIn: parent
                                            text: keyCell.modelData["short"] + (keyCell.modelData.index === 0 ? " ★" : "")
                                            font: ui.theme.bodyBoldFont
                                            color: keyCell.modelData.state === "" ? ui.theme.fontPrimaryColor : root.textOn(keyCell.fill)
                                        }

                                        MouseArea {
                                            id: keyMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: solosModel.cycleKey(soloRow.modelData.id, keyCell.modelData.index)
                                            onContainsMouseChanged: {
                                                if (containsMouse) {
                                                    var st = keyCell.modelData.state === "memorized" ? qsTrc("starscore", "Memorized")
                                                             : keyCell.modelData.state === "learned" ? qsTrc("starscore", "Learned")
                                                             : qsTrc("starscore", "Not yet")
                                                    ui.tooltip.show(keyCell, keyCell.modelData.name,
                                                                    st + (keyCell.modelData.date ? " (" + keyCell.modelData.date + ")" : ""))
                                                } else {
                                                    ui.tooltip.hide(keyCell)
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
                            text: qsTrc("starscore", "Progress by arrangement")
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
                                { "c": root.statusColors[3], "t": qsTrc("starscore", "Written, unformatted") },
                                { "c": root.statusColors[4], "t": qsTrc("starscore", "Needs review") },
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
                            Layout.preferredWidth: root.songColumnWidth
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("starscore", "Song")
                            opacity: 0.7
                        }
                        StyledTextLabel {
                            Layout.preferredWidth: 50
                            visible: !root.narrowGrid
                            text: qsTrc("starscore", "Plays")
                            opacity: 0.7
                        }
                        Repeater {
                            model: dash.columns
                            delegate: StyledTextLabel {
                                id: columnHeader
                                required property var modelData
                                Layout.preferredWidth: root.cellWidth
                                text: modelData.short
                                opacity: 0.7
                                // the full name ("4T" isn't explained anywhere else)
                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.NoButton
                                    onContainsMouseChanged: {
                                        if (containsMouse && columnHeader.modelData.title) {
                                            ui.tooltip.show(columnHeader, columnHeader.modelData.title, "")
                                        } else {
                                            ui.tooltip.hide(columnHeader)
                                        }
                                    }
                                }
                            }
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            visible: !root.narrowGrid
                            horizontalAlignment: Text.AlignLeft
                            text: qsTrc("starscore", "Audit preview")
                            opacity: 0.7
                        }
                        Item {   // (keeps the header's columns over the rows' when the preview is hidden)
                            Layout.fillWidth: true
                            visible: root.narrowGrid
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
                                    Layout.preferredWidth: root.songColumnWidth
                                    Layout.maximumWidth: root.songColumnWidth
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
                                    visible: !root.narrowGrid
                                    text: songRow.modelData.plays > 0 ? songRow.modelData.plays : "–"
                                    opacity: 0.8
                                }
                                Repeater {
                                    model: songRow.modelData.cells
                                    delegate: Item {
                                        id: cellItem
                                        required property var modelData
                                        Layout.preferredWidth: root.cellWidth
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
                                                       ? root.textOn(root.cellColor(cellItem.modelData)) : ui.theme.fontSecondaryColor
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
                                Item {
                                    Layout.fillWidth: true
                                    visible: root.narrowGrid
                                }
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    visible: !root.narrowGrid
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
