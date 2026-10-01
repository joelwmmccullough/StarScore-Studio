/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the organizer's window: a run's progress and log, the new-show question (paste the
 * YouTube link of each filmed show, or skip it) and the check of the songs found in a video.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    property string mode: "organize"     // "export", "organize", "rebuild"

    title: organizer.modeTitle

    contentWidth: 760
    contentHeight: 600
    margins: 16

    // answers being typed in the show question: url -> { kind, link }
    property var showAnswers: ({})
    // codes chosen in the song check, one per row
    property var songCodes: []

    OrganizerModel {
        id: organizer
        onPromptChanged: {
            root.showAnswers = ({})
            var codes = []
            for (var i = 0; i < organizer.songs.length; ++i) {
                codes.push(organizer.songs[i].code)
            }
            root.songCodes = codes
        }
    }

    Component.onCompleted: {
        if (root.mode === "export" || root.mode === "organize" || root.mode === "rebuild") {
            if (root.mode !== "rebuild") {
                organizer.start(root.mode)
            }
        }
    }

    function setAnswer(url, kind, link) {
        var a = root.showAnswers
        a[url] = { "url": url, "kind": kind, "link": link }
        root.showAnswers = a
    }

    function answerList() {
        var out = []
        for (var k in root.showAnswers) {
            out.push(root.showAnswers[k])
        }
        return out
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        // ---------------- before a rebuild: what it will do
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            visible: root.mode === "rebuild" && !organizer.running && !organizer.finished

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                text: qsTrc("starscore", "Files Sheets and Demos and Projects and Sheets as usual, then rebuilds every generated PDF: "
                            + "each song's What's Here, Recordings, changelogs and Horn Part Guides, the Band Guide, the Progress tracker, "
                            + "both Maintenance Reports and All Recordings. It takes a few minutes.")
            }
            FlatButton {
                text: qsTrc("starscore", "Rebuild everything")
                accentButton: true
                onClicked: organizer.start("rebuild")
            }
        }

        // ---------------- folders
        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 8
            rowSpacing: 4
            visible: !organizer.running

            StyledTextLabel { text: qsTrc("starscore", "Sheets and Demos"); font: ui.theme.bodyBoldFont; horizontalAlignment: Text.AlignLeft }
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                elide: Text.ElideMiddle
                text: organizer.bandFolder === "" ? qsTrc("starscore", "not found") : organizer.bandFolder
                opacity: 0.8
            }
            FlatButton { text: qsTrc("starscore", "Change…"); onClicked: organizer.chooseBandFolder() }

            StyledTextLabel { text: qsTrc("starscore", "Projects and Sheets"); font: ui.theme.bodyBoldFont; horizontalAlignment: Text.AlignLeft }
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                elide: Text.ElideMiddle
                text: organizer.projectsFolder === "" ? qsTrc("starscore", "not found (only Sheets and Demos will be organized)")
                                                      : organizer.projectsFolder
                opacity: 0.8
            }
            FlatButton { text: qsTrc("starscore", "Change…"); onClicked: organizer.chooseProjectsFolder() }
        }

        // ---------------- progress
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 4
            visible: organizer.running || organizer.finished

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                font: ui.theme.bodyBoldFont
                text: organizer.finished ? organizer.headline : organizer.step
            }
            ProgressBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 16
                visible: organizer.running
                from: 0
                to: 1
                value: organizer.fraction
            }
        }

        // ---------------- new shows on setlist.fm
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: organizer.shows.length > 0
            spacing: 8
            visible: organizer.shows.length > 0

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                font: ui.theme.bodyBoldFont
                text: qsTrc("starscore", "New shows on setlist.fm")
            }
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                opacity: 0.8
                text: qsTrc("starscore", "Paste the YouTube link of each filmed show. The songs and their times come from the video's description. "
                            + "Leave a link empty to be asked again next time.")
            }

            StyledListView {
                id: showList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 8
                model: organizer.shows

                delegate: Rectangle {
                    id: showRow
                    required property var modelData
                    required property int index
                    width: showList.width
                    height: showColumn.implicitHeight + 16
                    radius: 3
                    color: "transparent"
                    border.width: 1
                    border.color: ui.theme.strokeColor

                    ColumnLayout {
                        id: showColumn
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 4

                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            font: ui.theme.bodyBoldFont
                            text: showRow.modelData.date + " · " + showRow.modelData.venue
                                  + (showRow.modelData.askedBefore ? qsTrc("starscore", "  (skipped last time)") : "")
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.WordWrap
                            opacity: 0.7
                            text: showRow.modelData.setlist
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            TextInputField {
                                Layout.fillWidth: true
                                hint: qsTrc("starscore", "YouTube link")
                                enabled: !notFilmed.checked
                                onTextEdited: function(t) { root.setAnswer(showRow.modelData.url, "link", t) }
                            }
                            CheckBox {
                                id: notFilmed
                                text: qsTrc("starscore", "Not filmed")
                                onClicked: {
                                    checked = !checked
                                    root.setAnswer(showRow.modelData.url, checked ? "notfilmed" : "later", "")
                                }
                            }
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                FlatButton {
                    text: qsTrc("starscore", "Skip all for now")
                    onClicked: organizer.answerShows([])
                }
                FlatButton {
                    text: qsTrc("starscore", "Continue")
                    accentButton: true
                    onClicked: organizer.answerShows(root.answerList())
                }
            }
        }

        // ---------------- the songs found in the videos
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: organizer.songs.length > 0
            spacing: 8
            visible: organizer.songs.length > 0

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                font: ui.theme.bodyBoldFont
                text: qsTrc("starscore", "Songs in the video descriptions")
            }
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                opacity: 0.8
                text: qsTrc("starscore", "Check the song for each time. Names StarScore couldn't place are marked; what you choose is remembered.")
            }

            StyledListView {
                id: songList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 2
                model: organizer.songs

                delegate: RowLayout {
                    id: songRow
                    required property var modelData
                    required property int index
                    width: songList.width
                    spacing: 8

                    StyledTextLabel {
                        Layout.preferredWidth: 150
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        opacity: 0.7
                        text: songRow.modelData.show
                    }
                    StyledTextLabel {
                        Layout.preferredWidth: 60
                        horizontalAlignment: Text.AlignRight
                        text: songRow.modelData.time
                    }
                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        color: songRow.modelData.unsure ? "#E8892B" : ui.theme.fontPrimaryColor
                        text: songRow.modelData.name
                    }
                    StyledDropdown {
                        Layout.preferredWidth: 260
                        model: organizer.songChoices
                        currentIndex: indexOfValue(root.songCodes[songRow.index] !== undefined ? root.songCodes[songRow.index] : "")
                        onActivated: function(i, value) {
                            var c = root.songCodes
                            c[songRow.index] = value
                            root.songCodes = c
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                FlatButton {
                    text: qsTrc("starscore", "Continue")
                    accentButton: true
                    onClicked: organizer.answerSongs(root.songCodes)
                }
            }
        }

        // ---------------- log
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: organizer.shows.length === 0 && organizer.songs.length === 0
            Layout.preferredHeight: 120
            visible: organizer.logText !== ""
            color: ui.theme.textFieldColor
            border.color: ui.theme.strokeColor
            radius: 3
            ScrollView {
                anchors.fill: parent
                anchors.margins: 6
                TextArea {
                    readOnly: true
                    text: organizer.logText
                    color: ui.theme.fontPrimaryColor
                    font: ui.theme.bodyFont
                    wrapMode: TextEdit.Wrap
                }
            }
        }

        Item {
            Layout.fillHeight: true
            visible: organizer.logText === "" && organizer.shows.length === 0 && organizer.songs.length === 0
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            FlatButton {
                text: qsTrc("starscore", "Run again")
                visible: organizer.finished && root.mode !== "export"
                onClicked: organizer.start(root.mode)
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("starscore", "Stop")
                visible: organizer.running
                onClicked: organizer.stop()
            }
            FlatButton {
                text: qsTrc("starscore", "Close")
                enabled: !organizer.running
                onClicked: root.hide()
            }
        }
    }
}
