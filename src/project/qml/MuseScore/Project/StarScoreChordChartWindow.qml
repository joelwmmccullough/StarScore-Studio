/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — the chord chart editor (1.30.0, Joel: "an ireal-adjacent interface into starscore so I can directly
 * edit and adjust the chord charts that starscore exports").
 *
 * The song's iReal Pro chart laid out as iReal Pro lays it out, 16 cells to a line, on a page. Click a cell (or move
 * with the arrow keys) and change it on the right: the chord, the bar lines, section, time signature, ending, texts,
 * segno, coda, fermata. Typing a chord letter starts the chord; Enter keeps it and moves on. Every change is kept in
 * the song at once, and Export and Organize then writes this chart.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: chart.title !== "" ? qsTrc("starscore", "Chord chart: %1").arg(chart.title) : qsTrc("starscore", "Chord chart")

    contentWidth: 1180
    contentHeight: 780
    resizable: true
    modal: false
    closeOnEscape: false

    onClosed: chart.saveWindowSize(root.width, root.height)

    ChordChartEditorModel {
        id: chart
    }

    Component.onCompleted: {
        chart.load()
        root.draft = chart.current.typed !== undefined ? chart.current.typed : ""
        root.altDraft = chart.current.altTyped !== undefined ? chart.current.altTyped : ""
        grid.forceActiveFocus()
    }

    // what the chord field holds while it is typed in
    property string draft: ""
    property string altDraft: ""
    readonly property var cur: chart.current
    readonly property var draftPreview: chart.preview(root.draft)

    readonly property color ink: "#1b1b1b"
    readonly property color paper: "#fdfcf8"
    readonly property color faint: "#8a8a8a"

    Connections {
        target: chart
        function onSelectedChanged() {
            root.draft = chart.current.typed !== undefined ? chart.current.typed : ""
            root.altDraft = chart.current.altTyped !== undefined ? chart.current.altTyped : ""
        }
        function onCellsChanged() {
            if (!chordField.activeFocus) {
                root.draft = chart.current.typed !== undefined ? chart.current.typed : ""
            }
            if (!altField.activeFocus) {
                root.altDraft = chart.current.altTyped !== undefined ? chart.current.altTyped : ""
            }
        }
    }

    function moveTo(index) {
        chart.select(index)
        flick.showRow(Math.floor(chart.selected / 16))
    }

    function keepChord(moveOn) {
        if (chart.setChord(chart.selected, root.draft)) {
            if (moveOn) {
                root.moveTo(chart.selected + 1)
            }
            grid.forceActiveFocus()
            return true
        }
        return false
    }

    // typing a chord letter on the chart starts the chord
    function startChord(letter) {
        root.draft = letter
        chordField.ensureActiveFocus()
        Qt.callLater(function() {
            chordField.inputField.deselect()
            chordField.inputField.cursorPosition = chordField.inputField.text.length
        })
    }

    // iReal Pro's key ("Eb-", "F#") as it reads ("E♭ minor", "F♯")
    function keyText(k) {
        var minor = k.endsWith("-")
        var r = minor ? k.slice(0, -1) : k
        r = r.replace("b", "♭").replace("#", "♯")
        return minor ? qsTrc("starscore", "%1 minor").arg(r) : r
    }

    function times(n) {
        return n > 0 ? n + "×" : ""
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // what can't be charted
        StyledTextLabel {
            visible: !chart.possible
            Layout.fillWidth: true
            Layout.fillHeight: true
            wrapMode: Text.WordWrap
            text: chart.why !== "" ? chart.why
                                   : qsTrc("starscore", "This score has no chord symbols to make an iReal Pro chart from.")
        }

        // the top bar
        RowLayout {
            visible: chart.possible
            Layout.fillWidth: true
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    font: ui.theme.largeBodyBoldFont
                    text: chart.title
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    color: ui.theme.fontSecondaryColor
                    text: [chart.keyName !== "" ? qsTrc("starscore", "Key of %1").arg(root.keyText(chart.keyName)) : "",
                           chart.style,
                           chart.parts > 1 ? qsTrc("starscore", "%1 parts in iReal Pro").arg(chart.parts) : "",
                           chart.edited ? qsTrc("starscore", "Your version (kept in the song)")
                                        : qsTrc("starscore", "Made from the score's chords")]
                          .filter(function(s) { return s !== "" }).join("  ·  ")
                }
            }

            FlatButton {
                text: qsTrc("starscore", "Undo")
                enabled: chart.canUndo
                onClicked: chart.undo()
            }
            FlatButton {
                text: qsTrc("starscore", "Redo")
                enabled: chart.canRedo
                onClicked: chart.redo()
            }
            FlatButton {
                text: qsTrc("starscore", "Start again from the score")
                enabled: chart.edited
                toolTipTitle: qsTrc("starscore", "Start again from the score")
                toolTipDescription: qsTrc("starscore", "Throws away your version and makes the chart from the score's chord symbols again (Undo brings it back)")
                onClicked: chart.startAgain()
            }
            FlatButton {
                text: qsTrc("starscore", "Open in iReal Pro")
                accentButton: true
                toolTipTitle: qsTrc("starscore", "Open in iReal Pro")
                toolTipDescription: qsTrc("starscore", "Opens the chart's page; click its link to add the chart to iReal Pro")
                onClicked: chart.openInIReal()
            }
        }

        // the score's chords changed after the chart was edited
        Rectangle {
            visible: chart.possible && chart.stale
            Layout.fillWidth: true
            implicitHeight: staleText.implicitHeight + 12
            radius: 4
            color: "#33e0a030"
            border.color: "#e0a030"
            StyledTextLabel {
                id: staleText
                anchors.fill: parent
                anchors.margins: 6
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "The chord symbols in the score changed after you edited this chart. The export still writes your version. "
                            + "To pick up the score's changes, click Start again from the score.")
            }
        }

        RowLayout {
            visible: chart.possible
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12

            // the chart, on a page
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.paper
                border.color: ui.theme.strokeColor
                radius: 3

                StyledFlickable {
                    id: flick
                    anchors.fill: parent
                    anchors.margins: 14
                    clip: true
                    contentWidth: width
                    contentHeight: grid.height

                    function showRow(row) {
                        var top = row * grid.rowH
                        if (top < contentY) {
                            contentY = top
                        } else if (top + grid.rowH > contentY + height) {
                            contentY = Math.min(top + grid.rowH - height, Math.max(0, contentHeight - height))
                        }
                    }

                    Item {
                        id: grid
                        width: flick.width
                        readonly property real cellW: width / 16
                        readonly property real rowH: Math.max(64, Math.min(90, cellW * 1.25))
                        height: chart.rowCount * rowH + 8
                        focus: true
                        activeFocusOnTab: true

                        Keys.onShortcutOverride: function(event) {
                            // the score's shortcuts (A–G add notes!) don't fire while the chart has the keys
                            event.accepted = true
                        }
                        Keys.onPressed: function(event) {
                            var i = chart.selected
                            var handled = true
                            if (event.key === Qt.Key_Left) {
                                root.moveTo(i - 1)
                            } else if (event.key === Qt.Key_Right || event.key === Qt.Key_Tab) {
                                root.moveTo(i + 1)
                            } else if (event.key === Qt.Key_Up) {
                                root.moveTo(i - 16)
                            } else if (event.key === Qt.Key_Down) {
                                root.moveTo(i + 16)
                            } else if (event.key === Qt.Key_Home) {
                                root.moveTo(i - i % 16)
                            } else if (event.key === Qt.Key_End) {
                                root.moveTo(i - i % 16 + 15)
                            } else if (event.key === Qt.Key_Delete || event.key === Qt.Key_Backspace) {
                                chart.setContent(i, "")
                            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                root.startChord(root.draft)
                            } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Z) {
                                if (event.modifiers & Qt.ShiftModifier) {
                                    chart.redo()
                                } else {
                                    chart.undo()
                                }
                            } else if ((event.modifiers & Qt.ControlModifier) && event.key === Qt.Key_Y) {
                                chart.redo()
                            } else if (event.text === "%") {
                                chart.setContent(i, "x")
                                root.moveTo(i + 1)
                            } else if (event.text === "/") {
                                chart.setContent(i, "p")
                                root.moveTo(i + 1)
                            } else if (event.text === " ") {
                                root.moveTo(i + 1)
                            } else if (event.text.length === 1 && "ABCDEFGabcdefgN".indexOf(event.text) >= 0
                                       && !(event.modifiers & (Qt.ControlModifier | Qt.MetaModifier | Qt.AltModifier))) {
                                root.startChord(event.text.toUpperCase())
                            } else {
                                handled = false
                            }
                            event.accepted = handled
                        }

                        Repeater {
                            model: chart.cells

                            Item {
                                id: cell
                                readonly property var c: modelData
                                readonly property bool isSel: c.index === chart.selected
                                x: c.col * grid.cellW
                                y: c.row * grid.rowH
                                width: grid.cellW
                                height: grid.rowH

                                readonly property real topY: 18          // room above for section, ending, texts
                                readonly property real bottomY: height - 12

                                Rectangle {
                                    anchors.fill: parent
                                    anchors.margins: 1
                                    color: cell.isSel ? Qt.rgba(0.23, 0.51, 0.96, 0.18) : "transparent"
                                    border.color: cell.isSel ? "#3b82f6" : "transparent"
                                    border.width: 1.5
                                    radius: 2
                                }

                                // the ending's bracket over the bar
                                Rectangle {
                                    visible: cell.c.ending > 0
                                    x: 2; y: 3; width: cell.width * 2; height: 1.2; color: root.ink
                                }
                                Rectangle {
                                    visible: cell.c.ending > 0
                                    x: 2; y: 3; width: 1.2; height: 12; color: root.ink
                                }
                                Text {
                                    visible: cell.c.ending > 0
                                    x: 5; y: 4
                                    text: cell.c.ending + "."
                                    color: root.ink
                                    font.pixelSize: 10
                                    font.bold: true
                                }

                                // the section's box
                                Rectangle {
                                    visible: cell.c.mark !== ""
                                    x: -2; y: 0
                                    width: markText.implicitWidth + 6; height: 16
                                    color: root.ink
                                    radius: 2
                                    Text {
                                        id: markText
                                        anchors.centerIn: parent
                                        color: root.paper
                                        font.pixelSize: 11
                                        font.bold: true
                                        text: cell.c.mark === "i" ? "In" : cell.c.mark
                                    }
                                }

                                // text above (a section's word, "Solos")
                                Text {
                                    visible: cell.c.above !== ""
                                    x: cell.c.mark !== "" ? markText.implicitWidth + 8 : (cell.c.ending > 0 ? 18 : 2)
                                    y: 1
                                    text: cell.c.above
                                    color: root.ink
                                    font.pixelSize: 11
                                    font.italic: true
                                    font.bold: true
                                }

                                // segno, coda, fermata
                                Text {
                                    visible: cell.c.segno || cell.c.coda || cell.c.fermata
                                    anchors.right: parent.right
                                    anchors.rightMargin: 3
                                    y: 0
                                    text: (cell.c.segno ? "𝄋" : "") + (cell.c.coda ? "𝄌" : "") + (cell.c.fermata ? "𝄐" : "")
                                    color: root.ink
                                    font.pixelSize: 15
                                }

                                // the time signature
                                Column {
                                    visible: cell.c.ts !== ""
                                    x: cell.c.left === "" ? 2 : (cell.c.left === "|" ? 5 : (cell.c.left === "{" ? 13 : 9))
                                    y: cell.topY + (cell.bottomY - cell.topY - height) / 2
                                    spacing: -4
                                    Text { text: cell.c.ts.split("/")[0] || ""; color: root.ink; font.pixelSize: 13; font.bold: true }
                                    Text { text: cell.c.ts.split("/")[1] || ""; color: root.ink; font.pixelSize: 13; font.bold: true }
                                }

                                // the bar line before
                                Item {
                                    visible: cell.c.left !== ""
                                    x: 0
                                    y: cell.topY
                                    width: 10
                                    height: cell.bottomY - cell.topY
                                    Rectangle {   // thick (start of section, start repeat)
                                        visible: cell.c.left === "[" || cell.c.left === "{"
                                        x: 0; width: 3.5; height: parent.height; color: root.ink
                                    }
                                    Rectangle {
                                        x: cell.c.left === "|" ? 0 : 5
                                        width: 1.4; height: parent.height; color: root.ink
                                    }
                                    Column {
                                        visible: cell.c.left === "{"
                                        x: 8.5
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 6
                                        Rectangle { width: 3; height: 3; radius: 1.5; color: root.ink }
                                        Rectangle { width: 3; height: 3; radius: 1.5; color: root.ink }
                                    }
                                }

                                // the bar line after
                                Item {
                                    visible: cell.c.right !== ""
                                    x: cell.width - width
                                    y: cell.topY
                                    width: 12
                                    height: cell.bottomY - cell.topY
                                    Rectangle {   // thick (end of section, end repeat, end)
                                        visible: cell.c.right !== ""
                                        x: parent.width - 3.5; width: 3.5; height: parent.height; color: root.ink
                                    }
                                    Rectangle {
                                        x: parent.width - 8.5
                                        width: 1.4; height: parent.height; color: root.ink
                                    }
                                    Column {
                                        visible: cell.c.right === "}"
                                        x: parent.width - 13
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: 6
                                        Rectangle { width: 3; height: 3; radius: 1.5; color: root.ink }
                                        Rectangle { width: 3; height: 3; radius: 1.5; color: root.ink }
                                    }
                                }

                                // the alternate chord, small above
                                Text {
                                    visible: cell.c.alt !== ""
                                    x: 12
                                    y: cell.topY - 2
                                    text: "(" + cell.c.alt + ")"
                                    color: root.ink
                                    font.pixelSize: 10
                                }

                                // the chord
                                Row {
                                    id: chordRow
                                    readonly property real big: Math.max(13, Math.min(26, grid.cellW * 0.36)) * (cell.c.small ? 0.72 : 1)
                                    readonly property real lead: cell.c.ts !== "" ? (cell.c.left === "" || cell.c.left === "|" ? 17 : (cell.c.left === "{" ? 25 : 21))
                                                                 : (cell.c.left === "{" ? 12 : 7)
                                    x: lead
                                    width: cell.width - lead - 4
                                    y: cell.topY + (cell.bottomY - cell.topY - height) / 2 + 2
                                    clip: false
                                    spacing: 0

                                    Text {
                                        text: cell.c.isChord ? cell.c.root
                                              : cell.c.content === "x" ? "%"
                                              : cell.c.content === "r" ? "%%"
                                              : cell.c.content === "p" ? "/"
                                              : cell.c.content === "n" ? "N.C."
                                              : ""
                                        color: cell.c.isChord && !cell.c.known ? "#c0392b" : root.ink
                                        font.pixelSize: cell.c.content === "n" ? chordRow.big * 0.6 : chordRow.big
                                        font.bold: true
                                        font.family: "Helvetica Neue"
                                    }
                                    Text {
                                        visible: cell.c.isChord && cell.c.quality !== ""
                                        text: cell.c.quality
                                        color: cell.c.known ? root.ink : "#c0392b"
                                        font.pixelSize: chordRow.big * 0.58
                                        font.bold: true
                                        font.family: "Helvetica Neue"
                                    }
                                    Text {
                                        visible: cell.c.isChord && cell.c.bass !== ""
                                        anchors.bottom: parent.bottom
                                        text: "/" + cell.c.bass
                                        color: root.ink
                                        font.pixelSize: chordRow.big * 0.62
                                        font.bold: true
                                        font.family: "Helvetica Neue"
                                    }
                                }

                                // text below, repeat count
                                Text {
                                    visible: cell.c.below !== "" || cell.c.times > 0
                                    x: 6
                                    y: cell.bottomY - 2
                                    text: [cell.c.below, root.times(cell.c.times)].filter(function(s) { return s !== "" }).join("  ")
                                    color: root.ink
                                    font.pixelSize: 10
                                    font.italic: true
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    onClicked: {
                                        chart.select(cell.c.index)
                                        grid.forceActiveFocus()
                                    }
                                    onDoubleClicked: {
                                        chart.select(cell.c.index)
                                        root.startChord(root.draft)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // what the selected cell has
            StyledFlickable {
                id: inspectorFlick
                Layout.preferredWidth: 300
                Layout.maximumWidth: 300
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: inspector.implicitHeight
                clip: true

                ColumnLayout {
                    id: inspector
                    width: inspectorFlick.width - 10
                    spacing: 8

                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        font: ui.theme.bodyBoldFont
                        text: qsTrc("starscore", "Line %1, cell %2").arg((root.cur.row || 0) + 1).arg((root.cur.col || 0) + 1)
                    }

                    StyledTextLabel { text: qsTrc("starscore", "Chord"); font: ui.theme.bodyBoldFont }
                    TextInputField {
                        id: chordField
                        Layout.fillWidth: true
                        hint: qsTrc("starscore", "Ebm7, F7b9, Bbmaj7, Fm7b5, Eb7/Bb, N.C.")
                        currentText: root.draft
                        onTextEdited: function(t) { root.draft = t }
                        onAccepted: root.keepChord(true)
                        onEscaped: {
                            root.draft = root.cur.typed !== undefined ? root.cur.typed : ""
                            grid.forceActiveFocus()
                        }
                        onTextEditingFinished: function(t) {
                            if (t !== (root.cur.typed || "")) {
                                chart.setChord(chart.selected, t)
                            }
                        }
                    }
                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        color: !root.draftPreview.ok || (root.draftPreview.ireal !== "" && !root.draftPreview.known)
                               ? "#c0392b" : ui.theme.fontSecondaryColor
                        text: chart.message !== "" ? chart.message
                              : root.draft.trim() === "" ? qsTrc("starscore", "Type a chord, then Enter (it moves to the next cell).")
                              : !root.draftPreview.ok ? qsTrc("starscore", "Not a chord StarScore can read")
                              : qsTrc("starscore", "Reads %1 · iReal Pro gets “%2”%3").arg(root.draftPreview.display)
                                .arg(root.draftPreview.ireal)
                                .arg(root.draftPreview.known ? "" : qsTrc("starscore", " (a quality iReal Pro may not know)"))
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        Repeater {
                            model: [
                                { "text": qsTrc("starscore", "Empty"), "value": "" },
                                { "text": qsTrc("starscore", "% bar"), "value": "x" },
                                { "text": qsTrc("starscore", "%% 2 bars"), "value": "r" },
                                { "text": qsTrc("starscore", "/ slash"), "value": "p" },
                                { "text": qsTrc("starscore", "N.C."), "value": "n" }
                            ]
                            FlatButton {
                                text: modelData.text
                                accentButton: root.cur.content === modelData.value && modelData.value !== ""
                                onClicked: {
                                    chart.setContent(chart.selected, modelData.value)
                                    grid.forceActiveFocus()
                                }
                            }
                        }
                    }

                    CheckBox {
                        text: qsTrc("starscore", "Small chord")
                        checked: root.cur.small === true
                        enabled: root.cur.content !== undefined && root.cur.content !== ""
                        onClicked: chart.setSmall(chart.selected, !checked)
                    }

                    StyledTextLabel { text: qsTrc("starscore", "Alternate chord (small, above)"); font: ui.theme.bodyBoldFont }
                    TextInputField {
                        id: altField
                        Layout.fillWidth: true
                        hint: qsTrc("starscore", "none")
                        currentText: root.altDraft
                        onTextEdited: function(t) { root.altDraft = t }
                        onAccepted: { chart.setAlt(chart.selected, root.altDraft); grid.forceActiveFocus() }
                        onTextEditingFinished: function(t) {
                            if (t !== (root.cur.altTyped || "")) {
                                chart.setAlt(chart.selected, t)
                            }
                        }
                    }

                    SeparatorLine { Layout.fillWidth: true }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 6

                        StyledTextLabel { text: qsTrc("starscore", "Bar line before"); horizontalAlignment: Text.AlignLeft }
                        StyledDropdown {
                            Layout.fillWidth: true
                            model: [
                                { "text": qsTrc("starscore", "None"), "value": "" },
                                { "text": qsTrc("starscore", "| Bar"), "value": "|" },
                                { "text": qsTrc("starscore", "[ Double (section starts)"), "value": "[" },
                                { "text": qsTrc("starscore", "{ Repeat starts"), "value": "{" }
                            ]
                            currentIndex: indexOfValue(root.cur.left !== undefined ? root.cur.left : "")
                            onActivated: function(index, value) { chart.setLeft(chart.selected, value) }
                        }

                        StyledTextLabel { text: qsTrc("starscore", "Bar line after"); horizontalAlignment: Text.AlignLeft }
                        StyledDropdown {
                            Layout.fillWidth: true
                            model: [
                                { "text": qsTrc("starscore", "None"), "value": "" },
                                { "text": qsTrc("starscore", "] Double (section ends)"), "value": "]" },
                                { "text": qsTrc("starscore", "} Repeat ends"), "value": "}" },
                                { "text": qsTrc("starscore", "Z Final"), "value": "Z" }
                            ]
                            currentIndex: indexOfValue(root.cur.right !== undefined ? root.cur.right : "")
                            onActivated: function(index, value) { chart.setRight(chart.selected, value) }
                        }

                        StyledTextLabel { text: qsTrc("starscore", "Section"); horizontalAlignment: Text.AlignLeft }
                        StyledDropdown {
                            Layout.fillWidth: true
                            model: [
                                { "text": qsTrc("starscore", "None"), "value": "" },
                                { "text": "A", "value": "A" },
                                { "text": "B", "value": "B" },
                                { "text": "C", "value": "C" },
                                { "text": "D", "value": "D" },
                                { "text": qsTrc("starscore", "Verse (V)"), "value": "V" },
                                { "text": qsTrc("starscore", "Intro (In)"), "value": "i" }
                            ]
                            currentIndex: indexOfValue(root.cur.mark !== undefined ? root.cur.mark : "")
                            onActivated: function(index, value) { chart.setMark(chart.selected, value) }
                        }

                        StyledTextLabel { text: qsTrc("starscore", "Time signature"); horizontalAlignment: Text.AlignLeft }
                        StyledDropdown {
                            Layout.fillWidth: true
                            model: [
                                { "text": qsTrc("starscore", "No change"), "value": "" },
                                { "text": "4/4", "value": "T44" }, { "text": "3/4", "value": "T34" },
                                { "text": "2/4", "value": "T24" }, { "text": "5/4", "value": "T54" },
                                { "text": "6/4", "value": "T64" }, { "text": "7/4", "value": "T74" },
                                { "text": "2/2", "value": "T22" }, { "text": "3/8", "value": "T38" },
                                { "text": "5/8", "value": "T58" }, { "text": "6/8", "value": "T68" },
                                { "text": "7/8", "value": "T78" }, { "text": "9/8", "value": "T98" },
                                { "text": "12/8", "value": "T12" }
                            ]
                            currentIndex: indexOfValue(root.cur.tsCode !== undefined ? root.cur.tsCode : "")
                            onActivated: function(index, value) { chart.setTimeSig(chart.selected, value) }
                        }

                        StyledTextLabel { text: qsTrc("starscore", "Ending"); horizontalAlignment: Text.AlignLeft }
                        StyledDropdown {
                            Layout.fillWidth: true
                            model: [
                                { "text": qsTrc("starscore", "None"), "value": 0 },
                                { "text": qsTrc("starscore", "1st ending"), "value": 1 },
                                { "text": qsTrc("starscore", "2nd ending"), "value": 2 },
                                { "text": qsTrc("starscore", "3rd ending"), "value": 3 }
                            ]
                            currentIndex: indexOfValue(root.cur.ending !== undefined ? root.cur.ending : 0)
                            onActivated: function(index, value) { chart.setEnding(chart.selected, value) }
                        }

                        StyledTextLabel { text: qsTrc("starscore", "Play the repeat"); horizontalAlignment: Text.AlignLeft }
                        StyledDropdown {
                            Layout.fillWidth: true
                            model: [
                                { "text": qsTrc("starscore", "Twice (no count)"), "value": 0 },
                                { "text": "3×", "value": 3 }, { "text": "4×", "value": 4 }, { "text": "5×", "value": 5 },
                                { "text": "6×", "value": 6 }, { "text": "7×", "value": 7 }, { "text": "8×", "value": 8 }
                            ]
                            currentIndex: indexOfValue(root.cur.times !== undefined ? root.cur.times : 0)
                            onActivated: function(index, value) { chart.setRepeatCount(chart.selected, value) }
                        }
                    }

                    StyledTextLabel { text: qsTrc("starscore", "Text above the bar"); font: ui.theme.bodyBoldFont }
                    TextInputField {
                        Layout.fillWidth: true
                        hint: qsTrc("starscore", "e.g. Solos, Bridge, Vamp")
                        currentText: root.cur.above !== undefined ? root.cur.above : ""
                        onTextEditingFinished: function(t) {
                            if (t !== (root.cur.above || "")) {
                                chart.setTextAbove(chart.selected, t)
                            }
                        }
                        onAccepted: grid.forceActiveFocus()
                    }

                    StyledTextLabel { text: qsTrc("starscore", "Text below"); font: ui.theme.bodyBoldFont }
                    TextInputField {
                        Layout.fillWidth: true
                        hint: qsTrc("starscore", "e.g. D.C. al Coda, Fine, 161 bpm")
                        currentText: root.cur.below !== undefined ? root.cur.below : ""
                        onTextEditingFinished: function(t) {
                            if (t !== (root.cur.below || "")) {
                                chart.setTextBelow(chart.selected, t)
                            }
                        }
                        onAccepted: grid.forceActiveFocus()
                    }

                    Row {
                        spacing: 12
                        CheckBox {
                            text: qsTrc("starscore", "Segno")
                            checked: root.cur.segno === true
                            onClicked: chart.setSymbol(chart.selected, "S", !checked)
                        }
                        CheckBox {
                            text: qsTrc("starscore", "Coda")
                            checked: root.cur.coda === true
                            onClicked: chart.setSymbol(chart.selected, "Q", !checked)
                        }
                        CheckBox {
                            text: qsTrc("starscore", "Fermata")
                            checked: root.cur.fermata === true
                            onClicked: chart.setSymbol(chart.selected, "f", !checked)
                        }
                    }

                    SeparatorLine { Layout.fillWidth: true }

                    StyledTextLabel {
                        text: qsTrc("starscore", "Cells (what comes after moves along)")
                        font: ui.theme.bodyBoldFont
                    }
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 4
                        rowSpacing: 4
                        FlatButton {
                            Layout.fillWidth: true
                            text: qsTrc("starscore", "Insert a cell")
                            onClicked: chart.insertCells(chart.selected, 1)
                        }
                        FlatButton {
                            Layout.fillWidth: true
                            text: qsTrc("starscore", "Delete this cell")
                            onClicked: chart.deleteCells(chart.selected, 1)
                        }
                        FlatButton {
                            Layout.fillWidth: true
                            text: qsTrc("starscore", "Insert 4 cells")
                            onClicked: chart.insertCells(chart.selected, 4)
                        }
                        FlatButton {
                            Layout.fillWidth: true
                            text: qsTrc("starscore", "Delete 4 cells")
                            onClicked: chart.deleteCells(chart.selected, 4)
                        }
                        FlatButton {
                            Layout.fillWidth: true
                            text: qsTrc("starscore", "Insert a line above")
                            onClicked: chart.insertLine(Math.floor(chart.selected / 16))
                        }
                        FlatButton {
                            Layout.fillWidth: true
                            text: qsTrc("starscore", "Delete this line")
                            enabled: chart.rowCount > 1
                            onClicked: chart.deleteLine(Math.floor(chart.selected / 16))
                        }
                    }

                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.WordWrap
                        color: ui.theme.fontSecondaryColor
                        text: qsTrc("starscore", "Keys on the chart: arrows move · a letter (A–G) starts a chord · Enter edits the chord · "
                                    + "% repeats the bar · / is a slash · Delete empties the cell · Space skips · ⌘Z undoes. "
                                    + "Every change is kept in the song; Export and Organize writes this chart.")
                    }
                }
            }
        }
    }
}
