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
    property string setlistAction: ""   // "new" or "rename"
    property bool showSkills: false
    property int addMinutesValue: 30

    readonly property color goldColor: "#CFA85E"
    readonly property color doneColor: "#3FB05A"
    readonly property color missingColor: "#E0463A"
    readonly property var areas: ["parts", "comping", "solos"]
    readonly property var areaNames: ({ "parts": qsTrc("starscore", "Parts"), "comping": qsTrc("starscore", "Comping"),
                                        "solos": qsTrc("starscore", "Solos") })
    readonly property int needWidth: 72

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

    // A narrow window (a laptop screen with the sidebar): two columns, the making under the setlist, so nothing goes
    // off the right edge (Joel, 24 Oct 2026)
    readonly property bool narrow: width < 1180

    GridLayout {
        anchors.fill: parent
        anchors.margins: 24
        columnSpacing: 20
        rowSpacing: 16
        columns: root.narrow ? 2 : 3

        // ---------------- Setlist ----------------
        Rectangle {
            Layout.row: 0
            Layout.column: 0
            Layout.preferredWidth: 400
            Layout.minimumWidth: root.narrow ? 330 : 360
            Layout.fillHeight: true
            radius: 6
            color: ui.theme.backgroundPrimaryColor
            border.color: ui.theme.strokeColor

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                // the saved setlists
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 60
                    spacing: 4
                    StyledDropdown {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 60
                        model: pm.setlists.map(function(sl) { return { "text": sl.name, "value": sl.id } })
                        currentIndex: pm.setlists.findIndex(function(sl) { return sl.id === pm.setlistId })
                        onActivated: function(index, value) { root.openDay = -1; pm.selectSetlist(value) }
                    }
                    FlatButton {
                        minWidth: 44
                        text: qsTrc("starscore", "New")
                        toolTipTitle: qsTrc("starscore", "A new setlist with its own date and plans")
                        toolTipDescription: qsTrc("starscore", "It starts as a copy of this one.")
                        onClicked: { root.setlistAction = root.setlistAction === "new" ? "" : "new"; newName.currentText = ""; newDate.currentText = "" }
                    }
                    FlatButton {
                        minWidth: 56
                        text: qsTrc("starscore", "Rename")
                        onClicked: { root.setlistAction = root.setlistAction === "rename" ? "" : "rename"; newName.currentText = pm.setlistName }
                    }
                    FlatButton {
                        minWidth: 50
                        text: qsTrc("starscore", "Delete")
                        enabled: pm.setlists.length > 1
                        onClicked: pm.deleteSetlist()
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 60
                    spacing: 4
                    visible: root.setlistAction !== ""
                    TextInputField {
                        id: newName
                        Layout.fillWidth: true
                        Layout.preferredWidth: 60
                        hint: qsTrc("starscore", "Name, e.g. Show Sat 31 Oct")
                    }
                    TextInputField {
                        id: newDate
                        Layout.preferredWidth: 100
                        visible: root.setlistAction === "new"
                        hint: qsTrc("starscore", "Show date")
                    }
                    FlatButton {
                        minWidth: 50
                        accentButton: true
                        text: root.setlistAction === "new" ? qsTrc("starscore", "Make") : qsTrc("starscore", "Save")
                        onClicked: {
                            if (root.setlistAction === "new") {
                                root.openDay = -1
                                pm.newSetlist(newName.currentText, newDate.currentText)
                            } else {
                                pm.renameSetlist(newName.currentText)
                            }
                            root.setlistAction = ""
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 60
                    StyledTextLabel {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 60
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
                    Layout.preferredWidth: 60
                    visible: !root.editingSetlist
                    wrapMode: Text.WordWrap
                    horizontalAlignment: Text.AlignLeft
                    opacity: 0.7
                    text: qsTrc("starscore", "How much each song needs, from 0 (nothing) to 3 (a lot). Click a number to change it.")
                }

                // the column headers
                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 60
                    visible: !root.editingSetlist
                    spacing: 4
                    StyledTextLabel {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 60
                        horizontalAlignment: Text.AlignLeft
                        text: qsTrc("starscore", "Song")
                        font: ui.theme.bodyBoldFont
                        opacity: 0.8
                    }
                    Repeater {
                        model: root.areas
                        delegate: StyledTextLabel {
                            required property string modelData
                            Layout.preferredWidth: root.needWidth
                            horizontalAlignment: Text.AlignHCenter
                            text: root.areaNames[modelData]
                            font: ui.theme.bodyBoldFont
                            opacity: 0.8
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredWidth: 60
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
                    Layout.preferredWidth: 60
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
                                Layout.preferredWidth: 60
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
                                    Layout.preferredWidth: 60
                                    spacing: 4

                                    StyledTextLabel {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 60
                                        horizontalAlignment: Text.AlignLeft
                                        elide: Text.ElideRight
                                        text: modelData.title
                                        color: modelData.found ? ui.theme.fontPrimaryColor : root.missingColor
                                    }
                                    Repeater {
                                        model: modelData.found ? root.areas : []
                                        delegate: FlatButton {
                                            required property string modelData
                                            readonly property var song: songDelegate.modelData
                                            minWidth: root.needWidth
                                            Layout.preferredWidth: root.needWidth
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

            }
        }

        // ---------------- Make ----------------
        Rectangle {
            Layout.row: root.narrow ? 1 : 0
            Layout.column: root.narrow ? 0 : 1
            Layout.preferredWidth: root.narrow ? 400 : 290
            Layout.minimumWidth: root.narrow ? 330 : 270
            Layout.fillWidth: root.narrow
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
                        text: qsTrc("starscore", "Breaks")
                        font: ui.theme.headerBoldFont
                    }
                    RowLayout {
                        spacing: 8
                        StyledTextLabel { text: qsTrc("starscore", "Every") }
                        IncrementalPropertyControl {
                            Layout.preferredWidth: 80
                            currentValue: pm.breakEvery
                            step: 5; decimals: 0; minValue: 0; maxValue: 180
                            onValueEditingFinished: function(v) { pm.setBreakEvery(v) }
                        }
                        StyledTextLabel { text: qsTrc("starscore", "min, for") }
                        IncrementalPropertyControl {
                            Layout.preferredWidth: 70
                            currentValue: pm.breakMinutes
                            step: 1; decimals: 0; minValue: 1; maxValue: 30
                            onValueEditingFinished: function(v) { pm.setBreakMinutes(v) }
                        }
                    }
                    StyledTextLabel {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 60
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignLeft
                        opacity: 0.7
                        text: pm.breakEvery > 0 ? qsTrc("starscore", "Hands free stops for a break after about %1 minutes of practice. (0: no breaks.)").arg(pm.breakEvery)
                                                : qsTrc("starscore", "No breaks. Set how often for hands free.")
                    }

                    SeparatorLine { Layout.topMargin: 8; Layout.bottomMargin: 8 }

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
                            onValueEditingFinished: function(v) { pm.setRoutineMinutes(v) }
                        }
                    }
                    FlatButton {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 60
                        text: qsTrc("starscore", "Make a routine")
                        accentButton: true
                        enabled: !pm.busy
                        onClicked: { root.openDay = 0; pm.makeRoutine() }
                    }

                    SeparatorLine { Layout.topMargin: 8; Layout.bottomMargin: 8 }

                    StyledTextLabel {
                        text: qsTrc("starscore", "Up to a show")
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
                            onValueEditingFinished: function(v) { pm.setDefaultMinutes(v) }
                        }
                    }
                    StyledTextLabel {
                        Layout.fillWidth: true
                        Layout.preferredWidth: 60
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
                            Layout.preferredWidth: 60
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
                                onValueEditingFinished: function(v) { pm.setDayMinutes(modelData.date, v) }
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
                        Layout.preferredWidth: 60
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
            Layout.row: 0
            Layout.column: root.narrow ? 1 : 2
            Layout.rowSpan: root.narrow ? 2 : 1
            Layout.fillWidth: true
            Layout.preferredWidth: 400   // (wider when there's room: the long titles elide instead of widening it)
            Layout.minimumWidth: root.narrow ? 340 : 420
            Layout.fillHeight: true
            spacing: 10

            // (the buttons go on to a second line when the column is narrow, instead of off the right edge; Joel, 24 Oct
            // 2026)
            Flow {
                id: plansHeader
                Layout.fillWidth: true
                Layout.preferredWidth: 60
                spacing: 8
                StyledTextLabel {
                    height: 30
                    verticalAlignment: Text.AlignVCenter
                    text: qsTrc("starscore", "Practice")
                    font: ui.theme.headerBoldFont
                }
                StyledDropdown {
                    width: Math.min(320, plansHeader.width)
                    visible: pm.plans.length > 0
                    model: pm.plans.map(function(p) { return { "text": p.name + " — " + p.subtitle, "value": p.id } })
                    currentIndex: pm.plans.findIndex(function(p) { return p.id === pm.planId })
                    onActivated: function(index, value) { root.openDay = -1; pm.selectPlan(value) }
                }
                FlatButton {
                    minWidth: 60
                    text: root.showSkills ? qsTrc("starscore", "Plans") : qsTrc("starscore", "Skills")
                    toolTipTitle: qsTrc("starscore", "How good you are at each skill, from what you've practised")
                    onClicked: {
                        root.showSkills = !root.showSkills
                        if (root.showSkills) {
                            pm.refreshSkills()
                        }
                    }
                }
                FlatButton {
                    minWidth: 60
                    text: qsTrc("starscore", "Song folder…")
                    toolTipTitle: qsTrc("starscore", "The folder with your Starsign songs")
                    toolTipDescription: pm.libraryFolder
                    enabled: !pm.busy
                    onClicked: pm.chooseLibraryFolder()
                }
                FlatButton {
                    minWidth: 60
                    text: qsTrc("starscore", "Read songs again")
                    enabled: !pm.busy
                    onClicked: pm.rescan()
                }
            }

            StyledTextLabel {
                Layout.fillWidth: true
                Layout.preferredWidth: 60
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                text: pm.status
                opacity: 0.8
                MouseArea {
                    anchors.fill: parent
                    enabled: pm.status.indexOf("Saved ") === 0
                    cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: pm.openOutputFolder()   // (the folder the book or the iReal page was saved in)
                }
            }

            Flow {
                Layout.fillWidth: true
                Layout.preferredWidth: 60
                spacing: 8
                visible: pm.plan.id !== undefined
                FlatButton {
                    minWidth: 60
                    text: qsTrc("starscore", "Practice today")
                    accentButton: true
                    enabled: pm.plan.todayIndex !== undefined && pm.plan.todayIndex >= 0
                    onClicked: pm.openRunner(pm.plan.todayIndex, 0)
                }
                FlatButton {
                    minWidth: 60
                    text: qsTrc("starscore", "Hands free")
                    toolTipTitle: qsTrc("starscore", "Plays today's practice without stopping")
                    toolTipDescription: qsTrc("starscore", "Each part's backing and timer start by themselves, with breaks. Put the laptop on a stand and play.")
                    enabled: pm.plan.todayIndex !== undefined && pm.plan.todayIndex >= 0
                    onClicked: pm.openRunner(pm.plan.todayIndex, 0, true)
                }
                FlatButton {
                    minWidth: 60
                    text: qsTrc("starscore", "Practice book (PDF)")
                    enabled: !pm.busy
                    onClicked: pm.makeBook(-1)
                }
                FlatButton {
                    minWidth: 60
                    text: qsTrc("starscore", "iReal Pro charts")
                    onClicked: pm.exportIreal(-1)
                }
                FlatButton {
                    minWidth: 60
                    text: qsTrc("starscore", "Plan again from today")
                    toolTipTitle: qsTrc("starscore", "Plan again from today")
                    toolTipDescription: qsTrc("starscore", "Plans the days from today on again, from what you've done so far and how solid it is.")
                    enabled: !pm.busy
                    onClicked: pm.rebuildPlan()
                }
                FlatButton {
                    minWidth: 60
                    text: qsTrc("starscore", "Delete")
                    onClicked: pm.deletePlan(pm.planId)
                }
            }

            StyledTextLabel {
                Layout.fillWidth: true
                Layout.preferredWidth: 60
                visible: pm.plan.id !== undefined && !root.showSkills
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "%1 of %2 done").arg(pm.plan.done || 0).arg(pm.plan.total || 0)
                opacity: 0.7
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredWidth: 60
                Layout.fillHeight: true
                radius: 6
                color: ui.theme.backgroundPrimaryColor
                border.color: ui.theme.strokeColor

                StyledTextLabel {
                    anchors.centerIn: parent
                    width: parent.width - 60
                    visible: pm.plan.id === undefined && !root.showSkills
                    wrapMode: Text.WordWrap
                    text: pm.busy ? "" : qsTrc("starscore", "Make a routine for one session, or a plan for every day up to your show.")
                    opacity: 0.7
                }

                // ---------------- skills ----------------
                StyledFlickable {
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: root.showSkills
                    contentHeight: skillColumn.implicitHeight
                    clip: true
                    ColumnLayout {
                        id: skillColumn
                        width: parent.width
                        spacing: 4
                        StyledTextLabel {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 60
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.WordWrap
                            opacity: 0.7
                            text: pm.skills.length === 0
                                  ? qsTrc("starscore", "No skills yet: they come from the items you mark done.")
                                  : qsTrc("starscore", "From every item you've marked done, in every song. Plans skip what you've got and give you the next level up.")
                        }
                        Repeater {
                            model: pm.skills
                            delegate: ColumnLayout {
                                id: skillDelegate
                                required property var modelData
                                required property int index
                                Layout.fillWidth: true
                                Layout.preferredWidth: 60
                                spacing: 2
                                StyledTextLabel {
                                    visible: skillDelegate.index === 0 || pm.skills[skillDelegate.index - 1].group !== skillDelegate.modelData.group
                                    Layout.topMargin: skillDelegate.index === 0 ? 4 : 10
                                    text: skillDelegate.modelData.group
                                    font: ui.theme.bodyBoldFont
                                    color: root.goldColor
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 60
                                    spacing: 10
                                    StyledTextLabel {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 60
                                        horizontalAlignment: Text.AlignLeft
                                        elide: Text.ElideRight
                                        text: skillDelegate.modelData.name
                                    }
                                    Rectangle {
                                        Layout.preferredWidth: 120
                                        Layout.preferredHeight: 8
                                        radius: 4
                                        color: ui.theme.buttonColor
                                        Rectangle {
                                            width: parent.width * Math.min(1, skillDelegate.modelData.rating / 5)
                                            height: parent.height
                                            radius: 4
                                            color: root.goldColor
                                        }
                                    }
                                    StyledTextLabel {
                                        Layout.preferredWidth: 150
                                        horizontalAlignment: Text.AlignLeft
                                        text: skillDelegate.modelData.levelText
                                        opacity: 0.8
                                    }
                                    StyledTextLabel {
                                        Layout.preferredWidth: 40
                                        horizontalAlignment: Text.AlignRight
                                        text: skillDelegate.modelData.trend > 0.05 ? "▲" : ""
                                        color: root.doneColor
                                    }
                                    StyledTextLabel {
                                        Layout.preferredWidth: 110
                                        horizontalAlignment: Text.AlignRight
                                        text: qsTrc("starscore", "%1 items · %2").arg(skillDelegate.modelData.items).arg(skillDelegate.modelData.last)
                                        opacity: 0.6
                                    }
                                }
                            }
                        }
                    }
                }

                StyledFlickable {
                    anchors.fill: parent
                    anchors.margins: 12
                    visible: pm.plan.id !== undefined && !root.showSkills
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
                                Layout.preferredWidth: 60
                                spacing: 2
                                readonly property bool expanded: root.openDay === modelData.index

                                RowLayout {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 60
                                    spacing: 8

                                    FlatButton {
                                        Layout.preferredWidth: 28
                                        icon: dayDelegate.expanded ? IconCode.SMALL_ARROW_DOWN : IconCode.SMALL_ARROW_RIGHT
                                        transparent: true
                                        onClicked: root.openDay = dayDelegate.expanded ? -1 : dayDelegate.modelData.index
                                    }
                                    StyledTextLabel {
                                        Layout.fillWidth: true
                                        Layout.preferredWidth: 60
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
                                }
                                // the day's buttons on a line of their own (they went off the edge beside the title)
                                Flow {
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 60
                                    Layout.leftMargin: 36
                                    spacing: 6
                                    FlatButton {
                                        minWidth: 70
                                        text: qsTrc("starscore", "Practice")
                                        enabled: dayDelegate.modelData.total > 0
                                        onClicked: pm.openRunner(dayDelegate.modelData.index, 0)
                                    }
                                    FlatButton {
                                        minWidth: 80
                                        text: qsTrc("starscore", "Hands free")
                                        enabled: dayDelegate.modelData.total > dayDelegate.modelData.done
                                        onClicked: pm.openRunner(dayDelegate.modelData.index, 0, true)
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
                                        Layout.preferredWidth: 60
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
                                                Layout.preferredWidth: root.narrow ? 100 : 170
                                                horizontalAlignment: Text.AlignLeft
                                                elide: Text.ElideRight
                                                text: blockDelegate.modelData.song
                                                color: root.goldColor
                                            }
                                            StyledTextLabel {
                                                Layout.fillWidth: true
                                                Layout.preferredWidth: 60
                                                horizontalAlignment: Text.AlignLeft
                                                elide: Text.ElideRight
                                                text: (blockDelegate.modelData.kind === "warmup" ? qsTrc("starscore", "Warm-up: ")
                                                       : blockDelegate.modelData.kind === "runthrough" ? qsTrc("starscore", "Run-through: ") : "")
                                                      + blockDelegate.modelData.title
                                                      + (blockDelegate.modelData.solo ? "  ·  " + blockDelegate.modelData.solo : "")
                                            }
                                            StyledTextLabel {
                                                visible: !root.narrow   // (narrow: the title gets the room)
                                                Layout.preferredWidth: 110
                                                horizontalAlignment: Text.AlignLeft
                                                elide: Text.ElideRight
                                                text: blockDelegate.modelData.categoryName
                                                opacity: 0.7
                                            }
                                            StyledTextLabel {
                                                Layout.preferredWidth: 58
                                                horizontalAlignment: Text.AlignRight
                                                text: blockDelegate.modelData.tempo > 0 ? "♩ = " + blockDelegate.modelData.tempo : ""
                                                opacity: 0.7
                                            }
                                            StyledTextLabel {
                                                Layout.preferredWidth: 52
                                                horizontalAlignment: Text.AlignRight
                                                text: qsTrc("starscore", "%1 min").arg(blockDelegate.modelData.minutes)
                                            }
                                        }
                                    }
                                }

                                // more for this day: N minutes of new items at the end
                                Flow {
                                    visible: dayDelegate.expanded
                                    Layout.fillWidth: true
                                    Layout.preferredWidth: 60
                                    Layout.leftMargin: 36
                                    Layout.topMargin: 4
                                    spacing: 8
                                    StyledTextLabel { height: 30; verticalAlignment: Text.AlignVCenter; text: qsTrc("starscore", "Want more? Add") }
                                    IncrementalPropertyControl {
                                        width: 90
                                        currentValue: root.addMinutesValue
                                        step: 5; decimals: 0; minValue: 5; maxValue: 240
                                        onValueEdited: function(v) { root.addMinutesValue = v }
                                    }
                                    StyledTextLabel { height: 30; verticalAlignment: Text.AlignVCenter; text: qsTrc("starscore", "minutes") }
                                    FlatButton {
                                        minWidth: 60
                                        text: qsTrc("starscore", "Add")
                                        toolTipTitle: qsTrc("starscore", "Adds new items to the end of this day")
                                        toolTipDescription: qsTrc("starscore", "Chosen from what the songs still need, after what this day already has.")
                                        enabled: !pm.busy
                                        onClicked: pm.addMinutes(dayDelegate.modelData.index, root.addMinutesValue)
                                    }
                                    FlatButton {
                                        minWidth: 90
                                        text: qsTrc("starscore", "Add a jam")
                                        toolTipTitle: qsTrc("starscore", "10 minutes of improvising over one of the songs' changes")
                                        toolTipDescription: qsTrc("starscore", "A backing track in some groove and tempo, looping. Each jam is a different song and groove from the last one.")
                                        enabled: !pm.busy
                                        onClicked: pm.addJam(dayDelegate.modelData.index, 10)
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
