/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the Practice panel (1.27.0).
 * - Watch: falling notes for the score showing, over StarScore's playback. Play, speed, loop (the selected bars) and
 *   count-in are playback's own.
 * - Jam: a backing band (bass, drums, piano) from the score's chord symbols, iReal Pro style: a solo's form, or the
 *   song's bars (or only the selected ones), in its key, at any tempo, for as many choruses as wanted.
 * Make the panel taller, or float it and make it big, for watching; keep it low under the score while reading.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

Item {
    id: root

    property NavigationSection navigationSection: null
    property int navigationOrderStart: 1

    PracticePanelModel {
        id: practice
    }

    Component.onCompleted: practice.load()

    StyledTextLabel {
        anchors.centerIn: parent
        visible: !practice.hasScore
        text: qsTrc("starscore", "Open a song to practise it.")
    }

    ColumnLayout {
        anchors.fill: parent
        visible: practice.hasScore
        spacing: 0

        // --- the mode, and Watch's transport (StarScore's playback) ---
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 6
            spacing: 6

            FlatButton {
                text: qsTrc("starscore", "Watch")
                accentButton: practice.mode === "watch"
                toolTipTitle: qsTrc("starscore", "Falling notes")
                toolTipDescription: qsTrc("starscore", "The score showing, falling onto a keyboard as it plays")
                onClicked: practice.mode = "watch"
            }
            FlatButton {
                text: qsTrc("starscore", "Jam")
                accentButton: practice.mode === "jam"
                toolTipTitle: qsTrc("starscore", "Backing band")
                toolTipDescription: qsTrc("starscore", "Bass, drums and piano from the chord symbols, to solo over")
                onClicked: practice.mode = "jam"
            }

            Rectangle { width: 1; Layout.fillHeight: true; Layout.margins: 4; color: ui.theme.strokeColor }

            // Watch
            RowLayout {
                visible: practice.mode === "watch"
                spacing: 6

                FlatButton {
                    icon: IconCode.REWIND
                    toolTipTitle: qsTrc("starscore", "Back to the start")
                    onClicked: practice.rewind()
                }
                FlatButton {
                    icon: practice.playing ? IconCode.PAUSE : IconCode.PLAY
                    accentButton: !practice.playing
                    toolTipTitle: practice.playing ? qsTrc("starscore", "Pause") : qsTrc("starscore", "Play")
                    toolTipDescription: qsTrc("starscore", "From the selection or where playback stopped")
                    onClicked: practice.togglePlay()
                }
                FlatToggleButton {
                    icon: IconCode.LOOP
                    checked: practice.loop
                    toolTipTitle: qsTrc("starscore", "Loop")
                    toolTipDescription: qsTrc("starscore", "Select the bars to loop first")
                    onToggled: practice.toggleLoop()
                }
                FlatToggleButton {
                    icon: IconCode.COUNT_IN
                    checked: practice.countIn
                    toolTipTitle: qsTrc("starscore", "Count-in")
                    onToggled: practice.toggleCountIn()
                }
                StyledTextLabel {
                    Layout.leftMargin: 10
                    text: qsTrc("starscore", "Speed")
                }
                StyledSlider {
                    Layout.preferredWidth: 140
                    from: 25
                    to: 150
                    stepSize: 5
                    value: practice.speed
                    onMoved: practice.speed = value
                }
                StyledTextLabel {
                    Layout.preferredWidth: 40
                    horizontalAlignment: Text.AlignLeft
                    text: practice.speed + "%"
                }
            }

            // Jam
            RowLayout {
                visible: practice.mode === "jam"
                spacing: 6

                FlatButton {
                    icon: practice.jamPlaying ? IconCode.STOP : IconCode.PLAY
                    accentButton: !practice.jamPlaying
                    toolTipTitle: practice.jamPlaying ? qsTrc("starscore", "Stop the band") : qsTrc("starscore", "Play the band")
                    onClicked: practice.jamPlay()
                }
                StyledDropdown {
                    Layout.preferredWidth: 150
                    model: practice.grooves
                    currentIndex: indexOfValue(practice.groove)
                    onActivated: function(index, value) { practice.groove = value }
                }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 90
                    currentValue: practice.jamBpm
                    step: 4
                    decimals: 0
                    minValue: 30
                    maxValue: 400
                    measureUnitsSymbol: qsTrc("starscore", "bpm")
                    onValueEdited: function(v) { practice.jamBpm = v }
                }
                StyledTextLabel { text: qsTrc("starscore", "Choruses") }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 64
                    currentValue: practice.choruses
                    step: 1
                    decimals: 0
                    minValue: 1
                    maxValue: 99
                    onValueEdited: function(v) { practice.choruses = v }
                }
                StyledTextLabel { text: qsTrc("starscore", "Faster each chorus") }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: practice.tempoStep
                    step: 2
                    decimals: 0
                    minValue: 0
                    maxValue: 40
                    measureUnitsSymbol: qsTrc("starscore", "bpm")
                    onValueEdited: function(v) { practice.tempoStep = v }
                }
            }

            Item { Layout.fillWidth: true }

            // Watch's look
            RowLayout {
                visible: practice.mode === "watch"
                spacing: 6

                StyledTextLabel { text: qsTrc("starscore", "Colours") }
                StyledDropdown {
                    Layout.preferredWidth: 130
                    model: [
                        { "text": qsTrc("starscore", "Hands"), "value": "hands" },
                        { "text": qsTrc("starscore", "Note colours"), "value": "pitch" }
                    ]
                    currentIndex: practice.colors === "pitch" ? 1 : 0
                    onActivated: function(index, value) { practice.colors = value }
                }
                StyledTextLabel {
                    Layout.leftMargin: 8
                    text: qsTrc("starscore", "Look ahead")
                }
                StyledSlider {
                    Layout.preferredWidth: 100
                    from: 1
                    to: 12
                    stepSize: 1
                    value: practice.secondsShown
                    onMoved: practice.secondsShown = value
                }
                StyledTextLabel {
                    Layout.preferredWidth: 30
                    horizontalAlignment: Text.AlignLeft
                    text: practice.secondsShown + " s"
                }
            }
        }

        // --- Jam's second row: what plays ---
        RowLayout {
            visible: practice.mode === "jam"
            Layout.fillWidth: true
            Layout.leftMargin: 6
            Layout.rightMargin: 6
            spacing: 12

            CheckBox {
                text: qsTrc("starscore", "Bass")
                checked: practice.bassOn
                onClicked: practice.bassOn = !checked
            }
            CheckBox {
                text: qsTrc("starscore", "Drums")
                checked: practice.drumsOn
                onClicked: practice.drumsOn = !checked
            }
            CheckBox {
                text: qsTrc("starscore", "Piano")
                checked: practice.pianoOn
                onClicked: practice.pianoOn = !checked
            }
            StyledTextLabel { text: qsTrc("starscore", "Count-in bars") }
            IncrementalPropertyControl {
                Layout.preferredWidth: 56
                currentValue: practice.countInBars
                step: 1
                decimals: 0
                minValue: 0
                maxValue: 4
                onValueEdited: function(v) { practice.countInBars = v }
            }
            StyledTextLabel { text: qsTrc("starscore", "Key") }
            IncrementalPropertyControl {
                Layout.preferredWidth: 72
                currentValue: practice.transpose
                step: 1
                decimals: 0
                minValue: -11
                maxValue: 11
                measureUnitsSymbol: qsTrc("starscore", "st")
                onValueEdited: function(v) { practice.transpose = v }
            }
            CheckBox {
                text: qsTrc("starscore", "Selected bars only")
                checked: practice.selectedBarsOnly
                onClicked: practice.selectedBarsOnly = !checked
            }
            Item { Layout.fillWidth: true }
        }

        // --- Watch: the falling notes ---
        FallingNotesView {
            id: fall
            visible: practice.mode === "watch"
            Layout.fillWidth: true
            Layout.fillHeight: true
            colors: practice.colors
            secondsShown: practice.secondsShown
            dark: ui.theme.isDark

            StyledTextLabel {
                anchors.centerIn: parent
                visible: fall.noteCount === 0
                color: "#B0A898"
                text: qsTrc("starscore", "No notes to show in this score.")
            }
        }

        // --- Jam: where the band is, the chord now and next ---
        Rectangle {
            visible: practice.mode === "jam"
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: ui.theme.isDark ? "#15120E" : "#F4F1EA"

            ColumnLayout {
                anchors.centerIn: parent
                width: parent.width - 24
                spacing: 6

                StyledTextLabel {
                    Layout.fillWidth: true
                    visible: !practice.jamPlaying
                    wrapMode: Text.WordWrap
                    text: practice.jamInfo
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    visible: practice.jamPlaying
                    text: practice.jamWhere
                    opacity: 0.75
                }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    visible: practice.jamPlaying
                    spacing: 24

                    StyledTextLabel {
                        text: practice.jamChord
                        font.pixelSize: 44
                        font.bold: true
                    }
                    StyledTextLabel {
                        visible: practice.jamNext !== ""
                        text: "→ " + practice.jamNext
                        font.pixelSize: 26
                        opacity: 0.6
                    }
                }
            }
        }
    }
}
