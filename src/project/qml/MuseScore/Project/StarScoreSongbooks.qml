/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Songbooks. Album songbooks for one instrument, and charts of one song, built from the
 * parts that are done; what's missing is listed song by song.
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

    property bool editingTracklist: false
    property string notesSong: ""

    readonly property color okColor: "#3FB05A"
    readonly property color missingColor: "#E0463A"

    SongbookModel {
        id: sb
    }

    Component.onCompleted: sb.load()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 20

        // ---------------- What to make ----------------
        Rectangle {
            Layout.preferredWidth: 250
            Layout.fillHeight: true
            radius: 6
            color: ui.theme.backgroundPrimaryColor
            border.color: ui.theme.strokeColor

            StyledFlickable {
                anchors.fill: parent
                anchors.margins: 12
                contentHeight: bookColumn.implicitHeight
                clip: true

                ColumnLayout {
                    id: bookColumn
                    width: parent.width
                    spacing: 4

                    StyledTextLabel {
                        text: qsTrc("starscore", "Album songbooks")
                        font: ui.theme.bodyBoldFont
                        opacity: 0.7
                    }
                    Repeater {
                        model: sb.books.filter(function(b) { return !b.chart })
                        delegate: FlatButton {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.title
                            accentButton: sb.bookId === modelData.id
                            enabled: !sb.busy
                            onClicked: sb.setBookId(modelData.id)
                        }
                    }
                    Item { height: 10 }
                    StyledTextLabel {
                        text: qsTrc("starscore", "Song charts")
                        font: ui.theme.bodyBoldFont
                        opacity: 0.7
                    }
                    Repeater {
                        model: sb.books.filter(function(b) { return b.chart })
                        delegate: FlatButton {
                            required property var modelData
                            Layout.fillWidth: true
                            text: modelData.title
                            accentButton: sb.bookId === modelData.id
                            enabled: !sb.busy
                            onClicked: sb.setBookId(modelData.id)
                        }
                    }
                }
            }
        }

        // ---------------- Plan ----------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "Songbooks")
                font: ui.theme.headerBoldFont
            }

            // Album or song
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: !sb.isChart

                StyledTextLabel { text: qsTrc("starscore", "Album") }
                StyledDropdown {
                    Layout.preferredWidth: 240
                    model: sb.albums.map(function(a) { return { "text": a, "value": a } })
                    currentIndex: sb.albums.indexOf(sb.album)
                    enabled: !sb.busy
                    onActivated: function(index, value) { sb.setAlbum(value) }
                }
                FlatButton {
                    text: root.editingTracklist ? qsTrc("starscore", "Done") : qsTrc("starscore", "Edit tracklist")
                    enabled: !sb.busy
                    onClicked: {
                        if (root.editingTracklist) {
                            sb.setTracklist(tracklistArea.text)
                        }
                        root.editingTracklist = !root.editingTracklist
                    }
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: sb.isChart

                StyledTextLabel { text: qsTrc("starscore", "Song") }
                StyledDropdown {
                    Layout.preferredWidth: 300
                    model: sb.librarySongs.map(function(s) { return { "text": s.title, "value": s.path } })
                    currentIndex: sb.librarySongs.findIndex(function(s) { return s.path === sb.songPath })
                    enabled: !sb.busy
                    onActivated: function(index, value) { sb.setSongPath(value) }
                }
                Item { Layout.fillWidth: true }
            }

            // Tracklist editor
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                visible: root.editingTracklist && !sb.isChart
                color: ui.theme.textFieldColor
                border.color: ui.theme.strokeColor
                radius: 3
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 6
                    TextArea {
                        id: tracklistArea
                        text: sb.tracklist
                        color: ui.theme.fontPrimaryColor
                        font: ui.theme.bodyFont
                        wrapMode: TextEdit.NoWrap
                        placeholderText: qsTrc("starscore", "One song per line, in album order")
                    }
                }
            }

            // Summary + actions
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    text: sb.status !== "" ? sb.status
                          : sb.isChart ? (sb.readyCount > 0 ? qsTrc("starscore", "Ready to write.") : qsTrc("starscore", "Not ready yet."))
                          : qsTrc("starscore", "%1 of %2 songs ready").arg(sb.readyCount).arg(sb.songCount)
                    font: ui.theme.bodyBoldFont
                }
                FlatButton {
                    text: qsTrc("starscore", "Check")
                    toolTipTitle: qsTrc("starscore", "Check")
                    toolTipDescription: qsTrc("starscore", "Reads the songs that changed since they were last read, and checks again what's ready")
                    enabled: !sb.busy
                    onClicked: sb.refresh()
                }
                FlatButton {
                    text: sb.isChart ? qsTrc("starscore", "Write chart") : qsTrc("starscore", "Write songbook")
                    accentButton: true
                    visible: !sb.busy
                    enabled: sb.readyCount > 0
                    onClicked: sb.build()
                }
                FlatButton {
                    text: qsTrc("starscore", "Stop")
                    visible: sb.busy
                    onClicked: sb.cancel()
                }
            }

            // Song by song
            StyledListView {
                id: planList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 6
                model: sb.plan

                delegate: Rectangle {
                    id: songRow
                    required property var modelData
                    width: planList.width
                    implicitHeight: rowLayout.implicitHeight + 16 + (root.notesSong === modelData.song ? notesBox.height + 8 : 0)
                    radius: 5
                    color: ui.theme.backgroundPrimaryColor
                    border.color: ui.theme.strokeColor

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 4
                        radius: 2
                        color: songRow.modelData.ready ? root.okColor : root.missingColor
                    }

                    ColumnLayout {
                        id: rowLayout
                        x: 16
                        y: 8
                        width: parent.width - 28
                        spacing: 6

                        RowLayout {
                            Layout.fillWidth: true
                            StyledTextLabel {
                                text: sb.isChart ? songRow.modelData.song : songRow.modelData.track + ". " + songRow.modelData.song
                                font: ui.theme.bodyBoldFont
                            }
                            StyledTextLabel {
                                text: songRow.modelData.ready ? qsTrc("starscore", "ready") : qsTrc("starscore", "not ready")
                                color: songRow.modelData.ready ? root.okColor : root.missingColor
                            }
                            Item { Layout.fillWidth: true }
                            FlatButton {
                                visible: !sb.isChart
                                text: root.notesSong === songRow.modelData.song ? qsTrc("starscore", "Close notes") : qsTrc("starscore", "Notes…")
                                toolTipTitle: qsTrc("starscore", "Notes")
                                toolTipDescription: qsTrc("starscore", "Your notes on this song, printed on its first page in every book")
                                onClicked: {
                                    if (root.notesSong === songRow.modelData.song) {
                                        sb.setNotes(songRow.modelData.song, notesArea.text)
                                        root.notesSong = ""
                                    } else {
                                        root.notesSong = songRow.modelData.song
                                    }
                                }
                            }
                        }

                        Flow {
                            Layout.fillWidth: true
                            spacing: 8
                            Repeater {
                                model: songRow.modelData.items
                                delegate: Rectangle {
                                    required property var modelData
                                    height: chipText.implicitHeight + 8
                                    width: chipText.implicitWidth + 16
                                    radius: height / 2
                                    color: modelData.ready ? Qt.rgba(0.25, 0.69, 0.35, 0.18)
                                           : modelData.optional ? Qt.rgba(0.5, 0.5, 0.5, 0.15) : Qt.rgba(0.88, 0.27, 0.23, 0.16)
                                    StyledTextLabel {
                                        id: chipText
                                        anchors.centerIn: parent
                                        text: (modelData.ready ? "✓ " : "✗ ") + modelData.label
                                              + (!modelData.ready && modelData.why !== "" ? " — " + modelData.why : "")
                                    }
                                }
                            }
                        }

                        Rectangle {
                            id: notesBox
                            Layout.fillWidth: true
                            Layout.preferredHeight: 140
                            visible: root.notesSong === songRow.modelData.song
                            color: ui.theme.textFieldColor
                            border.color: ui.theme.strokeColor
                            radius: 3
                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 6
                                TextArea {
                                    id: notesArea
                                    text: root.notesSong === songRow.modelData.song ? sb.notes(songRow.modelData.song) : ""
                                    color: ui.theme.fontPrimaryColor
                                    font: ui.theme.bodyFont
                                    wrapMode: TextEdit.Wrap
                                    placeholderText: qsTrc("starscore", "Where the tune came from, what to listen for, things to try…")
                                    onTextChanged: {
                                        if (root.notesSong === songRow.modelData.song) {
                                            sb.setNotes(songRow.modelData.song, text)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Report
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                visible: sb.report !== ""
                text: sb.report
                opacity: 0.85
            }

            // Output
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideMiddle
                    text: qsTrc("starscore", "Saved in: %1").arg(sb.outputFolder)
                    opacity: 0.7
                }
                FlatButton {
                    text: qsTrc("starscore", "Change…")
                    enabled: !sb.busy
                    onClicked: sb.chooseOutputFolder()
                }
                FlatButton {
                    text: sb.lastOutput !== "" ? qsTrc("starscore", "Show the result") : qsTrc("starscore", "Open folder")
                    onClicked: sb.openOutput()
                }
            }
        }
    }
}
