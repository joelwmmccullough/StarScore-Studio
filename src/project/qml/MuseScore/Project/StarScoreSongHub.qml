/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Song page (1.28.0). What the open song has, and a way into each: its arrangements and part
 * scores, solos, recordings, reference PDFs, Practice, the sheets exported from it, its to-do list and history. Shown
 * first when a song is opened (unless "Open songs here first" is off); the Score tab is the notation.
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

    property NavigationSection navigationSection: null

    SongHubModel {
        id: hub
    }

    // the song's releases and live performances (the Recordings dialog's list, from Sheets and Demos)
    RecordingsModel {
        id: recordings
    }

    Component.onCompleted: {
        hub.load()
        if (hub.isStarScore && !hub.isSoloFile) {
            recordings.load()
        }
    }
    onVisibleChanged: {
        if (visible) {
            hub.refresh()
            if (hub.isStarScore && !hub.isSoloFile) {
                recordings.load()
            }
        }
    }

    readonly property bool wide: width > 1100

    // --- a card: a title, and its contents below
    component Card: Rectangle {
        id: card
        property string title: ""
        property string subtitle: ""
        default property alias content: body.data
        property alias actions: actionRow.data

        Layout.fillWidth: true
        Layout.alignment: Qt.AlignTop
        implicitHeight: col.implicitHeight + 28
        radius: 8
        color: ui.theme.backgroundPrimaryColor
        border.color: ui.theme.strokeColor
        border.width: 1

        ColumnLayout {
            id: col
            x: 14
            y: 14
            width: card.width - 28
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                StyledTextLabel {
                    text: card.title
                    font: ui.theme.largeBodyBoldFont
                    horizontalAlignment: Text.AlignLeft
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    text: card.subtitle
                    opacity: 0.6
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                }
                Row {
                    id: actionRow
                    spacing: 6
                }
            }
            ColumnLayout {
                id: body
                Layout.fillWidth: true
                spacing: 2
            }
        }
    }

    // --- a row that opens something: a status dot, a name, a detail at the right, and extra buttons
    component HubRow: Rectangle {
        id: row
        property string text: ""
        property string detail: ""
        property color dot: "transparent"
        property bool showDot: false
        property bool bold: false
        property string toolTip: ""
        property alias buttons: buttonRow.data
        signal clicked()

        Layout.fillWidth: true
        implicitHeight: 34
        radius: 4
        color: mouse.containsMouse ? ui.theme.buttonColor : "transparent"

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: row.clicked()
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 6
            spacing: 8

            Rectangle {
                visible: row.showDot
                width: 10
                height: 10
                radius: 5
                color: row.dot
                border.color: ui.theme.strokeColor
                border.width: row.dot == "transparent" ? 1 : 0
            }
            StyledTextLabel {
                Layout.fillWidth: true
                text: row.text
                font: row.bold ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                horizontalAlignment: Text.AlignLeft
                elide: Text.ElideRight
            }
            StyledTextLabel {
                text: row.detail
                opacity: 0.65
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideLeft
                Layout.maximumWidth: row.width * 0.55
            }
            Row {
                id: buttonRow
                spacing: 2
            }
        }
    }

    component Empty: StyledTextLabel {
        Layout.fillWidth: true
        horizontalAlignment: Text.AlignLeft
        wrapMode: Text.WordWrap
        opacity: 0.55
    }

    StyledTextLabel {
        anchors.centerIn: parent
        visible: !hub.hasSong
        text: qsTrc("starscore", "Open a song to see everything you've made for it here.")
    }

    StyledFlickable {
        id: flick
        anchors.fill: parent
        visible: hub.hasSong
        contentWidth: width
        contentHeight: content.implicitHeight + 48
        clip: true

        ColumnLayout {
            id: content
            x: 28
            y: 24
            width: flick.width - 56
            spacing: 18

            // ---------------- the song ----------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6

                StyledTextLabel {
                    Layout.fillWidth: true
                    text: hub.title
                    font.pixelSize: 30
                    font.bold: true
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    text: hub.facts
                    opacity: 0.75
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                }
                Flow {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    spacing: 10

                    FlatButton {
                        text: qsTrc("starscore", "Open the score")
                        accentButton: true
                        onClicked: hub.openScore()
                    }
                    FlatButton {
                        visible: hub.isStarScore && !hub.isSoloFile
                        text: qsTrc("starscore", "Export to Sheets and Demos…")
                        onClicked: hub.dispatch("starscore-export-band")
                    }
                    FlatButton {
                        text: qsTrc("starscore", "Show in Finder")
                        onClicked: hub.showInFolder(hub.filePath)
                    }
                    FlatButton {
                        visible: hub.songFolder !== ""
                        text: qsTrc("starscore", "Sheets and Demos folder")
                        onClicked: hub.openPath(hub.songFolder)
                    }
                    FlatButton {
                        text: qsTrc("starscore", "Autosave archive…")
                        onClicked: hub.dispatch("starscore-autosave-archive")
                    }
                    CheckBox {
                        text: qsTrc("starscore", "Show this page when a song opens")
                        checked: hub.openHere
                        onClicked: hub.openHere = !checked
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: root.wide ? 2 : 1
                columnSpacing: 18
                rowSpacing: 18

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    spacing: 18

                    // ---------------- arrangements and sections ----------------
                    Card {
                        visible: hub.isStarScore && !hub.isSoloFile
                        title: qsTrc("starscore", "Arrangements")
                        subtitle: qsTrc("starscore", "click to open the score on one")

                        Repeater {
                            model: hub.arrangements
                            delegate: HubRow {
                                required property var modelData
                                text: modelData.name
                                detail: modelData.statusName
                                showDot: true
                                dot: modelData.color
                                bold: modelData.active
                                onClicked: hub.openArrangement(modelData.id)
                                buttons: FlatButton {
                                    visible: modelData.scoreName !== ""
                                    text: qsTrc("starscore", "Score")
                                    toolTipTitle: modelData.scoreName
                                    toolTipDescription: qsTrc("starscore", "This arrangement's own score")
                                    onClicked: hub.openPartScore(modelData.scoreName)
                                }
                            }
                        }
                        Empty {
                            visible: hub.arrangements.length === 0
                            text: qsTrc("starscore", "No arrangements yet.")
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            visible: hub.sections.length > 0
                            text: qsTrc("starscore", "Sections")
                            font: ui.theme.bodyBoldFont
                            horizontalAlignment: Text.AlignLeft
                        }
                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: hub.sections
                                delegate: Rectangle {
                                    required property var modelData
                                    width: secText.implicitWidth + 30
                                    height: 26
                                    radius: 13
                                    color: "transparent"
                                    border.color: ui.theme.strokeColor
                                    Row {
                                        anchors.centerIn: parent
                                        spacing: 6
                                        Rectangle {
                                            anchors.verticalCenter: parent.verticalCenter
                                            width: 8
                                            height: 8
                                            radius: 4
                                            color: modelData.color
                                        }
                                        StyledTextLabel {
                                            id: secText
                                            text: modelData.name
                                        }
                                    }
                                    ToolTip.visible: secMouse.containsMouse
                                    ToolTip.text: modelData.statusName
                                    MouseArea {
                                        id: secMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                    }
                                }
                            }
                        }
                    }

                    // ---------------- a solo transcription's keys ----------------
                    Card {
                        visible: hub.isSoloFile
                        title: qsTrc("starscore", "Keys")
                        subtitle: qsTrc("starscore", "how far you've got with each")

                        Flow {
                            Layout.fillWidth: true
                            spacing: 6
                            Repeater {
                                model: hub.soloKeys
                                delegate: Rectangle {
                                    required property var modelData
                                    width: keyText.implicitWidth + 20
                                    height: 28
                                    radius: 6
                                    color: modelData.state === "memorized" ? "#3FB05A"
                                           : modelData.state === "learned" ? "#E3A72B" : "transparent"
                                    border.color: ui.theme.strokeColor
                                    StyledTextLabel {
                                        id: keyText
                                        anchors.centerIn: parent
                                        text: modelData.short !== "" ? modelData.short : modelData.name
                                        color: modelData.state !== "" ? "#000000" : ui.theme.fontPrimaryColor
                                    }
                                    ToolTip.visible: keyMouse.containsMouse
                                    ToolTip.text: modelData.name + (modelData.state !== "" ? " · " + modelData.state : "")
                                    MouseArea {
                                        id: keyMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                    }
                                }
                            }
                        }
                        Empty {
                            visible: hub.soloKeys.length === 0
                            text: qsTrc("starscore", "No keys exported yet: export the solo sheets to start the list.")
                        }
                    }

                    // ---------------- practice ----------------
                    Card {
                        title: qsTrc("starscore", "Practice")
                        subtitle: qsTrc("starscore", "beside the score")

                        Flow {
                            Layout.fillWidth: true
                            spacing: 10
                            FlatButton {
                                text: qsTrc("starscore", "Watch")
                                toolTipTitle: qsTrc("starscore", "Falling notes")
                                toolTipDescription: qsTrc("starscore", "The score falling onto a keyboard as it plays, or Wait for me on a MIDI keyboard")
                                onClicked: hub.practice("watch")
                            }
                            FlatButton {
                                visible: !hub.isSoloFile || hub.arrangements.length > 0 || true
                                text: qsTrc("starscore", "Jam")
                                toolTipTitle: qsTrc("starscore", "Backing band")
                                toolTipDescription: qsTrc("starscore", "Bass, drums and piano from the chord symbols, to solo over")
                                onClicked: hub.practice("jam")
                            }
                            FlatButton {
                                enabled: hub.media.length > 0
                                text: qsTrc("starscore", "Listen")
                                toolTipTitle: qsTrc("starscore", "The recordings")
                                toolTipDescription: hub.media.length > 0 ? qsTrc("starscore", "The recordings stored in this score, in sync, at any speed")
                                                                         : qsTrc("starscore", "Add a recording first (Recordings and video, below)")
                                onClicked: hub.practice("listen")
                            }
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            Layout.topMargin: 8
                            visible: hub.solos.length > 0
                            text: qsTrc("starscore", "Solos in this song")
                            font: ui.theme.bodyBoldFont
                            horizontalAlignment: Text.AlignLeft
                        }
                        Repeater {
                            model: hub.solos
                            delegate: HubRow {
                                required property var modelData
                                text: modelData.name
                                detail: modelData.detail
                                onClicked: hub.openSolo(modelData.id)
                            }
                        }
                    }

                    // ---------------- recordings and video ----------------
                    Card {
                        title: qsTrc("starscore", "Recordings and video")
                        subtitle: qsTrc("starscore", "stored in this file")
                        actions: FlatButton {
                            text: qsTrc("starscore", "+ Add")
                            onClicked: hub.addMedia()
                        }

                        Repeater {
                            model: hub.media
                            delegate: HubRow {
                                required property var modelData
                                text: modelData.name
                                detail: modelData.kindTitle + " · " + modelData.size
                                onClicked: hub.openMedia(modelData.id)
                            }
                        }
                        Empty {
                            visible: hub.media.length === 0
                            text: qsTrc("starscore", "None yet. Add the full mix, stems, the players' video or a Transcribe! file.")
                        }
                    }

                    // ---------------- releases and live performances ----------------
                    Card {
                        visible: hub.isStarScore && !hub.isSoloFile
                        title: qsTrc("starscore", "Releases and live takes")
                        subtitle: recordings.releases.length + recordings.performances.length > 0
                                  ? qsTrc("starscore", "%1 releases, %2 live takes").arg(recordings.releases.length).arg(recordings.performances.length)
                                  : ""
                        actions: FlatButton {
                            text: qsTrc("starscore", "Edit…")
                            onClicked: hub.dispatch("starscore-recordings")
                        }

                        Repeater {
                            model: recordings.releases
                            delegate: HubRow {
                                required property var modelData
                                text: modelData.label !== "" ? modelData.label : modelData.kind
                                detail: modelData.date
                                onClicked: {
                                    if (modelData.link !== "") {
                                        Qt.openUrlExternally(modelData.link)
                                    }
                                }
                            }
                        }
                        Repeater {
                            model: recordings.performances.slice(0, 12)
                            delegate: HubRow {
                                required property var modelData
                                text: modelData.date + " · " + modelData.venue
                                detail: (modelData.rating > 0 ? "★".repeat(modelData.rating) + "  " : "") + modelData.time
                                onClicked: Qt.openUrlExternally(modelData.link)
                            }
                        }
                        Empty {
                            visible: recordings.performances.length > 12
                            text: qsTrc("starscore", "and %1 more: Edit… lists them all").arg(recordings.performances.length - 12)
                        }
                        Empty {
                            visible: recordings.releases.length + recordings.performances.length === 0
                            text: qsTrc("starscore", "None listed yet.")
                        }
                    }

                    // ---------------- to do ----------------
                    Card {
                        visible: hub.isStarScore && !hub.isSoloFile
                        title: qsTrc("starscore", "Still to do")

                        Repeater {
                            model: hub.todo
                            delegate: ColumnLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 0
                                HubRow {
                                    text: modelData.title
                                    detail: modelData.statusName
                                    showDot: true
                                    dot: modelData.color
                                    onClicked: hub.openPanel("todo")
                                }
                                StyledTextLabel {
                                    Layout.fillWidth: true
                                    Layout.leftMargin: 26
                                    visible: modelData.details !== "" || modelData.note !== ""
                                    text: [modelData.details, modelData.note].filter(function(t) { return t !== "" }).join(" · ")
                                    opacity: 0.6
                                    wrapMode: Text.WordWrap
                                    horizontalAlignment: Text.AlignLeft
                                }
                            }
                        }
                        Empty {
                            visible: hub.todo.length === 0
                            text: qsTrc("starscore", "Nothing: every part is finished.")
                        }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 1
                    Layout.alignment: Qt.AlignTop
                    spacing: 18

                    // ---------------- part scores ----------------
                    Card {
                        id: partsCard
                        property bool showAll: false
                        readonly property var shown: hub.partScores.filter(function(p) { return !p.arrangementScore })
                        title: qsTrc("starscore", "Part scores")
                        subtitle: partsCard.shown.length > 0 ? qsTrc("starscore", "%1 in this file").arg(partsCard.shown.length) : ""

                        Repeater {
                            model: partsCard.showAll ? partsCard.shown : partsCard.shown.slice(0, 14)
                            delegate: HubRow {
                                required property var modelData
                                text: modelData.name
                                detail: modelData.statusName
                                showDot: true
                                dot: modelData.color !== "" ? modelData.color : "transparent"
                                opacity: modelData.visible ? 1.0 : 0.6
                                onClicked: hub.openPartScore(modelData.name)
                            }
                        }
                        FlatButton {
                            visible: partsCard.shown.length > 14
                            transparent: true
                            text: partsCard.showAll ? qsTrc("starscore", "Show fewer")
                                                    : qsTrc("starscore", "Show all %1").arg(partsCard.shown.length)
                            onClicked: partsCard.showAll = !partsCard.showAll
                        }
                        Empty {
                            visible: partsCard.shown.length === 0
                            text: qsTrc("starscore", "No part scores yet.")
                        }
                    }

                    // ---------------- reference PDFs ----------------
                    Card {
                        visible: hub.isStarScore
                        title: qsTrc("starscore", "Reference PDFs")
                        subtitle: qsTrc("starscore", "stored in this file")
                        actions: FlatButton {
                            text: qsTrc("starscore", "+ Add")
                            onClicked: hub.addReference()
                        }

                        Repeater {
                            model: hub.references
                            delegate: HubRow {
                                required property var modelData
                                text: modelData.name
                                detail: modelData.instrument
                                onClicked: hub.openReference(modelData.id)
                                buttons: FlatButton {
                                    icon: IconCode.OPEN_FILE
                                    toolTipTitle: qsTrc("starscore", "Open in another app")
                                    onClicked: hub.openReferenceElsewhere(modelData.id)
                                }
                            }
                        }
                        Empty {
                            visible: hub.references.length === 0
                            text: qsTrc("starscore", "None yet.")
                        }
                    }

                    // ---------------- sheets and files ----------------
                    Card {
                        visible: !hub.isSoloFile
                        title: qsTrc("starscore", "In Sheets and Demos")
                        subtitle: hub.songFolder.substring(hub.songFolder.lastIndexOf("/") + 1)
                        actions: FlatButton {
                            visible: hub.songFolder !== ""
                            icon: IconCode.OPEN_FILE
                            toolTipTitle: qsTrc("starscore", "Open the folder")
                            onClicked: hub.openPath(hub.songFolder)
                        }

                        Repeater {
                            model: hub.bandFolders
                            delegate: FolderRow { hubModel: hub }
                        }
                        Empty {
                            visible: hub.bandFolders.length === 0
                            text: hub.songFolder === "" ? qsTrc("starscore", "Not exported yet: Export to Sheets and Demos makes its folder.")
                                  : !hub.foldersReady ? qsTrc("starscore", "Reading the folder…")
                                  : qsTrc("starscore", "The folder is empty.")
                        }
                    }

                    Card {
                        title: qsTrc("starscore", "Beside this file")
                        subtitle: qsTrc("starscore", "in its folder in Projects and Sheets")
                        actions: FlatButton {
                            icon: IconCode.OPEN_FILE
                            toolTipTitle: qsTrc("starscore", "Open the folder")
                            onClicked: hub.showInFolder(hub.filePath)
                        }

                        Repeater {
                            model: hub.localFolders
                            delegate: FolderRow { hubModel: hub }
                        }
                        Empty {
                            visible: hub.localFolders.length === 0
                            text: !hub.foldersReady ? qsTrc("starscore", "Reading the folder…") : qsTrc("starscore", "Nothing else in its folder.")
                        }
                    }
                }
            }
        }
    }

    // --- a folder: click to see its files (click one to open it); the arrow opens the folder itself
    component FolderRow: ColumnLayout {
        id: folderRow
        required property var modelData
        property var hubModel: null
        property bool expanded: false
        property var files: []

        Layout.fillWidth: true
        spacing: 0

        HubRow {
            text: (folderRow.expanded ? "▾ " : "▸ ") + (folderRow.modelData.group ? folderRow.modelData.group + " / " : "")
                  + folderRow.modelData.name
            detail: folderRow.modelData.detail
            onClicked: {
                folderRow.expanded = !folderRow.expanded
                if (folderRow.expanded) {
                    folderRow.files = folderRow.hubModel.filesIn(folderRow.modelData.path)
                }
            }
            buttons: FlatButton {
                icon: IconCode.OPEN_FILE
                toolTipTitle: qsTrc("starscore", "Open the folder")
                onClicked: folderRow.hubModel.openPath(folderRow.modelData.path)
            }
        }
        Repeater {
            model: folderRow.expanded ? folderRow.files : []
            delegate: HubRow {
                required property var modelData
                Layout.leftMargin: 22
                implicitHeight: 28
                text: modelData.name
                detail: modelData.detail
                onClicked: folderRow.hubModel.openPath(modelData.path)
            }
        }
    }
}
