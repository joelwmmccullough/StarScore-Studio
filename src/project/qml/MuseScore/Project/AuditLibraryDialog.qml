/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Audit library: every .starscore in the projects folder with what's left to audit in it.
 * Songs with the most to look at come first. Click a song to open it with the Audit panel.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Audit library")

    contentWidth: 820
    contentHeight: 600
    margins: 16

    AuditLibraryModel {
        id: libraryModel
        onCloseRequested: root.hide()
    }

    Component.onCompleted: libraryModel.load()

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                elide: Text.ElideMiddle
                text: libraryModel.folder === "" ? qsTrc("starscore", "No folder chosen") : libraryModel.folder
            }
            FlatButton {
                text: qsTrc("starscore", "Choose folder…")
                enabled: !libraryModel.scanning
                onClicked: libraryModel.chooseFolder()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                text: libraryModel.status
            }
            FlatButton {
                text: qsTrc("starscore", "Check")
                toolTipTitle: qsTrc("starscore", "Check every song")
                toolTipDescription: qsTrc("starscore", "Songs that haven't changed since they were last checked are not read again")
                accentButton: true
                visible: !libraryModel.scanning
                enabled: libraryModel.folder !== ""
                onClicked: libraryModel.scan(false)
            }
            FlatButton {
                text: qsTrc("starscore", "Check all again")
                visible: !libraryModel.scanning
                enabled: libraryModel.folder !== ""
                onClicked: libraryModel.scan(true)
            }
            FlatButton {
                text: qsTrc("starscore", "Stop")
                visible: libraryModel.scanning
                onClicked: libraryModel.cancel()
            }
        }

        StyledListView {
            id: songList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 4
            model: libraryModel.songs


            delegate: Rectangle {
                id: songRow
                required property var modelData
                required property int index

                width: songList.width
                height: 48
                radius: 3
                color: songMouse.containsMouse ? ui.theme.buttonColor : "transparent"
                border.width: 1
                border.color: ui.theme.strokeColor

                MouseArea {
                    id: songMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: libraryModel.openSong(songRow.index)
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 4
                    radius: 2
                    color: songRow.modelData.error ? "#8A8FA0"
                         : songRow.modelData.done ? "#3FA66B"
                         : songRow.modelData.likely > 0 ? "#E0504A" : "#E8892B"
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 10
                    spacing: 14

                    ColumnLayout {
                        Layout.preferredWidth: 260
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
                            text: songRow.modelData.folder
                            opacity: 0.7
                        }
                    }
                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        text: songRow.modelData.issues
                    }
                    StyledTextLabel {
                        Layout.preferredWidth: 170
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        text: songRow.modelData.arrangements
                    }
                    StyledTextLabel {
                        Layout.preferredWidth: 150
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                        text: songRow.modelData.listen
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("global", "Close")
                onClicked: root.hide()
            }
        }
    }
}
