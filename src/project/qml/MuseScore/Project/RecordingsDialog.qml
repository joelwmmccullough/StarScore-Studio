/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Recordings: this song's live takes (with your stars), album tracks, sessions and originals.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Recordings") + (rec.songTitle !== "" ? " — " + rec.songTitle : "")

    contentWidth: 860
    contentHeight: 620
    margins: 16

    property string addError: ""
    property string newShowId: ""
    property string addDate: ""
    property string addVenue: ""
    property string addLink: ""
    property string addTime: ""

    RecordingsModel { id: rec }

    Component.onCompleted: rec.load()

    readonly property var kindChoices: [
        { "text": qsTrc("starscore", "Album"), "value": "album" },
        { "text": qsTrc("starscore", "Unreleased studio mix"), "value": "wip" },
        { "text": qsTrc("starscore", "Live session"), "value": "session" },
        { "text": qsTrc("starscore", "The original (covers)"), "value": "original" }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            opacity: 0.8
            text: rec.status
        }

        StyledTextLabel { text: qsTrc("starscore", "Live performances"); font: ui.theme.bodyBoldFont; horizontalAlignment: Text.AlignLeft }

        StyledListView {
            id: perfList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 2
            model: rec.performances

            delegate: RowLayout {
                id: prow
                required property var modelData
                width: perfList.width
                spacing: 8

                StyledTextLabel { Layout.preferredWidth: 90; horizontalAlignment: Text.AlignLeft; text: prow.modelData.date }
                StyledTextLabel { Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft; elide: Text.ElideRight; text: prow.modelData.venue }
                TextInputField {
                    Layout.preferredWidth: 80
                    currentText: prow.modelData.time
                    onTextEditingFinished: function(t) { rec.setPerformanceField(prow.modelData.id, "seconds", t) }
                }
                TextInputField {
                    Layout.preferredWidth: 140
                    currentText: prow.modelData.variant
                    hint: qsTrc("starscore", "note (encore, intro…)")
                    onTextEditingFinished: function(t) { rec.setPerformanceField(prow.modelData.id, "variant", t) }
                }
                Row {
                    spacing: 0
                    Repeater {
                        model: 5
                        delegate: FlatButton {
                            required property int index
                            width: 22
                            height: 24
                            transparent: true
                            text: index < prow.modelData.rating ? "★" : "☆"
                            onClicked: rec.setRating(prow.modelData.id, prow.modelData.rating === index + 1 ? 0 : index + 1)
                        }
                    }
                }
                FlatButton {
                    text: qsTrc("starscore", "Watch")
                    onClicked: Qt.openUrlExternally(prow.modelData.link)
                }
                FlatButton {
                    text: qsTrc("starscore", "Remove")
                    onClicked: rec.removePerformance(prow.modelData.id)
                }
            }
        }

        // add a performance
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            StyledDropdown {
                id: showPick
                Layout.preferredWidth: 260
                model: rec.showChoices
                currentIndex: indexOfValue(root.newShowId)
                onActivated: function(i, value) { root.newShowId = value }
            }
            TextInputField {
                Layout.preferredWidth: 100; visible: root.newShowId === ""; hint: qsTrc("starscore", "yyyy-mm-dd")
                currentText: root.addDate; onTextEdited: function(t) { root.addDate = t }
            }
            TextInputField {
                Layout.preferredWidth: 120; visible: root.newShowId === ""; hint: qsTrc("starscore", "venue")
                currentText: root.addVenue; onTextEdited: function(t) { root.addVenue = t }
            }
            TextInputField {
                Layout.fillWidth: true; visible: root.newShowId === ""; hint: qsTrc("starscore", "YouTube link")
                currentText: root.addLink; onTextEdited: function(t) { root.addLink = t }
            }
            TextInputField {
                Layout.preferredWidth: 70; hint: qsTrc("starscore", "1:06:25")
                currentText: root.addTime; onTextEdited: function(t) { root.addTime = t }
            }
            FlatButton {
                text: qsTrc("starscore", "Add")
                onClicked: {
                    root.addError = rec.addPerformance(root.newShowId, root.addTime, "", root.addDate, root.addVenue, root.addLink)
                    if (root.addError === "") {
                        root.addTime = ""
                    }
                }
            }
        }
        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            visible: root.addError !== ""
            color: "#d04040"
            text: root.addError
        }

        StyledTextLabel { text: qsTrc("starscore", "Albums, sessions and originals"); font: ui.theme.bodyBoldFont; horizontalAlignment: Text.AlignLeft }

        StyledListView {
            id: relList
            Layout.fillWidth: true
            Layout.preferredHeight: 150
            clip: true
            spacing: 2
            model: rec.releases

            delegate: RowLayout {
                id: rrow
                required property var modelData
                width: relList.width
                spacing: 8

                StyledDropdown {
                    Layout.preferredWidth: 180
                    model: root.kindChoices
                    currentIndex: indexOfValue(rrow.modelData.kind)
                    onActivated: function(i, value) { rec.setReleaseField(rrow.modelData.id, "kind", value) }
                }
                TextInputField {
                    Layout.preferredWidth: 160
                    currentText: rrow.modelData.label
                    hint: qsTrc("starscore", "album / who")
                    onTextEditingFinished: function(t) { rec.setReleaseField(rrow.modelData.id, "label", t) }
                }
                TextInputField {
                    Layout.preferredWidth: 100
                    currentText: rrow.modelData.date
                    hint: qsTrc("starscore", "yyyy-mm-dd")
                    onTextEditingFinished: function(t) { rec.setReleaseField(rrow.modelData.id, "date", t) }
                }
                TextInputField {
                    Layout.fillWidth: true
                    currentText: rrow.modelData.link
                    hint: qsTrc("starscore", "YouTube link")
                    onTextEditingFinished: function(t) { rec.setReleaseField(rrow.modelData.id, "link", t) }
                }
                TextInputField {
                    Layout.preferredWidth: 120
                    currentText: rrow.modelData.note
                    hint: qsTrc("starscore", "note")
                    onTextEditingFinished: function(t) { rec.setReleaseField(rrow.modelData.id, "note", t) }
                }
                FlatButton {
                    text: qsTrc("starscore", "Remove")
                    onClicked: rec.removeRelease(rrow.modelData.id)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            FlatButton {
                text: qsTrc("starscore", "Add album / session")
                onClicked: rec.addRelease()
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTrc("starscore", "Save")
                accentButton: rec.dirty
                onClicked: rec.save()
            }
            FlatButton {
                text: qsTrc("starscore", "Close")
                onClicked: root.hide()
            }
        }
    }
}
