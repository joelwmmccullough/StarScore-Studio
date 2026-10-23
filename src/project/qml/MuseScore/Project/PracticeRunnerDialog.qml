/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the practice runner: one day of a practice plan, a block at a time (see practicerunnermodel.h).
 * It stays open beside the score, so an exercise opened in StarScore plays with its backing while you read this.
 *
 * Each block's parts have their own timer, a Play example, and a Play along (the backing, stepping up in tempo).
 * Hands free plays the whole day without stopping: each part's play-along and timer, breaks, and quick ratings in the
 * breaks, with the notation fitted to the window (no scrolling).
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    property string planId: ""
    property string dayIndex: "0"
    property string blockIndex: "0"
    property string handsFree: "0"

    title: qsTrc("starscore", "Practice")

    contentWidth: 860
    contentHeight: 800
    margins: 0
    resizable: true
    alwaysAboveApp: true

    readonly property color paper: "#16130F"
    readonly property color ink: "#EDE6D6"
    readonly property color soft: "#B9B0A0"
    readonly property color gold: "#CFA85E"
    readonly property color teal: "#8FB3A8"
    readonly property color doneColor: "#3FB05A"
    readonly property color line: "#3A332A"

    property int tempoReached: 0
    property int stageChoice: 2
    property string noteText: ""
    property bool fullScreen: false
    readonly property bool hf: run.handsFree !== ""

    PracticeRunnerModel {
        id: run
        onChanged: {
            root.tempoReached = run.block.tempo || 0
            var s = run.block.stage !== undefined ? run.block.stage : 1
            root.stageChoice = Math.max(1, Math.min(4, s + 1))
        }
    }

    Component.onCompleted: {
        run.load(root.planId, parseInt(root.dayIndex), parseInt(root.blockIndex))
        if (root.handsFree === "1") {
            run.startHandsFree()
        }
    }
    Component.onDestruction: {
        run.keepUnrated()
        run.stopHandsFree()
    }

    //! a text size, at the size chosen in the runner (Text − / +)
    function px(n) { return Math.round(n * run.textScale) }

    function clock(sec) {
        var m = Math.floor(sec / 60)
        var s = sec % 60
        return m + ":" + (s < 10 ? "0" : "") + s
    }

    function toggleFullScreen() {
        if (!root.window) {
            return
        }
        if (root.fullScreen) {
            root.window.showNormal()
        } else {
            root.window.showFullScreen()
        }
        root.fullScreen = !root.fullScreen
    }

    function passText() {
        if (!run.playingAlong || run.passTempos.length === 0 || run.currentPassTempo <= 0) {
            return ""
        }
        var i = run.passTempos.indexOf(run.currentPassTempo)
        return "♩ = " + run.currentPassTempo + (run.passTempos.length > 1 ? "   " + qsTrc("starscore", "pass %1 of %2").arg(i + 1).arg(run.passTempos.length) : "")
    }

    //! "Text  −  120%  +": steps a size by 10%
    component SizeControl: RowLayout {
        id: sc
        property string label: ""
        property real value: 1.0
        property real minimum: 0.5
        property real maximum: 2.0
        signal step(real delta)
        spacing: 2
        Text {
            text: sc.label
            color: root.soft
            font.pixelSize: root.px(12)
            rightPadding: 2
        }
        FlatButton {
            text: "−"
            minWidth: 26
            enabled: sc.value > sc.minimum + 0.001
            toolTipTitle: qsTrc("starscore", "Smaller")
            onClicked: sc.step(-0.1)
        }
        Text {
            text: Math.round(sc.value * 100) + "%"
            color: root.ink
            font.pixelSize: root.px(12)
            horizontalAlignment: Text.AlignHCenter
            Layout.preferredWidth: root.px(12) * 3
        }
        FlatButton {
            text: "+"
            minWidth: 26
            enabled: sc.value < sc.maximum - 0.001
            toolTipTitle: qsTrc("starscore", "Bigger")
            onClicked: sc.step(0.1)
        }
    }

    //! The notation of the current part, every page fitted into the space it is given (no scrolling)
    component FittedNotation: Item {
        id: fit
        Column {
            anchors.fill: parent
            spacing: 4
            Repeater {
                model: run.images
                delegate: Image {
                    required property string modelData
                    width: fit.width
                    height: (fit.height - 4 * Math.max(0, run.images.length - 1)) / Math.max(1, run.images.length)
                    source: modelData
                    fillMode: Image.PreserveAspectFit
                    horizontalAlignment: Image.AlignHCenter
                    verticalAlignment: Image.AlignTop
                    smooth: true
                    mipmap: true
                    cache: false
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: run.rendering
            text: qsTrc("starscore", "Engraving…")
            color: root.soft
            font.pixelSize: root.px(14)
        }
    }

    //! Quick ratings for blocks played hands free
    component Ratings: ColumnLayout {
        spacing: 6
        visible: run.toRate.length > 0
        Text {
            text: qsTrc("starscore", "How did these go?")
            color: root.gold
            font.pixelSize: root.px(15)
            font.bold: true
        }
        Repeater {
            model: run.toRate
            delegate: RowLayout {
                id: rateRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    text: rateRow.modelData.song + " · " + rateRow.modelData.title
                    color: root.ink
                    font.pixelSize: root.px(14)
                    elide: Text.ElideRight
                }
                Repeater {
                    model: [ { "t": qsTrc("starscore", "Started"), "v": 1 }, { "t": qsTrc("starscore", "Getting there"), "v": 2 },
                             { "t": qsTrc("starscore", "Solid"), "v": 3 }, { "t": qsTrc("starscore", "Mastered"), "v": 4 } ]
                    delegate: FlatButton {
                        required property var modelData
                        text: modelData.t
                        minWidth: 70
                        onClicked: run.rate(rateRow.modelData.index, modelData.v)
                    }
                }
            }
        }
        FlatButton {
            text: qsTrc("starscore", "Skip ratings")
            minWidth: 110
            toolTipTitle: qsTrc("starscore", "Marks them done and keeps how solid they were")
            onClicked: run.keepUnrated()
        }
    }

    Rectangle {
        anchors.fill: parent
        color: root.paper

        // ======================================================== hands free
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 8
            visible: root.hf

            // the one line on top: what, the part, the clock, the tempo, and the controls
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: (run.block.song || "") + (run.parts.length > 1
                              ? "  ·  " + qsTrc("starscore", "part %1 of %2").arg(run.partIndex + 1).arg(run.parts.length) : "")
                        color: root.gold
                        font.pixelSize: root.px(13)
                        font.capitalization: Font.AllUppercase
                        font.letterSpacing: 1.2
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: run.handsFree === "break" ? qsTrc("starscore", "Break")
                              : run.handsFree === "done" ? qsTrc("starscore", "That's the day done")
                              : (run.parts.length > 1 ? (run.parts[run.partIndex] ? run.parts[run.partIndex].label : "") : (run.block.title || ""))
                        color: root.ink
                        font.pixelSize: root.px(24)
                        font.bold: true
                        elide: Text.ElideRight
                    }
                }
                Text {
                    text: root.passText()
                    visible: text !== "" && run.handsFree === "practice"
                    color: root.teal
                    font.pixelSize: root.px(18)
                }
                Text {
                    text: root.clock(run.handsFree === "break" ? run.breakSecondsLeft : run.partSecondsLeft)
                    visible: run.handsFree !== "done"
                    color: root.ink
                    font.pixelSize: root.px(40)
                    font.family: "Menlo"
                }
                ColumnLayout {
                    spacing: 4
                    RowLayout {
                        spacing: 4
                        FlatButton {
                            visible: run.handsFree !== "done"
                            text: run.handsFree === "paused" ? qsTrc("starscore", "Resume") : qsTrc("starscore", "Pause")
                            accentButton: run.handsFree === "paused"
                            minWidth: 80
                            onClicked: run.handsFree === "paused" ? run.resumeHandsFree() : run.pauseHandsFree()
                        }
                        FlatButton {
                            visible: run.handsFree === "practice"
                            text: qsTrc("starscore", "Skip")
                            minWidth: 60
                            toolTipTitle: qsTrc("starscore", "On to the next part")
                            onClicked: run.skipPart()
                        }
                        FlatButton {
                            text: qsTrc("starscore", "Stop")
                            minWidth: 60
                            toolTipTitle: qsTrc("starscore", "Back to the runner")
                            onClicked: run.stopHandsFree()
                        }
                        FlatButton {
                            text: root.fullScreen ? qsTrc("starscore", "Window") : qsTrc("starscore", "Full screen")
                            minWidth: 90
                            onClicked: root.toggleFullScreen()
                        }
                    }
                    Text {
                        Layout.alignment: Qt.AlignRight
                        text: qsTrc("starscore", "%1 left today").arg(run.dayMinutesLeft + " min")
                        color: root.soft
                        font.pixelSize: root.px(12)
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                visible: run.handsFree === "practice" || run.handsFree === "paused"
                text: (run.block.goal || "") + ((run.block.steps || []).length > 0 ? "   ·   " + run.block.steps.join("   ·   ") : "")
                color: root.soft
                font.pixelSize: root.px(14)
                wrapMode: Text.WordWrap
                maximumLineCount: 2
                elide: Text.ElideRight
            }
            Rectangle { Layout.fillWidth: true; height: 1; color: root.gold; opacity: 0.5 }

            // the notation, fitted
            FittedNotation {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: run.handsFree === "practice" || run.handsFree === "paused"
            }
            Text {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: (run.handsFree === "practice" || run.handsFree === "paused") && run.images.length === 0 && !run.rendering
                text: run.block.moveOnWhen ? qsTrc("starscore", "Move on when: %1").arg(run.block.moveOnWhen) : ""
                color: root.ink
                font.pixelSize: root.px(20)
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

            // the break, and the end of the day
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: run.handsFree === "break" || run.handsFree === "done"
                spacing: 14
                Text {
                    visible: run.handsFree === "break"
                    text: qsTrc("starscore", "Next: %1").arg(run.nextUp || (run.block.song + " · " + run.block.title))
                    color: root.teal
                    font.pixelSize: root.px(16)
                }
                FlatButton {
                    visible: run.handsFree === "break"
                    text: qsTrc("starscore", "End the break")
                    accentButton: true
                    minWidth: 140
                    onClicked: run.skipBreak()
                }
                RowLayout {
                    visible: run.handsFree === "done"
                    spacing: 8
                    Text {
                        text: qsTrc("starscore", "Keep going? Add")
                        color: root.ink
                        font.pixelSize: root.px(16)
                    }
                    Repeater {
                        model: [15, 30, 45, 60]
                        delegate: FlatButton {
                            required property int modelData
                            text: qsTrc("starscore", "%1 min").arg(modelData)
                            minWidth: 70
                            onClicked: run.addMinutes(modelData)
                        }
                    }
                }
                Ratings { Layout.fillWidth: true }
                Item { Layout.fillHeight: true }
            }
        }

        // ======================================================== the runner
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 18
            spacing: 10
            visible: !root.hf

            // ---------------- day, sizes, hands free ----------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: run.planName + "  ·  " + run.dayTitle
                    color: root.soft
                    font.pixelSize: root.px(13)
                    elide: Text.ElideRight
                }
                SizeControl {
                    label: qsTrc("starscore", "Text")
                    value: run.textScale
                    minimum: 0.7; maximum: 2.0
                    onStep: function(delta) { run.textScale = run.textScale + delta }
                }
                SizeControl {
                    label: qsTrc("starscore", "Score")
                    value: run.scoreScale
                    minimum: 0.6; maximum: 2.0
                    onStep: function(delta) { run.scoreScale = run.scoreScale + delta }
                }
                Text {
                    text: qsTrc("starscore", "%1 of %2 done").arg(run.doneCount).arg(run.blockCount)
                    color: run.blockCount > 0 && run.doneCount === run.blockCount ? root.doneColor : root.soft
                    font.pixelSize: root.px(13)
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
                FlatButton {
                    text: qsTrc("starscore", "Hands free")
                    accentButton: true
                    minWidth: 110
                    toolTipTitle: qsTrc("starscore", "Plays the day from here without stopping")
                    toolTipDescription: qsTrc("starscore", "Each part's backing and timer start by themselves, it moves on when the time is up, and it takes breaks.")
                    onClicked: run.startHandsFree()
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
                        font.pixelSize: root.px(14)
                        font.letterSpacing: 1.5
                        font.capitalization: Font.AllUppercase
                    }
                    Text {
                        Layout.fillWidth: true
                        text: run.block.title || ""
                        color: root.ink
                        font.pixelSize: root.px(26)
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
                        font.pixelSize: root.px(13)
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: run.block.missing === true
                        text: qsTrc("starscore", "This item isn't in the song's practice library any more. Plan again from today on the Practice page.")
                        color: "#E0463A"
                        font.pixelSize: root.px(13)
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (run.block.goal || "") !== ""
                        text: run.block.goal || ""
                        color: root.ink
                        font.pixelSize: root.px(16)
                        wrapMode: Text.WordWrap
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
                                Layout.preferredWidth: root.px(20)
                                text: (index + 1) + "."
                                color: root.gold
                                font.pixelSize: root.px(14)
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData
                                color: root.ink
                                font.pixelSize: root.px(14)
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // ---------------- the clock ----------------
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 10
                        Text {
                            text: root.clock(run.partSecondsLeft)
                            color: run.partSecondsLeft === 0 ? root.gold : root.ink
                            font.pixelSize: root.px(34)
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
                            text: root.passText() !== "" ? root.passText()
                                  : (run.block.tempo || 0) > 0 ? qsTrc("starscore", "Today ♩ = %1").arg(run.block.tempo)
                                    + ((run.block.tempoGoal || 0) > (run.block.tempo || 0) ? qsTrc("starscore", ", up to %1").arg(run.block.tempoGoal) : "")
                                  : ""
                            color: root.passText() !== "" ? root.teal : root.soft
                            font.pixelSize: root.px(14)
                        }
                    }

                    // ---------------- the parts ----------------
                    Repeater {
                        model: run.parts
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true
                            implicitHeight: partRow.implicitHeight + 10
                            radius: 3
                            color: modelData.current ? "#2A241C" : "transparent"
                            border.color: modelData.current ? root.gold : root.line
                            border.width: 1
                            RowLayout {
                                id: partRow
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.verticalCenter: parent.verticalCenter
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                spacing: 8
                                Text {
                                    text: modelData.finished ? "✓" : (modelData.index + 1) + "."
                                    color: modelData.finished ? root.doneColor : root.gold
                                    font.pixelSize: root.px(14)
                                    Layout.preferredWidth: root.px(20)
                                }
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    color: root.ink
                                    font.pixelSize: root.px(14)
                                    elide: Text.ElideRight
                                    MouseArea { anchors.fill: parent; onClicked: run.goPart(modelData.index) }
                                }
                                Text {
                                    text: modelData.time
                                    color: root.soft
                                    font.pixelSize: root.px(13)
                                    font.family: "Menlo"
                                }
                                FlatButton {
                                    visible: modelData.hasNotation
                                    text: qsTrc("starscore", "Play example")
                                    minWidth: 100
                                    toolTipTitle: qsTrc("starscore", "Hear it as written, at today's tempo")
                                    onClicked: run.playExample(modelData.index)
                                }
                                FlatButton {
                                    visible: modelData.hasNotation
                                    text: qsTrc("starscore", "Play along")
                                    accentButton: true
                                    minWidth: 100
                                    toolTipTitle: qsTrc("starscore", "The backing for you to play over, and the timer")
                                    toolTipDescription: qsTrc("starscore", "It loops for the part's minutes and steps up from today's tempo toward the goal.")
                                    onClicked: run.playAlong(modelData.index)
                                }
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        FlatButton {
                            visible: run.playingAlong
                            text: qsTrc("starscore", "Stop the music")
                            minWidth: 110
                            onClicked: run.stopPlaying()
                        }
                        Item { Layout.fillWidth: true }
                        FlatButton {
                            visible: run.block.hasExercise === true
                            text: qsTrc("starscore", "Open in StarScore")
                            minWidth: 110
                            onClicked: run.openExercise(run.block.tempo || 0, false)
                        }
                        FlatButton {
                            text: qsTrc("starscore", "Open the song")
                            minWidth: 100
                            onClicked: run.openSong()
                        }
                        FlatButton {
                            visible: run.block.hasIreal === true
                            text: qsTrc("starscore", "iReal Pro")
                            minWidth: 70
                            toolTipTitle: qsTrc("starscore", "The chart as an iReal Pro link")
                            onClicked: run.openIreal()
                        }
                    }

                    Text {
                        visible: run.message !== ""
                        text: run.message
                        color: root.teal
                        font.pixelSize: root.px(13)
                    }
                    RowLayout {
                        visible: run.blockCount > 0 && run.doneCount === run.blockCount
                        spacing: 8
                        Text {
                            text: qsTrc("starscore", "All done. Keep going? Add")
                            color: root.ink
                            font.pixelSize: root.px(14)
                        }
                        Repeater {
                            model: [15, 30, 45, 60]
                            delegate: FlatButton {
                                required property int modelData
                                text: qsTrc("starscore", "%1 min").arg(modelData)
                                minWidth: 64
                                onClicked: run.addMinutes(modelData)
                            }
                        }
                    }

                    // ---------------- the notation (the current part) ----------------
                    Text {
                        visible: run.rendering
                        text: qsTrc("starscore", "Engraving the exercise…")
                        color: root.soft
                        font.pixelSize: root.px(12)
                    }
                    Repeater {
                        model: run.images
                        delegate: Image {
                            required property string modelData
                            // a full system fills the width; a shorter exercise keeps the same size of notation
                            Layout.preferredWidth: run.systemWidth > 0 && implicitWidth > 0
                                                   ? Math.min(body.width, implicitWidth * body.width / run.systemWidth) : body.width
                            Layout.preferredHeight: implicitWidth > 0 ? Layout.preferredWidth * implicitHeight / implicitWidth : 0
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
                            text: "<b>" + qsTrc("starscore", "MOVE ON WHEN") + "</b>&nbsp;&nbsp;" + (run.block.moveOnWhenHtml || "")
                            textFormat: Text.StyledText
                            color: root.gold
                            font.pixelSize: root.px(14)
                            wrapMode: Text.WordWrap
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: (run.block.sessions || 0) > 0
                        text: qsTrc("starscore", "So far: %1 sessions, %2%3")
                              .arg(run.block.sessions).arg(run.block.stageName)
                              .arg((run.block.bestTempo || 0) > 0 ? qsTrc("starscore", ", best ♩ = %1").arg(run.block.bestTempo) : "")
                              + ((run.block.lastNote || "") !== "" ? qsTrc("starscore", ". Last note: %1").arg(run.block.lastNote) : "")
                        color: root.teal
                        font.pixelSize: root.px(12)
                        wrapMode: Text.WordWrap
                    }
                    Ratings { Layout.fillWidth: true }
                }
            }

            Rectangle { Layout.fillWidth: true; height: 1; color: root.gold; opacity: 0.6 }

            // ---------------- done ----------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: run.blockCount > 0
                Text {
                    text: qsTrc("starscore", "Tempo reached")
                    color: root.soft
                    font.pixelSize: root.px(13)
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
