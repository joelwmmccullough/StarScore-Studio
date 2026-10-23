/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the practice runner: one day of a practice plan, a block at a time (see practicerunnermodel.h).
 * It stays open beside the score, so an exercise opened in StarScore plays with its backing while you read this.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    property string planId: ""
    property string dayIndex: "0"
    property string blockIndex: "0"

    title: qsTrc("starscore", "Practice")

    contentWidth: 820
    contentHeight: 900
    margins: 0
    resizable: true
    alwaysAboveApp: true

    readonly property color paper: "#16130F"
    readonly property color ink: "#EDE6D6"
    readonly property color soft: "#B9B0A0"
    readonly property color gold: "#CFA85E"
    readonly property color teal: "#8FB3A8"
    readonly property color doneColor: "#3FB05A"

    property int tempoReached: 0
    property int stageChoice: 2
    property string noteText: ""

    PracticeRunnerModel {
        id: run
        onChanged: {
            root.tempoReached = run.block.tempo || 0
            var s = run.block.stage !== undefined ? run.block.stage : 1
            root.stageChoice = Math.max(1, Math.min(4, s + 1))
        }
    }

    Component.onCompleted: run.load(root.planId, parseInt(root.dayIndex), parseInt(root.blockIndex))

    function clock(sec) {
        var m = Math.floor(sec / 60)
        var s = sec % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    Rectangle {
        anchors.fill: parent
        color: root.paper

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10

            // ---------------- day and block ----------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: run.planName + "  ·  " + run.dayTitle
                    color: root.soft
                    font.pixelSize: 13
                    elide: Text.ElideRight
                }
                Text {
                    text: qsTrc("starscore", "%1 of %2 done").arg(run.doneCount).arg(run.blockCount)
                    color: run.blockCount > 0 && run.doneCount === run.blockCount ? root.doneColor : root.soft
                    font.pixelSize: 13
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 6
                FlatButton {
                    icon: IconCode.ARROW_LEFT
                    minWidth: 32
                    enabled: run.blockIndex > 0
                    onClicked: run.previous()
                }
                StyledDropdown {
                    Layout.fillWidth: true
                    model: run.blocks.map(function(b) {
                        return { "text": (b.done ? "✓ " : "") + (b.index + 1) + ". " + b.song + " — " + b.title + "  (" + b.minutes + " min)",
                                 "value": b.index }
                    })
                    currentIndex: run.blockIndex
                    onActivated: function(index, value) { run.go(value) }
                }
                FlatButton {
                    icon: IconCode.ARROW_RIGHT
                    minWidth: 32
                    enabled: run.blockIndex < run.blockCount - 1
                    onClicked: run.next()
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: root.gold; opacity: 0.6 }

            StyledFlickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentHeight: body.implicitHeight + 20
                clip: true

                ColumnLayout {
                    id: body
                    width: parent.width
                    spacing: 10

                    Text {
                        text: (run.block.kindName ? run.block.kindName + "  ·  " : "") + (run.block.song || "")
                        color: root.gold
                        font.pixelSize: 14
                        font.letterSpacing: 1.5
                        font.capitalization: Font.AllUppercase
                    }
                    Text {
                        Layout.fillWidth: true
                        text: run.block.title || ""
                        color: root.ink
                        font.pixelSize: 26
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: text !== ""
                        text: [run.block.categoryName, run.block.solo, run.block.sections,
                               run.block.level ? qsTrc("starscore", "level %1").arg(run.block.level) : ""]
                              .filter(function(x) { return x }).join("  ·  ")
                        color: root.teal
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (run.block.soloInfo || "") !== ""
                        text: run.block.soloInfo || ""
                        color: root.soft
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: run.block.missing === true
                        text: qsTrc("starscore", "This item isn't in the song's practice library any more. Plan again from today on the Practice page.")
                        color: "#E0463A"
                        wrapMode: Text.WordWrap
                    }

                    // ---------------- timer ----------------
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            text: root.clock(run.secondsLeft)
                            color: run.secondsLeft === 0 ? root.gold : root.ink
                            font.pixelSize: 34
                            font.family: "Menlo"
                        }
                        FlatButton {
                            text: run.running ? qsTrc("starscore", "Pause") : qsTrc("starscore", "Start")
                            accentButton: !run.running
                            minWidth: 80
                            onClicked: run.running ? run.pauseTimer() : run.startTimer()
                        }
                        FlatButton {
                            text: qsTrc("starscore", "Reset")
                            minWidth: 70
                            onClicked: run.resetTimer()
                        }
                        Item { Layout.fillWidth: true }
                        Text {
                            visible: (run.block.tempo || 0) > 0
                            text: qsTrc("starscore", "Today ♩ = %1").arg(run.block.tempo)
                                  + ((run.block.tempoGoal || 0) > 0 ? qsTrc("starscore", "   (start %1, goal %2)").arg(run.block.tempoStart).arg(run.block.tempoGoal) : "")
                            color: root.soft
                            font.pixelSize: 13
                        }
                    }

                    // ---------------- goal and steps ----------------
                    Text {
                        Layout.fillWidth: true
                        visible: (run.block.goal || "") !== ""
                        text: run.block.goal || ""
                        color: root.ink
                        font.pixelSize: 15
                        wrapMode: Text.WordWrap
                        lineHeight: 1.2
                    }
                    Repeater {
                        model: run.block.steps || []
                        delegate: RowLayout {
                            required property string modelData
                            required property int index
                            Layout.fillWidth: true
                            spacing: 8
                            Text {
                                Layout.alignment: Qt.AlignTop
                                Layout.preferredWidth: 22
                                text: (index + 1) + "."
                                color: root.gold
                                font.pixelSize: 14
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                color: root.ink
                                font.pixelSize: 14
                                wrapMode: Text.WordWrap
                                lineHeight: 1.15
                            }
                        }
                    }

                    // ---------------- the exercise ----------------
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        spacing: 6
                        visible: run.block.hasExercise === true
                        FlatButton {
                            text: run.block.hasBacking ? qsTrc("starscore", "Play with backing") : qsTrc("starscore", "Play")
                            accentButton: true
                            minWidth: 120
                            toolTipTitle: qsTrc("starscore", "Opens the exercise in StarScore at today's tempo and plays it")
                            toolTipDescription: qsTrc("starscore", "It takes the place of the open score (you're asked to save it first if it has changes).")
                            onClicked: run.openExercise(run.block.tempo || 0, true)
                        }
                        FlatButton {
                            text: qsTrc("starscore", "Open in StarScore")
                            minWidth: 120
                            onClicked: run.openExercise(run.block.tempo || 0, false)
                        }
                        FlatButton {
                            icon: IconCode.PLAY
                            minWidth: 36
                            toolTipTitle: qsTrc("starscore", "Play or stop")
                            onClicked: run.togglePlay()
                        }
                        FlatButton {
                            icon: IconCode.METRONOME
                            minWidth: 36
                            toolTipTitle: qsTrc("starscore", "Metronome on or off")
                            onClicked: run.toggleMetronome()
                        }
                        Item { Layout.fillWidth: true }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        FlatButton {
                            visible: run.block.hasIreal === true
                            text: qsTrc("starscore", "iReal Pro")
                            minWidth: 90
                            onClicked: run.openIreal()
                        }
                        FlatButton {
                            text: qsTrc("starscore", "Open the song")
                            minWidth: 110
                            onClicked: run.openSong()
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        visible: run.rendering
                        text: qsTrc("starscore", "Engraving the exercise…")
                        color: root.soft
                        font.pixelSize: 12
                    }
                    Repeater {
                        model: run.images
                        delegate: Image {
                            required property string modelData
                            Layout.fillWidth: true
                            Layout.preferredHeight: implicitWidth > 0 ? width * implicitHeight / implicitWidth : 0
                            source: modelData
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                            mipmap: true
                            cache: false
                        }
                    }

                    // ---------------- move on when ----------------
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: 6
                        visible: (run.block.moveOnWhen || "") !== ""
                        implicitHeight: moveOn.implicitHeight + 20
                        color: "transparent"
                        border.color: root.gold
                        border.width: 1
                        radius: 2
                        Text {
                            id: moveOn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.margins: 10
                            text: "<b>" + qsTrc("starscore", "MOVE ON WHEN") + "</b>&nbsp;&nbsp;" + (run.block.moveOnWhen || "")
                            textFormat: Text.StyledText
                            color: root.gold
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (run.block.listen || "") !== ""
                        text: qsTrc("starscore", "Listen: %1").arg(run.block.listen || "")
                        color: root.soft
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (run.block.refs || "") !== ""
                        text: qsTrc("starscore", "In the score: %1").arg(run.block.refs || "")
                        color: root.soft
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (run.block.sessions || 0) > 0
                        text: qsTrc("starscore", "So far: %1 sessions, %2%3")
                              .arg(run.block.sessions).arg(run.block.stageName)
                              .arg((run.block.bestTempo || 0) > 0 ? qsTrc("starscore", ", best ♩ = %1").arg(run.block.bestTempo) : "")
                              + ((run.block.lastNote || "") !== "" ? qsTrc("starscore", ". Last note: %1").arg(run.block.lastNote) : "")
                        color: root.teal
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: root.gold; opacity: 0.6 }

            // ---------------- done ----------------
            Text {
                Layout.fillWidth: true
                visible: run.message !== ""
                text: run.message
                color: root.doneColor
                font.pixelSize: 14
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: run.blockCount > 0
                Text {
                    text: qsTrc("starscore", "Tempo reached")
                    color: root.soft
                    visible: (run.block.tempo || 0) > 0
                }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 90
                    visible: (run.block.tempo || 0) > 0
                    currentValue: root.tempoReached
                    step: 2; decimals: 0; minValue: 0; maxValue: 400
                    onValueEdited: function(v) { root.tempoReached = v }
                }
                StyledDropdown {
                    Layout.preferredWidth: 150
                    model: [ { "text": qsTrc("starscore", "Started"), "value": 1 },
                             { "text": qsTrc("starscore", "Getting there"), "value": 2 },
                             { "text": qsTrc("starscore", "Solid"), "value": 3 },
                             { "text": qsTrc("starscore", "Mastered"), "value": 4 } ]
                    currentIndex: root.stageChoice - 1
                    onActivated: function(index, value) { root.stageChoice = value }
                }
                TextInputField {
                    id: noteField
                    Layout.fillWidth: true
                    hint: qsTrc("starscore", "A note for next time (optional)")
                    currentText: root.noteText
                    onTextChanged: function(t) { root.noteText = t }
                }
                FlatButton {
                    text: run.block.done ? qsTrc("starscore", "Done again") : qsTrc("starscore", "Done")
                    accentButton: true
                    minWidth: 90
                    onClicked: {
                        run.markDone(root.tempoReached, root.stageChoice, root.noteText)
                        root.noteText = ""
                    }
                }
                FlatButton {
                    visible: run.block.done === true
                    text: qsTrc("starscore", "Not done")
                    minWidth: 80
                    onClicked: run.markNotDone()
                }
            }
        }
    }
}
