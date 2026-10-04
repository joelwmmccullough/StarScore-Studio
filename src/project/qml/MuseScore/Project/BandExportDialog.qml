/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Export to Sheets and Demos: tick the sheets to write.
 * Un-ticked sheets are remembered per song for the next export.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Export to Sheets and Demos")

    contentWidth: 700
    contentHeight: 620
    margins: 16

    property bool done: false
    // the sheet list shows once the song has a folder in Sheets and Demos
    readonly property bool listMode: exportModel.errorText === "" && !exportModel.newSong && !root.done

    // new song form
    property string newTitle: ""
    property string newCode: ""
    property int newCategory: 1
    property string newError: ""

    BandExportModel {
        id: exportModel
    }

    Component.onCompleted: {
        exportModel.load()
        root.newTitle = exportModel.songTitle
        root.newCode = exportModel.suggestedCode
    }

    function folderPreview() {
        var t = root.newTitle.trim()
        if (t === "") {
            return ""
        }
        return root.newCategory === 4 ? "4 Works In Progress/" + t : root.newCategory + " " + t
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            font: ui.theme.bodyBoldFont
            text: exportModel.errorText !== "" ? exportModel.errorText : exportModel.heading
            wrapMode: Text.WordWrap
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            color: ui.theme.fontSecondaryColor
            visible: root.listMode
            text: qsTrc("starscore", "Sheets that already exist are moved to Version History/Superseded <today> first. "
                        + "Your ticks are remembered for this song.")
        }

        // Version: keep it, or raise one of the three numbers for this export
        RowLayout {
            visible: root.listMode
            spacing: 12

            StyledTextLabel {
                text: qsTrc("starscore", "Version %1  →").arg(exportModel.currentVersion)
                font: ui.theme.bodyBoldFont
            }
            CheckBox {
                text: qsTrc("starscore", "+ first number")
                checked: exportModel.bump === 1
                onClicked: exportModel.bump = checked ? 0 : 1
            }
            CheckBox {
                text: qsTrc("starscore", "+ second")
                checked: exportModel.bump === 2
                onClicked: exportModel.bump = checked ? 0 : 2
            }
            CheckBox {
                text: qsTrc("starscore", "+ third")
                checked: exportModel.bump === 3
                onClicked: exportModel.bump = checked ? 0 : 3
            }
            StyledTextLabel {
                text: qsTrc("starscore", "Exports as Version %1").arg(exportModel.exportVersion)
                color: ui.theme.fontSecondaryColor
            }
        }

        RowLayout {
            visible: root.listMode
            spacing: 8
            FlatButton { text: qsTrc("starscore", "Tick all"); onClicked: exportModel.setAllChecked(true) }
            FlatButton { text: qsTrc("starscore", "Untick all"); onClicked: exportModel.setAllChecked(false) }
        }

        StyledListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.listMode
            spacing: 2
            // the model itself is the list (one row per folder header and sheet); a tick updates its row in place
            model: exportModel

            delegate: Item {
                id: row
                required property int index
                required property bool header
                required property string name
                required property bool checked
                width: list.width
                height: row.header ? 34 : 28

                CheckBox {
                    anchors.left: parent.left
                    anchors.leftMargin: row.header ? 0 : 28
                    anchors.verticalCenter: parent.verticalCenter
                    text: row.name
                    font: row.header ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                    checked: row.checked
                    onClicked: exportModel.setChecked(row.index, !row.checked)
                }
            }
        }


        // --- New song: where it goes in Sheets and Demos, and its code
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: exportModel.newSong && !root.done
            spacing: 10

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                text: qsTrc("starscore", "This song isn't in Sheets and Demos yet. StarScore will make its folder (with Demos and Version History), "
                            + "add its code to 6 Inbox/.organizer/codes.json, and then list the sheets to export. "
                            + "Sheets are named “CODE - …” and sorted into the same folders as the other songs.")
            }

            StyledTextLabel { text: qsTrc("starscore", "Song title"); font: ui.theme.bodyBoldFont }
            TextInputField {
                Layout.fillWidth: true
                currentText: root.newTitle
                hint: qsTrc("starscore", "Song title")
                onTextEdited: function(t) { root.newTitle = t }
            }

            StyledTextLabel { text: qsTrc("starscore", "Kind of song"); font: ui.theme.bodyBoldFont }
            StyledDropdown {
                Layout.preferredWidth: 360
                model: [
                    { text: qsTrc("starscore", "1 – Original"), value: 1 },
                    { text: qsTrc("starscore", "2 – Cover"), value: 2 },
                    { text: qsTrc("starscore", "3 – Cover that needs a vocalist"), value: 3 },
                    { text: qsTrc("starscore", "4 – Work in progress"), value: 4 }
                ]
                currentIndex: indexOfValue(root.newCategory)
                onActivated: function(i, value) { root.newCategory = value }
            }

            StyledTextLabel { text: qsTrc("starscore", "Four-letter code"); font: ui.theme.bodyBoldFont }
            TextInputField {
                Layout.preferredWidth: 120
                currentText: root.newCode
                maximumLength: 4
                onTextEdited: function(t) { root.newCode = t.toUpperCase() }
            }

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                color: ui.theme.fontSecondaryColor
                visible: root.folderPreview() !== ""
                text: qsTrc("starscore", "Folder: Sheets and Demos/%1").arg(root.folderPreview())
            }

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                visible: root.newError !== ""
                color: "#d04040"
                text: root.newError
            }

            Item { Layout.fillHeight: true }
        }

        // the plan's notes before the export, the export's result after it: one binding, never assigned to
        // (an assignment used to replace the binding, so the notes came back when the model reloaded)
        StyledTextLabel {
            id: resultLabel
            Layout.fillWidth: true
            Layout.fillHeight: root.done
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignTop
            wrapMode: Text.WordWrap
            visible: root.done || (exportModel.notes !== "" && root.listMode)
            color: root.done ? ui.theme.fontPrimaryColor : ui.theme.fontSecondaryColor
            text: root.done ? exportModel.result : exportModel.notes
        }

        CheckBox {
            visible: root.listMode && !root.done
            text: qsTrc("starscore", "Run organization process")
            checked: exportModel.runOrganizer
            onClicked: exportModel.runOrganizer = !checked
        }

        ButtonBox {
            Layout.fillWidth: true
            buttons: [ ButtonBoxModel.Close ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                visible: exportModel.newSong && !root.done
                enabled: root.newTitle.trim() !== "" && root.newCode.length === 4
                text: qsTrc("starscore", "Add song")
                accentButton: true
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 2
                onClicked: {
                    root.newError = exportModel.createSong(root.newTitle, root.newCategory, root.newCode)
                }
            }

            FlatButton {
                visible: root.listMode
                enabled: exportModel.checkedCount > 0
                text: qsTrc("starscore", "Export %n sheet(s)", "", exportModel.checkedCount)
                accentButton: true
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: {
                    root.done = true
                    exportModel.exportNow()    // its summary shows through exportModel.result
                    if (exportModel.runOrganizer) {
                        exportModel.openOrganizer()
                        root.hide()
                    }
                }
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Close) {
                    root.hide()
                }
            }
        }
    }
}
