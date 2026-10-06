/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Update all sheets (Joel, 6 Oct 2026): after a StarScore change in how sheets are made, every
 * song's Finished sheets made again; only the ones that come out differently are written, with their version's last
 * number raised. Shows what it's doing as it goes.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Update all sheets")

    contentWidth: 640
    contentHeight: 560
    margins: 16

    property var state: ({ "running": false, "finished": false, "songIndex": 0, "songCount": 0, "song": "", "phase": "", "results": "" })
    readonly property bool busy: root.state.running

    closeOnEscape: !root.busy
    onAboutToClose: function(closeEvent) {
        if (root.busy) {
            closeEvent.accepted = false
        }
    }

    UpdateAllSheetsModel {
        id: model
    }

    Component.onCompleted: model.load()

    Timer {
        interval: 250
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: root.state = model.status()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            text: qsTrc("starscore", "For after a StarScore update that changes how sheets are made. Each song below is "
                        + "opened in turn and its sheets that were exported while Finished, and are still Finished, are "
                        + "made again. Only the ones that come out differently are written: the old file goes to Version "
                        + "History and the version on the sheet goes up by its last number (1.2.0 becomes 1.2.1). Each song "
                        + "is saved afterwards. Nothing that isn't Finished is touched.")
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
            visible: !root.busy && !root.state.finished
            text: model.songs.length === 0 ? qsTrc("starscore", "Every song's sheets are up to date.")
                  : qsTrc("starscore", "%1 song(s), %2 Finished sheet(s) to check:").arg(model.songs.length).arg(model.sheetCount)
        }

        StyledListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !root.busy && !root.state.finished
            model: model.songs
            delegate: StyledTextLabel {
                width: ListView.view ? ListView.view.width : 0
                horizontalAlignment: Text.AlignLeft
                text: modelData.code + " – " + modelData.title + "   ("
                      + qsTrc("starscore", "%1 Finished sheet(s), made with %2").arg(modelData.sheets).arg(modelData.made) + ")"
            }
        }

        // while running, and after
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.busy || root.state.finished
            spacing: 6

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                font: ui.theme.bodyBoldFont
                text: root.state.finished ? qsTrc("starscore", "Done.")
                      : qsTrc("starscore", "Song %1 of %2: %3").arg(root.state.songIndex + 1).arg(root.state.songCount).arg(root.state.song)
            }
            ProgressBar {
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                from: 0
                to: Math.max(1, root.state.songCount)
                value: root.state.finished ? root.state.songCount : root.state.songIndex
            }
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                elide: Text.ElideMiddle
                visible: root.busy
                text: root.state.phase
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.state.results !== ""
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignTop
            wrapMode: Text.WordWrap
            text: root.state.results
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8
            FlatButton {
                text: qsTrc("starscore", "Stop after this song")
                visible: root.busy
                onClicked: model.cancel()
            }
            FlatButton {
                text: qsTrc("starscore", "Update %n song(s)", "", model.songs.length)
                accentButton: true
                visible: !root.busy && !root.state.finished
                enabled: model.songs.length > 0
                onClicked: model.start()
            }
            FlatButton {
                text: qsTrc("global", "Close")
                enabled: !root.busy
                onClicked: root.hide()
            }
        }
    }
}
