/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — File › Autosave archive…: earlier saves of each song, to open as their own files.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Autosave archive")

    contentWidth: 820
    contentHeight: 560
    margins: 16

    property string selectedTime: ""

    AutosaveArchiveModel {
        id: archiveModel
        onVersionOpened: root.hide()
    }

    Component.onCompleted: archiveModel.load()

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            text: qsTrc("starscore", "Each time you save, the version being replaced is kept (at most every 10 minutes). Older versions are thinned to one per hour, day, week, then month. Opening a version opens a copy; the song itself is not changed.")
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // --- songs ---
            StyledListView {
                id: songList
                Layout.preferredWidth: 280
                Layout.fillHeight: true
                model: archiveModel.songs

                delegate: ListItemBlank {
                    required property var modelData
                    width: songList.width
                    height: 46
                    isSelected: modelData.name === archiveModel.currentSong
                    onClicked: {
                        root.selectedTime = ""
                        archiveModel.selectSong(modelData.name)
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        width: parent.width - 20
                        StyledTextLabel { width: parent.width; horizontalAlignment: Text.AlignLeft; text: modelData.name; font: ui.theme.bodyBoldFont }
                        StyledTextLabel { width: parent.width; horizontalAlignment: Text.AlignLeft; text: modelData.info; opacity: 0.7 }
                    }
                }
            }

            SeparatorLine { orientation: Qt.Vertical }

            // --- versions ---
            StyledListView {
                id: versionList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: archiveModel.versions

                delegate: ListItemBlank {
                    required property var modelData
                    width: versionList.width
                    height: 36
                    isSelected: modelData.time === root.selectedTime
                    onClicked: root.selectedTime = modelData.time
                    onDoubleClicked: archiveModel.openVersion(modelData.time)

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        StyledTextLabel { Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft; text: modelData.label }
                        StyledTextLabel { Layout.preferredWidth: 110; horizontalAlignment: Text.AlignLeft; text: modelData.ago; opacity: 0.7 }
                        StyledTextLabel { Layout.preferredWidth: 120; horizontalAlignment: Text.AlignRight; text: modelData.size; opacity: 0.7 }
                    }
                }
            }
        }

        StyledTextLabel {
            visible: archiveModel.songs.length === 0
            Layout.fillWidth: true
            text: qsTrc("starscore", "Nothing archived yet. Versions appear here after you save a song.")
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            FlatButton {
                text: qsTrc("starscore", "Show archive folder")
                onClicked: archiveModel.showArchiveFolder()
            }
            FlatButton {
                text: qsTrc("starscore", "Save all versions as files")
                enabled: archiveModel.currentSong !== "" && archiveModel.busyText === ""
                toolTipTitle: qsTrc("starscore", "Unpacks every version of this song into Autosave Archive/Unpacked, as normal song files")
                onClicked: archiveModel.saveAllVersions()
            }
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignHCenter
                text: archiveModel.busyText
            }
            FlatButton {
                text: qsTrc("global", "Close")
                onClicked: root.hide()
            }
            FlatButton {
                text: qsTrc("starscore", "Open this version")
                accentButton: true
                enabled: root.selectedTime !== "" && archiveModel.busyText === ""
                onClicked: archiveModel.openVersion(root.selectedTime)
            }
        }
    }
}
