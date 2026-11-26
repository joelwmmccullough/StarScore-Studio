/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — a MuseScore Studio fork
 *
 * The Arrangements / Sections bar shown above the score.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

Rectangle {
    id: root

    property bool collapsed: false

    visible: barModel.hasScore && barModel.panelVisible
    implicitHeight: visible ? content.implicitHeight + 12 : 0
    color: ui.theme.backgroundSecondaryColor

    StarScoreBarModel {
        id: barModel
    }

    // (1.35.0, Joel 15 Nov 2026) rows he hid to make the panel smaller. (1.38.0, Joel 25 Nov: "instead of putting it in
    // the dropdown menu, just add a little eye icon next to each row") the eye before a row's name hides it; the
    // "Hidden:" line at the bottom shows it again
    function rowShown(key) {
        return barModel.hiddenRows.indexOf(key) < 0
    }

    Component.onCompleted: {
        barModel.load()
    }

    //! (1.38.0) the eye before a row's name: hides the row
    component RowEye: FlatButton {
        property string rowKey: ""

        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: 22
        Layout.preferredHeight: 26
        icon: IconCode.EYE_OPEN
        transparent: true
        opacity: mouseArea.containsMouse ? 1.0 : 0.45
        toolTipTitle: qsTrc("starscore", "Hide this row")

        onClicked: barModel.handleMenuItem("panel-row:" + rowKey)
    }

    function rowTitle(key) {
        switch (key) {
        case "arrangements": return barModel.isSoloTranscription ? qsTrc("starscore", "Solo") : qsTrc("starscore", "Arrangements")
        case "sections": return qsTrc("starscore", "Sections")
        case "progress": return qsTrc("starscore", "Progress")
        case "audio": return qsTrc("starscore", "Audio")
        case "solos": return qsTrc("starscore", "Solos")
        }
        return key
    }

    //! hidden rows that this song would show (a solo transcription has no Sections or Solos row)
    readonly property var hiddenShown: barModel.hiddenRows.filter(function(key) {
        return !(barModel.isSoloTranscription && (key === "sections" || key === "solos"))
    })

    //! One pill-shaped button: an arrangement or a section
    component StarScoreChip: Rectangle {
        id: chip

        property string text: ""
        property bool active: false
        property bool dashed: false
        property color dotColor: "transparent"
        property bool showDot: true
        property bool showCheck: false
        property string toolTip: ""
        property var menuItemsProvider: null

        signal leftClicked(var mouse)
        signal menuItemChosen(string itemId)
        signal menuOpenChanged(bool isOpen)

        // (1.40.0) chips with the same reorderGroup can be dragged to a new place among each other (reordered gives
        // the new index); double-clicking and clicking the dot are signals of their own
        property string reorderGroup: ""
        property bool dotClickable: false
        signal reordered(int newIndex)
        signal doubleClicked()
        signal dotClicked()
        property bool dragging: false
        property bool justDragged: false
        property point pressPoint: Qt.point(0, 0)
        transform: Translate { id: dragShift }
        z: dragging ? 100 : 0

        //! Where this chip would go if dropped here: how many of its group's other chips come before the point
        function dropIndex(px, py) {
            let index = 0
            const siblings = chip.parent ? chip.parent.children : []
            for (let i = 0; i < siblings.length; ++i) {
                const c = siblings[i]
                if (c === chip || !c.visible || c.reorderGroup !== chip.reorderGroup) {
                    continue
                }
                const cx = c.x + c.width / 2
                const cy = c.y + c.height / 2
                const sameRow = Math.abs(cy - py) < c.height / 2
                if (cy < py - c.height / 2 || (sameRow && cx < px)) {
                    ++index
                }
            }
            return index
        }

        implicitHeight: 26
        implicitWidth: chipRow.implicitWidth + 20
        radius: height / 2

        color: chip.active ? ui.theme.accentColor : (chipMouse.containsMouse ? ui.theme.buttonColor : "transparent")
        opacity: chip.active ? 1.0 : 0.9
        border.width: 1
        border.color: chip.active ? ui.theme.accentColor : ui.theme.strokeColor

        Row {
            id: chipRow
            anchors.centerIn: parent
            spacing: 6

            StyledIconLabel {
                visible: chip.showCheck
                anchors.verticalCenter: parent.verticalCenter
                iconCode: chip.active ? IconCode.EYE_OPEN : IconCode.EYE_CLOSED
                // (1.38.0) the closed eye in the main text colour, a little lighter (the secondary colour was too faint)
                color: ui.theme.fontPrimaryColor
                opacity: chip.active ? 1.0 : 0.6
            }

            Rectangle {
                id: chipDot
                visible: chip.showDot
                anchors.verticalCenter: parent.verticalCenter
                width: chip.dotClickable && chipMouse.containsMouse ? 11 : 9
                height: width
                radius: width / 2
                color: chip.dotColor
                border.width: 1
                border.color: chip.active ? ui.theme.fontPrimaryColor : Qt.darker(chip.dotColor, 1.3)
            }

            StyledTextLabel {
                anchors.verticalCenter: parent.verticalCenter
                text: chip.text
                font: chip.active ? ui.theme.bodyBoldFont : ui.theme.bodyFont
                // (1.24.0) the dashed "+ New …" chips' text was too faint to read: the main text colour, a little lighter
                color: ui.theme.fontPrimaryColor
                opacity: chip.dashed && !chip.active ? 0.7 : 1.0
            }
        }

        MouseArea {
            id: chipMouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: chip.dragging ? Qt.ClosedHandCursor : Qt.ArrowCursor

            onPressed: function(mouse) {
                chip.pressPoint = Qt.point(mouse.x, mouse.y)
                chip.justDragged = false
            }
            onPositionChanged: function(mouse) {
                if (!pressed || chip.reorderGroup === "" || mouse.buttons !== Qt.LeftButton) {
                    return
                }
                const dx = mouse.x - chip.pressPoint.x
                const dy = mouse.y - chip.pressPoint.y
                if (!chip.dragging && Math.abs(dx) + Math.abs(dy) > 8) {
                    chip.dragging = true
                }
                if (chip.dragging) {
                    dragShift.x = dx
                    dragShift.y = dy
                }
            }
            onReleased: function(mouse) {
                if (!chip.dragging) {
                    return
                }
                const p = chip.mapToItem(chip.parent, mouse.x, mouse.y)   // (the pointer, the drag included)
                const index = chip.dropIndex(p.x, p.y)
                chip.dragging = false
                chip.justDragged = true
                dragShift.x = 0
                dragShift.y = 0
                chip.reordered(index)
            }
            onDoubleClicked: function(mouse) {
                if (mouse.button === Qt.LeftButton) {
                    chip.doubleClicked()
                }
            }

            onClicked: function(mouse) {
                if (chip.justDragged) {
                    chip.justDragged = false
                    return
                }
                if (mouse.button === Qt.RightButton) {
                    if (chip.menuItemsProvider) {
                        chip.openProvider = null
                        chipMenu.toggleOpened(chip.menuItemsProvider())
                    }
                    return
                }
                if (chip.dotClickable && chip.showDot) {
                    const d = chipDot.mapFromItem(chipMouse, mouse.x, mouse.y)
                    if (d.x >= -4 && d.x <= chipDot.width + 4 && d.y >= -6 && d.y <= chipDot.height + 6) {
                        chip.dotClicked()
                        return
                    }
                }
                chip.leftClicked(mouse)
            }

            ToolTip.visible: containsMouse && chip.toolTip !== ""
            ToolTip.delay: 700
            ToolTip.text: chip.toolTip
        }

        StyledMenuLoader {
            id: chipMenu

            onHandleMenuItem: function(itemId) {
                chip.menuItemChosen(itemId)
            }

            // Status items: the menu stays open and shows the new ticks
            onKeepOpenItemChosen: function(itemId) {
                chip.menuItemChosen(itemId)
                const provider = chip.openProvider || chip.menuItemsProvider
                if (provider) {
                    chipMenu.refreshOpen(provider())
                }
            }

            onOpened: chip.menuOpenChanged(true)
            onClosed: chip.menuOpenChanged(false)
        }

        //! provider: what the open menu is read again from after a tick (the right-click menu when not given)
        property var openProvider: null
        function openMenu(items, provider) {
            chip.openProvider = provider || null
            chipMenu.toggleOpened(items)
        }
    }

    ColumnLayout {
        id: content

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 12
        anchors.rightMargin: 8
        anchors.topMargin: 6

        spacing: 6

        // --- Arrangements ---
        RowLayout {
            id: arrangementsRow
            Layout.fillWidth: true
            visible: root.rowShown("arrangements")
            spacing: 8

            RowEye {
                rowKey: "arrangements"
            }

            StyledTextLabel {
                Layout.preferredWidth: 88
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.topMargin: 5
                horizontalAlignment: Text.AlignLeft

                text: barModel.isSoloTranscription ? qsTrc("starscore", "Solo") : qsTrc("starscore", "Arrangements")
                font: ui.theme.bodyBoldFont
            }

            Flow {
                Layout.fillWidth: true
                // takes the room left beside the label and buttons and wraps within it (a narrow window kept the
                // chips on one line and pushed the buttons out of sight)
                Layout.preferredWidth: 1
                spacing: 6

                // (1.24.0) a Solo Transcription: who and what, its sheets, the song it's tied to
                StarScoreChip {
                    visible: barModel.isSoloTranscription
                    text: (barModel.soloInfo.player || "") + " – " + (barModel.soloInfo.song || "")
                          + (barModel.soloInfo.instrumentName ? " · " + barModel.soloInfo.instrumentName : "")
                    showDot: false
                    toolTip: [ barModel.soloInfo.band || "", barModel.soloInfo.recording || "", barModel.soloInfo.link || "" ]
                             .filter(function(t) { return t !== "" }).join("\n")
                }

                StarScoreChip {
                    id: soloKeysChip
                    visible: barModel.isSoloTranscription
                    text: (barModel.soloInfo.mode === "minor" ? qsTrc("starscore", "Minor keys") : qsTrc("starscore", "Major keys")) + " ▾"
                    showDot: false
                    toolTip: qsTrc("starscore", "Name the keys major or minor, and pick a key's other spelling (C♯ major for D♭ major)")
                    menuItemsProvider: function() { return barModel.soloKeysMenu() }
                    onLeftClicked: function(mouse) {
                        soloKeysChip.openMenu(barModel.soloKeysMenu())
                    }
                    onMenuItemChosen: function(itemId) {
                        barModel.handleMenuItem(itemId)
                    }
                }

                StarScoreChip {
                    visible: barModel.isSoloTranscription && (barModel.soloInfo.tiedSongTitle || "") !== ""
                    text: qsTrc("starscore", "Song: %1").arg(barModel.soloInfo.tiedSongTitle || "")
                    showDot: false
                    toolTip: qsTrc("starscore", "Open the song")
                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("solo-open-tied")
                    }
                }

                Repeater {
                    model: barModel.isSoloTranscription ? [] : barModel.arrangements

                    delegate: StarScoreChip {
                        required property var modelData

                        text: modelData.name
                        active: modelData.active
                        dotColor: barModel.statusColor(modelData.status)
                        toolTip: modelData.sections + "\n" + qsTrc("starscore", "Status: ") + barModel.statusName(modelData.status)
                                 + "\n" + qsTrc("starscore", "Click: show · Double-click: rename · Drag: move · Right-click: options")
                        menuItemsProvider: function() { return barModel.arrangementMenu(modelData.id) }
                        reorderGroup: "arrangements"

                        onLeftClicked: function(mouse) {
                            barModel.showArrangement(modelData.id)
                        }
                        onDoubleClicked: {
                            barModel.handleMenuItem("arr-rename:" + modelData.id)
                        }
                        onReordered: function(newIndex) {
                            barModel.moveArrangementTo(modelData.id, newIndex)
                        }
                        onMenuItemChosen: function(itemId) {
                            barModel.handleMenuItem(itemId)
                        }
                    }
                }

                StarScoreChip {
                    id: newArrangementChip
                    visible: !barModel.isSoloTranscription
                    text: qsTrc("starscore", "+ New arrangement")
                    dashed: true
                    showDot: false

                    onLeftClicked: function(mouse) {
                        newArrangementChip.openMenu(barModel.addArrangementMenu())
                    }
                    onMenuItemChosen: function(itemId) {
                        barModel.handleMenuItem(itemId)
                    }
                }
            }

            // (1.40.0) the song's version, copyright year and where it's kept
            FlatButton {
                Layout.alignment: Qt.AlignTop
                visible: !barModel.isSoloTranscription
                icon: IconCode.SETTINGS_COG
                transparent: true
                toolTipTitle: qsTrc("starscore", "Song settings")
                toolTipDescription: qsTrc("starscore", "Version number, copyright year, and where the song is kept")

                onClicked: {
                    barModel.handleMenuItem("song-settings")
                }
            }

            FlatButton {
                Layout.alignment: Qt.AlignTop
                icon: IconCode.MENU_THREE_DOTS
                transparent: true
                toolTipTitle: qsTrc("starscore", "More StarScore options")

                onClicked: {
                    moreMenu.toggleOpened(barModel.moreMenu())
                }

                StyledMenuLoader {
                    id: moreMenu

                    onHandleMenuItem: function(itemId) {
                        barModel.handleMenuItem(itemId)
                    }
                    // the panel's row ticks: the menu stays open
                    onKeepOpenItemChosen: function(itemId) {
                        barModel.handleMenuItem(itemId)
                        moreMenu.refreshOpen(barModel.moreMenu())
                    }
                }
            }

            FlatButton {
                Layout.alignment: Qt.AlignTop
                icon: root.collapsed ? IconCode.SMALL_ARROW_DOWN : IconCode.SMALL_ARROW_UP
                transparent: true
                toolTipTitle: root.collapsed ? qsTrc("starscore", "Show the section, progress and solo rows") : qsTrc("starscore", "Hide the section, progress and solo rows")

                onClicked: {
                    root.collapsed = !root.collapsed
                }
            }

            FlatButton {
                Layout.alignment: Qt.AlignTop
                icon: IconCode.CLOSE_X_ROUNDED
                transparent: true
                toolTipTitle: qsTrc("starscore", "Hide the StarScore panel")
                toolTipDescription: qsTrc("starscore", "Bring it back with View › StarScore panel")

                onClicked: {
                    barModel.hidePanel()
                }
            }
        }

        // --- Sections ---
        RowLayout {
            id: sectionsRow
            Layout.fillWidth: true
            visible: root.rowShown("sections") && !root.collapsed && !barModel.isSoloTranscription
            spacing: 8

            RowEye {
                rowKey: "sections"
            }

            StyledTextLabel {
                Layout.preferredWidth: 88
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.topMargin: 5
                horizontalAlignment: Text.AlignLeft

                text: qsTrc("starscore", "Sections")
                font: ui.theme.bodyBoldFont
            }

            Flow {
                Layout.fillWidth: true
                // takes the room left beside the label and buttons and wraps within it (a narrow window kept the
                // chips on one line and pushed the buttons out of sight)
                Layout.preferredWidth: 1
                spacing: 6

                Repeater {
                    model: barModel.sections

                    delegate: StarScoreChip {
                        required property var modelData

                        text: modelData.name
                        active: modelData.on
                        showCheck: true
                        // teal: finished, with drums / percussion / keys reading the lead sheet
                        dotColor: modelData.leadSheet ? "#2FB5A8" : barModel.statusColor(modelData.status)
                        toolTip: qsTrc("starscore", "%1 instrument(s)").arg(modelData.instrumentCount)
                                 + "\n" + qsTrc("starscore", "Status: ") + modelData.statusText
                                 + "\n" + qsTrc("starscore", "Click: show/hide · Alt-click: show only this · Double-click: rename")
                                 + "\n" + qsTrc("starscore", "Click the dot: status · Drag: move · Right-click: options")
                        menuItemsProvider: function() { return barModel.sectionMenu(modelData.id) }
                        reorderGroup: "sections"
                        dotClickable: true

                        onDotClicked: {
                            const sid = modelData.id
                            openMenu(barModel.sectionStatusMenu(sid), function() { return barModel.sectionStatusMenu(sid) })
                        }
                        onDoubleClicked: {
                            barModel.toggleSection(modelData.id)   // (the first click of the two showed or hid it)
                            barModel.handleMenuItem("sec-rename:" + modelData.id)
                        }
                        onReordered: function(newIndex) {
                            barModel.moveSectionTo(modelData.id, newIndex)
                        }
                        // the chips are rebuilt when the panel updates, which would close the open menu
                        onMenuOpenChanged: function(isOpen) { barModel.setHoldUpdates(isOpen) }
                        Component.onDestruction: barModel.setHoldUpdates(false)

                        onLeftClicked: function(mouse) {
                            if (mouse.modifiers & (Qt.AltModifier | Qt.MetaModifier | Qt.ControlModifier)) {
                                barModel.soloSection(modelData.id)
                            } else {
                                barModel.toggleSection(modelData.id)
                            }
                        }
                        onMenuItemChosen: function(itemId) {
                            barModel.handleMenuItem(itemId)
                        }
                    }
                }

                StarScoreChip {
                    id: newSectionChip
                    text: qsTrc("starscore", "+ New section")
                    dashed: true
                    showDot: false

                    onLeftClicked: function(mouse) {
                        newSectionChip.openMenu(barModel.addSectionMenu())
                    }
                    onMenuItemChosen: function(itemId) {
                        barModel.handleMenuItem(itemId)
                    }
                }

                // every section shown or hidden in one click (Joel, 16 Oct 2026)
                StarScoreChip {
                    text: qsTrc("starscore", "Show all")
                    dashed: true
                    showDot: false
                    toolTip: qsTrc("starscore", "Show every section")

                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("all-on")
                    }
                }

                StarScoreChip {
                    text: qsTrc("starscore", "Hide all")
                    dashed: true
                    showDot: false
                    toolTip: qsTrc("starscore", "Hide every section")

                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("all-off")
                    }
                }

                // (1.40.0, from the old "…" › Sections) whether the section scores and the percussion score are tabs
                StarScoreChip {
                    text: qsTrc("starscore", "Section scores")
                    showDot: false
                    showCheck: true
                    active: barModel.sectionScoresShown
                    toolTip: qsTrc("starscore", "Show or hide each section's own score as a tab")

                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("toggle-section-scores")
                    }
                }

                StarScoreChip {
                    text: qsTrc("starscore", "Percussion score")
                    showDot: false
                    showCheck: true
                    active: barModel.percussionScoreShown
                    toolTip: qsTrc("starscore", "Show or hide the percussion score as a tab")

                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("toggle-percussion-score")
                    }
                }
            }
        }

        // --- Progress colors (Joel, 7 Oct 2026): select measures, then click a color ---
        RowLayout {
            id: progressRow
            Layout.fillWidth: true
            visible: root.rowShown("progress") && !root.collapsed
            spacing: 8

            RowEye {
                rowKey: "progress"
            }

            StyledTextLabel {
                Layout.preferredWidth: 88
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.topMargin: 5
                horizontalAlignment: Text.AlignLeft

                text: qsTrc("starscore", "Progress")
                font: ui.theme.bodyBoldFont
            }

            Flow {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 6

                StarScoreChip {
                    text: barModel.progressShown ? qsTrc("starscore", "Colors shown") : qsTrc("starscore", "Colors hidden")
                    active: barModel.progressShown
                    showDot: false
                    toolTip: qsTrc("starscore", "Show or hide the progress colors. Hiding them keeps them.\nThey never appear in exports.")
                    onLeftClicked: function(mouse) {
                        barModel.runAction("starscore-progress-colors")
                    }
                }

                Repeater {
                    model: [
                        { code: "starscore-progress-finished", name: qsTrc("starscore", "Finished"), color: "#28AA3C" },
                        { code: "starscore-progress-review", name: qsTrc("starscore", "Needs review"), color: "#FF8C00" },
                        { code: "starscore-progress-unfinished", name: qsTrc("starscore", "Unfinished"), color: "#E11E1E" },
                        { code: "starscore-progress-clear", name: qsTrc("starscore", "Clear"), color: "transparent" }
                    ]

                    delegate: StarScoreChip {
                        required property var modelData

                        text: modelData.name
                        showDot: modelData.color !== "transparent"
                        dotColor: modelData.color
                        toolTip: qsTrc("starscore", "Select measures (click, then Shift+click), then click here to color them.")
                        onLeftClicked: function(mouse) {
                            barModel.runAction(modelData.code)
                        }
                    }
                }
            }
        }

        // --- Audio: the score's recordings, video and Transcribe! files, stored inside the .starscore (1.26.0; in 1.25.0
        // they were in an Audio folder beside it, still shown until moved in) ---
        RowLayout {
            id: audioRow
            Layout.fillWidth: true
            visible: root.rowShown("audio") && !root.collapsed && (barModel.audioFiles.length > 0 || barModel.canAddAudio)
            spacing: 8

            RowEye {
                rowKey: "audio"
            }

            StyledTextLabel {
                Layout.preferredWidth: 88
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.topMargin: 5
                horizontalAlignment: Text.AlignLeft

                text: qsTrc("starscore", "Audio")
                font: ui.theme.bodyBoldFont
            }

            Flow {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 6

                Repeater {
                    model: barModel.audioFiles

                    delegate: StarScoreChip {
                        required property var modelData

                        text: (modelData.kind === "transcribe" ? "T! " : modelData.kind === "video" ? "▶ " : "♪ ") + modelData.name
                              + (modelData.inside ? "" : qsTrc("starscore", " (Audio folder)"))
                        showDot: false
                        dashed: !modelData.inside
                        toolTip: (modelData.kind === "transcribe" ? qsTrc("starscore", "Click to open it in Transcribe!")
                                  : modelData.kind === "video" ? qsTrc("starscore", "Click to watch it")
                                  : qsTrc("starscore", "Click to play it"))
                                 + "\n" + (modelData.inside ? qsTrc("starscore", "Stored inside this score")
                                                            : qsTrc("starscore", "Still in the Audio folder beside the score; it moves in when you reopen the score and say so"))
                                 + "\n" + qsTrc("starscore", "Right-click for options")
                        menuItemsProvider: function() { return barModel.audioFileMenu(modelData) }
                        onLeftClicked: function(mouse) {
                            barModel.handleMenuItem("audio-open:" + modelData.id)
                        }
                        onMenuItemChosen: function(itemId) {
                            barModel.handleMenuItem(itemId)
                        }
                    }
                }

                StarScoreChip {
                    visible: barModel.canAddAudio
                    text: qsTrc("starscore", "+ Audio, video or Transcribe! file")
                    dashed: true
                    showDot: false
                    toolTip: qsTrc("starscore", "The full mix, the piano stem, everything but piano, the video, a Transcribe! file…: "
                                   + "stored inside this score, so it's all in one file")
                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("audio-add")
                    }
                }

                // (1.40.0, from the old "…") the song's recordings list, and its reference PDFs
                StarScoreChip {
                    text: qsTrc("starscore", "Recordings…")
                    dashed: true
                    showDot: false
                    toolTip: qsTrc("starscore", "The song's live and studio recordings, and their ratings")
                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("recordings")
                    }
                }

                StarScoreChip {
                    text: barModel.referenceCount > 0 ? qsTrc("starscore", "Reference PDFs (%1)").arg(barModel.referenceCount)
                                                      : qsTrc("starscore", "+ Reference PDF")
                    dashed: true
                    showDot: false
                    toolTip: barModel.referenceCount > 0 ? qsTrc("starscore", "Show the reference PDFs beside the score")
                                                         : qsTrc("starscore", "Add a PDF (an original chart, say), kept inside this score")
                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem(barModel.referenceCount > 0 ? "ref-panel" : "ref-add")
                    }
                }
            }
        }

        // --- Solos ---
        RowLayout {
            id: solosRow
            Layout.fillWidth: true
            visible: root.rowShown("solos") && !root.collapsed && !barModel.isSoloTranscription
                     && (barModel.solos.length > 0 || barModel.canAddSolos || barModel.tiedSolos.length > 0)
            spacing: 8

            RowEye {
                rowKey: "solos"
            }

            StyledTextLabel {
                Layout.preferredWidth: 88
                Layout.alignment: Qt.AlignTop | Qt.AlignLeft
                Layout.topMargin: 5
                horizontalAlignment: Text.AlignLeft

                text: qsTrc("starscore", "Solos")
                font: ui.theme.bodyBoldFont
            }

            Flow {
                Layout.fillWidth: true
                // takes the room left beside the label and buttons and wraps within it (a narrow window kept the
                // chips on one line and pushed the buttons out of sight)
                Layout.preferredWidth: 1
                spacing: 6

                StarScoreChip {
                    visible: barModel.solos.length > 0
                    text: qsTrc("starscore", "Main score")
                    active: !barModel.isSoloView
                    showDot: false
                    onLeftClicked: function(mouse) {
                        barModel.showMainScore()
                    }
                }

                Repeater {
                    model: barModel.solos

                    delegate: StarScoreChip {
                        required property var modelData

                        text: modelData.name
                        active: modelData.active
                        showDot: false
                        toolTip: modelData.info + "\n" + qsTrc("starscore", "Right-click for options")
                        menuItemsProvider: function() { return barModel.soloMenu(modelData.id) }

                        onLeftClicked: function(mouse) {
                            barModel.showSolo(modelData.id)
                        }
                        onMenuItemChosen: function(itemId) {
                            barModel.handleMenuItem(itemId)
                        }
                    }
                }

                // (1.24.0) the song's Solo Transcriptions, each in its own file: clicked, it opens
                Repeater {
                    model: barModel.tiedSolos

                    delegate: StarScoreChip {
                        required property var modelData

                        text: modelData.name
                        showDot: false
                        toolTip: qsTrc("starscore", "Solo transcription (its own file): click to open it")
                        onLeftClicked: function(mouse) {
                            barModel.handleMenuItem("solo-open-file:" + modelData.path)
                        }
                    }
                }

                StarScoreChip {
                    visible: barModel.canAddSolos
                    text: qsTrc("starscore", "+ New solo transcription")
                    dashed: true
                    showDot: false
                    toolTip: qsTrc("starscore", "A solo transcription in its own file, in all twelve keys, tied to this song.\n"
                                   + "Right-click to add a solo kept inside the song instead (the old way).")
                    menuItemsProvider: function() {
                        return [ { "id": "solo-add", "title": qsTrc("starscore", "Add a solo kept inside the song (the old way)…"), "enabled": true } ]
                    }
                    onLeftClicked: function(mouse) {
                        barModel.handleMenuItem("solo-new")
                    }
                    onMenuItemChosen: function(itemId) {
                        barModel.handleMenuItem(itemId)
                    }
                }
            }
        }

        // --- (1.38.0) the hidden rows: click one to show it again ---
        RowLayout {
            id: hiddenRowsStrip
            Layout.fillWidth: true
            visible: root.hiddenShown.length > 0
            spacing: 6

            StyledTextLabel {
                Layout.preferredWidth: 118
                horizontalAlignment: Text.AlignLeft
                color: ui.theme.fontPrimaryColor
                opacity: 0.7
                text: qsTrc("starscore", "Hidden:")
            }

            Flow {
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                spacing: 6

                Repeater {
                    model: root.hiddenShown

                    StarScoreChip {
                        text: root.rowTitle(modelData)
                        showDot: false
                        showCheck: true
                        toolTip: qsTrc("starscore", "Show this row")
                        onLeftClicked: function(mouse) {
                            barModel.handleMenuItem("panel-row:" + modelData)
                        }
                    }
                }
            }
        }
    }

    SeparatorLine {
        anchors.bottom: parent.bottom
    }
}
