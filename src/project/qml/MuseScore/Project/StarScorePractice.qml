/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Practice (Joel, 22 Oct 2026). The setlist and what each song needs, a routine (one
 * session) or a plan (every day up to a show), the plan's days, the practice book PDF, iReal Pro, and the runner.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

Rectangle {
    id: root

    color: ui.theme.backgroundSecondaryColor

    property bool editingSetlist: false
    property int openDay: -1

    readonly property color goldColor: "#CFA85E"
    readonly property color doneColor: "#3FB05A"
    readonly property color missingColor: "#E0463A"
    readonly property var areaNames: ({ "parts": qsTrc("starscore", "Parts"), "comping": qsTrc("starscore", "Comping"),
                                        "harmony": qsTrc("starscore", "Harmony"), "solos": qsTrc("starscore", "Solos") })

    PracticeModel {
        id: pm
        onPlansChanged: {
            if (pm.plan && pm.plan.todayIndex !== undefined && root.openDay < 0) {
                root.openDay = pm.plan.todayIndex
            }
        }
    }

    Component.onCompleted: pm.load()

    function minutesText(m) {
        if (m < 60) {
            return qsTrc("starscore", "%1 min").arg(m)
        }
        var h = Math.floor(m / 60)
        var r = m % 60
        return r ? qsTrc("starscore", "%1 h %2 min").arg(h).arg(r) : qsTrc("starscore", "%1 h").arg(h)
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 20

        // ---------------- Setlist ----------------
        Rectangle {
            Layout.preferredWidth: 360
            Layout.fillHeight: true
            radius: 6
            color: ui.theme.backgroundPrimaryColor
            border.color: ui.theme.strokeColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    StyledTextLabel {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignLeft
                        text: qsTrc("starscore", "Setlist")
                        font: ui.theme.headerBoldFont
                    }
                    FlatButton {
                        text: root.editingSetlist ? qsTrc("starscore", "Done") : qsTrc("starscore", "Edit")
                        onClicked: {
                            if (root.editingSetlist) {
                                pm.setSetlistText(setlistArea.text)
                            }
                            root.editingSetlist = !root.editingSetlist
                        }
                    }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    visible: !root.editingSetlist
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignLeft
                    opacity: 0.7
                    text: qsTrc("starscore", "How much each song needs, from 0 (nothing) to 3 (a lot). Click a number to change it. Plans give more time to the songs and areas that need more.")
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: root.editingSetlist
                    color: ui.theme.textFieldColor
                    border.color: ui.theme.strokeColor
                    radius: 3
                    ScrollView {
                        anchors.fill: parent
                        anchors.margins: 6
                        TextArea {
                            id: setlistArea
                            text: pm.setlistText
                            color: ui.theme.fontPrimaryColor
                            font: ui.theme.bodyFont
                            wrapMode: TextEdit.NoWrap
                            placeholderText: qsTrc("starscore", "One song per line. A line like “Set 2” starts a set.")
                        }
                    }
                }

                StyledFlickable {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: !root.editingSetlist
                    contentHeight: setColumn.implicitHeight
                    clip: true

                    ColumnLayout {
                        id: setColumn
                        width: parent.width
                        spacing: 2

                        Repeater {
                            model: pm.setlist
                            delegate: ColumnLayout {
                                id: songDelegate
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                spacing: 2

                                StyledTextLabel {
                                    visible: modelData.set > 0 && (index === 0 || pm.setlist[index - 1].set !== modelData.set)
                                    Layout.topMargin: index === 0 ? 0 : 8
                                    text: qsTrc("starscore", "Set %1").arg(modelData.set)
                                    font: ui.theme.bodyBoldFont
                                    color: root.goldColor
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 4

                                    StyledTextLabel {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignLeft
                                        elide: Text.ElideRight
                                        text: modelData.title
                                        color: modelData.found ? ui.theme.fontPrimaryColor : root.missingColor
                                    }
                                    Repeater {
                                        model: modelData.found ? ["parts", "comping", "harmony", "solos"] : []
                                        delegate: FlatButton {
                                            required property string modelData
                                            readonly property var song: songDelegate.modelData
                                            minWidth: 30
                                            Layout.preferredWidth: 30
                                            Layout.preferredHeight: 24
                                            text: song && song.needs ? String(song.needs[modelData]) : ""
                                            toolTipTitle: root.areaNames[modelData]
                                            toolTipDescription: qsTrc("starscore", "How much %1 needs (0–3). Click to change.").arg(song ? song.title : "")
                                            onClicked: pm.setNeed(song.code, modelData, (song.needs[modelData] + 1) % 4)
                                        }
                                    }
                                    StyledTextLabel {
                                        visible: !modelData.found
                                        text: qsTrc("starscore", "no practice items")
                                        opacity: 0.6
                                    }
                                }
                            }
                        }
                    }
                }

                StyledTextLabel {
                    Layout.fillWidth: true
                    visible: !root.editingSetlist
                    horizontalAlignment: Text.AlignLeft
                    opacity: 0.6
                    text: qsTrc("starscore", "Columns: parts, comping, harmony, solos")
                }
            }
        }

        // ---------------- Make ----------------
        Rectangle {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            radius: 6
            color: ui.theme.backgroundPrimaryColor
            border.color: ui.theme.strokeColor

            StyledFlickable {
                anchors.fill: parent
                anchors.margins: 12
                contentHeight: makeColumn.implicitHeight
                clip: true

                ColumnLayout {
                    id: makeColumn
                    width: parent.width
                    spacing: 8

                    StyledTextLabel {
                        text: qsTrc("starscore", "One session")
                        font: ui.theme.headerBoldFont
                    }
                    RowLayout {
                        spacing: 8
                        StyledTextLabel { text: qsTrc("starscore", "Minutes") }
                        IncrementalPropertyControl {
                            Layout.preferredWidth: 100
                            currentValue: pm.routineMinutes
                            step: 15; decimals: 0; minValue: 15; maxValue: 600
                            onValueEdited: function(v) { pm.setRoutineMinutes(v) }
                        }
                    }
                    FlatButton {
                        Layout.fillWidth: true
                        text: qsTrc("starscore", "Make a routine")
                        accentButton: true
                        enabled: !pm.busy
                        onClicked: { root.openDay = 0; pm.makeRoutine() }
                    }

                    SeparatorLine { Layout.topMargin: 8; Layout.bottomMargin: 8 }

                    StyledTextLabel {
                        text: qsTrc("starscore", "Every day up to a show")
                        font: ui.theme.headerBoldFont
                    }
                    RowLayout {
                        spacing: 8
                        StyledTextLabel { text: qsTrc("starscore", "Show date") }
                        TextInputField {
                            Layout.preferredWidth: 130
                            currentText: pm.showDate
                            hint: "2026-10-31"
                            onTextEditingFinished: function(t) { pm.setShowDate(t) }
                        }
                    }
                    RowLayout {
                        spacing: 8
                        StyledTextLabel { text: qsTrc("starscore", "Minutes a day") }
                        IncrementalPropertyControl {
                            Layout.preferredWidth: 100
                            currentValue: pm.defaultMinutes
                            step: 15; decimals: 0; minValue: 15; maxValue: 600
                            onValueEdited: function(v) { pm.setDefaultMinutes(v) }
                        }
                    }
                    StyledTextLabel {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignLeft
                        opacity: 0.7
                        text: qsTrc("starscore", "Change any day's minutes below (0 for a day off). The day before the show is a run-through day.")
                    }
                    Repeater {
                        model: pm.days
                        delegate: RowLayout {
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 8
                            StyledTextLabel {
                                Layout.preferredWidth: 110
                                horizontalAlignment: Text.AlignLeft
                                text: modelData.label
                                font: modelData.isShow ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                                color: modelData.isShow ? root.goldColor : ui.theme.fontPrimaryColor
                            }
                            IncrementalPropertyControl {
                                Layout.preferredWidth: 100
                                currentValue: modelData.minutes
                                step: 15; decimals: 0; minValue: 0; maxValue: 600
                                onValueEdited: function(v) { pm.setDayMinutes(modelData.date, v) }
                            }
                            StyledTextLabel {
                                visible: modelData.isShow
                                text: qsTrc("starscore", "show")
                                opacity: 0.7
                            }
                        }
                    }
                    StyledTextLabel {
                        text: qsTrc("starscore", "In all: %1").arg(root.minutesText(pm.totalMinutes))
                        opacity: 0.7
                    }
                    FlatButton {
                        Layout.fillWidth: true
                        text: qsTrc("starscore", "Make a plan")
                        accentButton: true
                        enabled: !pm.busy && pm.days.length > 0
                        onClicked: { root.openDay = 0; pm.makePlan() }
                    }
                }
            }
        }

        // ---------------- Plans ----------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                StyledTextLabel {
                    text: qsTrc("starscore", "Practice")
                    font: ui.theme.headerBoldFont
                }
                StyledDropdown {
                    Layout.preferredWidth: 320
                    visible: pm.plans.length > 0
                    model: pm.plans.map(function(p) { return { "text": p.name + " — " + p.subtitle, "value": p.id } })
                    currentIndex: pm.plans.findIndex(function(p) { return p.id === pm.planId })
                    onActivated: function(index, value) { root.openDay = -1; pm.selectPlan(value) }
                }
                Item { Layout.fillWidth: true }
                FlatButton {
                    text: qsTrc("starscore", "Read songs again")
                    enabled: !pm.busy
                    onClicked: pm.rescan()
                }
            }

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                text: pm.status
                opacity: 0.8
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: pm.plan.id !== undefined
                FlatButton {
                    text: qsTrc("starscore", "Practice today")
                    accentButton: true
                    enabled: pm.plan.todayIndex !== undefined && pm.plan.todayIndex >= 0
                    onClicked: pm.openRunner(pm.plan.todayIndex, 0)
                }
                FlatButton {
                    text: qsTrc("starscore", "Practice book (PDF)")
                    enabled: !pm.busy
                    onClicked: pm.makeBook(-1)
                }
                FlatButton {
                    text: qsTrc("starscore", "iReal Pro charts")
                    onClicked: pm.exportIreal(-1)
                }
                FlatButton {
                    text: qsTrc("starscore", "Plan again from today")
                    toolTipTitle: qsTrc("starscore", "Plan again from today")
                    toolTipDescription: qsTrc("starscore", "Plans the days from today on again, from what you've done so far and how solid it is.")
                    enabled: !pm.busy
                    onClicked: pm.rebuildPlan()
                }
                Item { Layout.fillWidth: true }
                FlatButton {
                    text: qsTrc("starscore", "Folder")
                    onClicked: pm.openOutputFolder()
                }
                FlatButton {
                    text: qsTrc("starscore", "Delete")
                    onClicked: pm.deletePlan(pm.planId)
                }
            }

            StyledTextLabel {
                Layout.fillWidth: true
                visible: pm.plan.id !== undefined
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "%1 of %2 done").arg(pm.plan.done || 0).arg(pm.plan.total || 0)
                opacity: 0.7
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 6
                color: ui.theme.backgroundPrimaryColor
                border.color: ui.theme.strokeColor

                StyledTextLabel {
                    anchors.centerIn: parent
                    width: parent.width - 60
                    visible: pm.plan.id === undefined
                    wrapMode: Text.WordWrap
                    text: pm.busy ? "" : qsTrc("starscore", "Make a routine for one session, or a plan for every day up to your show.")
                    opacity: 0.7
                }

                StyledFlickable {
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: pm.plan.id !== undefined
                    contentHeight: dayColumn.implicitHeight
                    clip: true

                    ColumnLayout {
                        id: dayColumn
                        width: parent.width
                        spacing: 6

                        Repeater {
                            model: pm.plan.days || []
                            delegate: ColumnLayout {
                                id: dayDelegate
                                required property var modelData
                                Layout.fillWidth: true
                                spacing: 2
                                readonly property bool expanded: root.openDay === modelData.index

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    FlatButton {
                                        Layout.preferredWidth: 28
                                        icon: dayDelegate.expanded ? IconCode.SMALL_ARROW_DOWN : IconCode.SMALL_ARROW_RIGHT
                                        transparent: true
                                        onClicked: root.openDay = dayDelegate.expanded ? -1 : dayDelegate.modelData.index
                                    }
                                    StyledTextLabel {
                                        Layout.fillWidth: true
                                        horizontalAlignment: Text.AlignLeft
                                        text: dayDelegate.modelData.title + (dayDelegate.modelData.label ? " · " + dayDelegate.modelData.label : "")
                                        font: ui.theme.bodyBoldFont
                                        color: dayDelegate.modelData.isToday ? root.goldColor : ui.theme.fontPrimaryColor
                                        opacity: dayDelegate.modelData.isPast ? 0.6 : 1
                                    }
                                    StyledTextLabel {
                                        text: qsTrc("starscore", "%1 · %2 of %3 done").arg(dayDelegate.modelData.plannedText)
                                              .arg(dayDelegate.modelData.done).arg(dayDelegate.modelData.total)
                                        opacity: 0.7
                                        color: dayDelegate.modelData.total > 0 && dayDelegate.modelData.done === dayDelegate.modelData.total
                                               ? root.doneColor : ui.theme.fontPrimaryColor
                                    }
                                    FlatButton {
                                        minWidth: 70
                                        text: qsTrc("starscore", "Practice")
                                        enabled: dayDelegate.modelData.total > 0
                                        onClicked: pm.openRunner(dayDelegate.modelData.index, 0)
                                    }
                                    FlatButton {
                                        minWidth: 50
                                        text: qsTrc("starscore", "PDF")
                                        enabled: !pm.busy && dayDelegate.modelData.total > 0
                                        onClicked: pm.makeBook(dayDelegate.modelData.index)
                                    }
                                    FlatButton {
                                        minWidth: 50
                                        text: qsTrc("starscore", "iReal")
                                        enabled: dayDelegate.modelData.total > 0
                                        onClicked: pm.exportIreal(dayDelegate.modelData.index)
                                    }
                                }

                                Repeater {
                                    model: dayDelegate.expanded ? dayDelegate.modelData.blocks : []
                                    delegate: Rectangle {
                                        id: blockDelegate
                                        required property var modelData
                                        Layout.fillWidth: true
                                        Layout.leftMargin: 36
                                        implicitHeight: blockRow.implicitHeight + 10
                                        radius: 3
                                        color: blockMouse.containsMouse ? ui.theme.buttonColor : "transparent"

                                        MouseArea {
                                            id: blockMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            onClicked: pm.openRunner(dayDelegate.modelData.index, blockDelegate.modelData.index)
                                        }

                                        RowLayout {
                                            id: blockRow
                                            anchors.left: parent.left
                                            anchors.right: parent.right
                                            anchors.verticalCenter: parent.verticalCenter
                                            anchors.leftMargin: 6
                                            anchors.rightMargin: 6
                                            spacing: 10

                                            StyledTextLabel {
                                                Layout.preferredWidth: 18
                                                text: blockDelegate.modelData.done ? "✓" : "○"
                                                color: blockDelegate.modelData.done ? root.doneColor : ui.theme.fontPrimaryColor
                                                opacity: blockDelegate.modelData.done ? 1 : 0.5
                                            }
                                            StyledTextLabel {
                                                Layout.preferredWidth: 170
                                                horizontalAlignment: Text.AlignLeft
                                                elide: Text.ElideRight
                                                text: blockDelegate.modelData.song
                                                color: root.goldColor
                                            }
                                            StyledTextLabel {
                                                Layout.fillWidth: true
                                                horizontalAlignment: Text.AlignLeft
                                                elide: Text.ElideRight
                                                text: (blockDelegate.modelData.kind === "warmup" ? qsTrc("starscore", "Warm-up: ")
                                                       : blockDelegate.modelData.kind === "runthrough" ? qsTrc("starscore", "Run-through: ") : "")
                                                      + blockDelegate.modelData.title
                                                      + (blockDelegate.modelData.solo ? "  ·  " + blockDelegate.modelData.solo : "")
                                            }
                                            StyledTextLabel {
                                                Layout.preferredWidth: 110
                                                horizontalAlignment: Text.AlignLeft
                                                text: blockDelegate.modelData.categoryName
                                                opacity: 0.7
                                            }
                                            StyledTextLabel {
                                                Layout.preferredWidth: 60
                                                horizontalAlignment: Text.AlignRight
                                                text: blockDelegate.modelData.tempo > 0 ? "♩ = " + blockDelegate.modelData.tempo : ""
                                                opacity: 0.7
                                            }
                                            StyledTextLabel {
                                                Layout.preferredWidth: 56
                                                horizontalAlignment: Text.AlignRight
                                                text: qsTrc("starscore", "%1 min").arg(blockDelegate.modelData.minutes)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
