/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — New StarScore
 *
 * File › New: first "Starsign Score" or "Non-Starsign Score". Non-Starsign hands over to MuseScore's own new-score
 * wizard (the dialog closes with the value "musescore-wizard"; ProjectActionsController::newProject opens the wizard);
 * Starsign goes on to the StarScore form below; Solo Transcription (1.24.0) to its own form: the solo's song,
 * player, band, recording, link and instrument, and the song it's tied to.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "New Score")

    // 0 = the Starsign / Non-Starsign / Solo Transcription choice, 1 = the StarScore form, 2 = the solo form
    property int page: 0

    contentWidth: root.page === 0 ? 560 : 460
    contentHeight: root.page === 0 ? 220 : 680
    margins: 20

    NewStarScoreModel {
        id: newModel
    }

    property string scoreTitle: ""
    // what the song is (Joel, 19 Oct 2026): where it's filed in Projects and Sheets, and a vocal cover gets a Voice part
    property string songKind: "original"
    property string subtitle: ""
    property string composer: ""
    property int keyFifths: 0
    property int timeNum: 4
    property int timeDen: 4
    property int tempo: 120

    function tempoLabel(num, den) {
        var dotted = num > 3 && num % 3 === 0 && den >= 2
        var value = dotted ? den / 2 : den
        if (dotted && value === 1) {
            dotted = false
            value = 2
        }
        var names = { 1: qsTrc("starscore", "whole"), 2: qsTrc("starscore", "half"), 4: qsTrc("starscore", "quarter"),
                      8: qsTrc("starscore", "eighth"), 16: qsTrc("starscore", "16th") }
        var name = names[Math.min(16, Math.max(1, value))] || qsTrc("starscore", "16th")
        return qsTrc("starscore", "Tempo (%1 =)").arg(dotted ? qsTrc("starscore", "dotted %1").arg(name) : name)
    }
    property int measures: 32
    // the last choices, remembered in settings
    property string arrangementKey: newModel.lastArrangementKey
    property string doublerId: newModel.lastDoublerId
    property string lowHornId: newModel.lastLowHornId
    // with Ben on Bass Clarinet, the 7th horn can't be a second bass clarinet
    onDoublerIdChanged: {
        if (root.doublerId === "bb-bass-clarinet" && root.lowHornId === "bb-bass-clarinet") {
            root.lowHornId = "bass-trombone"
        }
    }

    property bool showDoubler: newModel.hasDoubler(root.arrangementKey)
    property bool showLowHorn: newModel.hasLowHorn(root.arrangementKey)

    function chooseStarsign() {
        root.page = 1
        titleField.ensureActiveFocus()
    }

    // --- the Solo Transcription form
    property string soloSong: ""
    property string soloPlayer: ""
    property string soloBand: ""
    property string soloRecording: ""
    property string soloLink: ""
    property string soloInstrument: newModel.lastSoloInstrumentId
    property string soloTiedCode: ""
    // opened for a solo (the StarScore bar): "new" (a new one, tied to the song), "convert" (the open score, an old solo
    // kept in a song, made into a Solo Transcription file: no instrument, key or bars to ask)
    property string soloMode: ""
    property string soloName: ""
    readonly property bool converting: root.soloMode === "convert"

    Component.onCompleted: {
        if (root.soloMode !== "") {
            root.page = 2
            if (root.converting) {
                root.soloSong = newModel.openTitle
            }
            Qt.callLater(function() {
                soloSongField.ensureActiveFocus()
            })
        }
    }

    function chooseSolo() {
        root.page = 2
        soloSongField.ensureActiveFocus()
    }

    function createSolo() {
        if (root.soloSong.trim() === "" || root.soloPlayer.trim() === "") {
            return
        }
        if (newModel.createSolo({
            "song": root.soloSong,
            "player": root.soloPlayer,
            "band": root.soloBand,
            "recording": root.soloRecording,
            "link": root.soloLink,
            "instrumentId": root.soloInstrument,
            "tiedSongCode": root.soloTiedCode,
            "keyFifths": root.keyFifths,
            "timeSigNumerator": root.timeNum,
            "timeSigDenominator": root.timeDen,
            "tempoBpm": root.tempo,
            "measures": root.measures
        }, root.converting)) {
            root.ret = { "errcode": 0, "value": "created" }
            root.hide()
        }
    }

    function chooseMuseScore() {
        root.ret = { "errcode": 0, "value": "musescore-wizard" }
        root.hide()
    }

    function create() {
        if (root.scoreTitle.trim() === "") {   // (Enter in a field with no title yet)
            return
        }
        if (newModel.create({
            "title": root.scoreTitle,
            "subtitle": root.subtitle,
            "composer": root.composer,
            "keyFifths": root.keyFifths,
            "timeSigNumerator": root.timeNum,
            "timeSigDenominator": root.timeDen,
            "tempoBpm": root.tempo,
            "measures": root.measures,
            "arrangementTemplateKey": root.arrangementKey,
            "doublerInstrumentId": root.doublerId,
            "lowHornInstrumentId": root.lowHornId,
            "songKind": root.songKind
        })) {
            root.ret = { "errcode": 0, "value": "created" }
            root.hide()
        }
    }

    // --- Page 0: which kind of score ---
    ColumnLayout {
        anchors.fill: parent
        visible: root.page === 0
        spacing: 16

        StyledTextLabel {
            Layout.fillWidth: true
            text: qsTrc("starscore", "What kind of score?")
            font: ui.theme.bodyBoldFont
            horizontalAlignment: Text.AlignLeft
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 16

            FlatButton {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: qsTrc("starscore", "Starsign Score")
                toolTipTitle: qsTrc("starscore", "A .starscore with a Starsign horn arrangement, lead sheet, and rhythm section")
                accentButton: true
                navigation.panel: choicePanel
                navigation.order: 1
                onClicked: root.chooseStarsign()
            }

            FlatButton {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: qsTrc("starscore", "Non-Starsign Score")
                toolTipTitle: qsTrc("starscore", "Any other score, set up in MuseScore's own new-score wizard")
                navigation.panel: choicePanel
                navigation.order: 2
                onClicked: root.chooseMuseScore()
            }

            FlatButton {
                Layout.fillWidth: true
                Layout.fillHeight: true
                text: qsTrc("starscore", "Solo Transcription")
                toolTipTitle: qsTrc("starscore", "A transcribed solo in all twelve keys, kept in Projects and Sheets/8 Solo Transcriptions")
                navigation.panel: choicePanel
                navigation.order: 3
                onClicked: root.chooseSolo()
            }
        }

        NavigationPanel {
            id: choicePanel
            name: "NewScoreChoice"
            section: root.navigationSection
            order: 1
            direction: NavigationPanel.Horizontal
        }

        ButtonBox {
            Layout.fillWidth: true

            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) {
                    root.reject()
                }
            }
        }
    }

    // --- Page 1: the StarScore ---
    ColumnLayout {
        anchors.fill: parent
        visible: root.page === 1
        spacing: 10

        StyledTextLabel { text: qsTrc("starscore", "Type"); font: ui.theme.bodyBoldFont }
        StyledDropdown {
            Layout.fillWidth: true
            model: [
                { "text": qsTrc("starscore", "Original (saved in Originals › Sketches)"), "value": "original" },
                { "text": qsTrc("starscore", "Cover (saved in Covers › In Progress)"), "value": "cover" },
                { "text": qsTrc("starscore", "Vocal Cover (saved in Vocal Covers › In Progress, with a Voice part)"), "value": "vocal" }
            ]
            currentIndex: indexOfValue(root.songKind)
            onActivated: function(index, value) { root.songKind = value }
        }

        StyledTextLabel { text: qsTrc("starscore", "Title"); font: ui.theme.bodyBoldFont }
        TextInputField {
            id: titleField
            Layout.fillWidth: true
            hint: qsTrc("starscore", "Song title")
            onTextEdited: function(t) { root.scoreTitle = t }
            onAccepted: root.create()
        }

        StyledTextLabel { text: qsTrc("starscore", "Subtitle"); font: ui.theme.bodyBoldFont }
        TextInputField {
            Layout.fillWidth: true
            hint: qsTrc("starscore", "Subtitle (optional)")
            onTextEdited: function(t) { root.subtitle = t }
            onAccepted: root.create()
        }

        StyledTextLabel { text: qsTrc("starscore", "Composer"); font: ui.theme.bodyBoldFont }
        TextInputField {
            Layout.fillWidth: true
            hint: qsTrc("starscore", "Composer")
            onTextEdited: function(t) { root.composer = t }
            onAccepted: root.create()
        }

        StyledTextLabel { text: qsTrc("starscore", "Starting arrangement"); font: ui.theme.bodyBoldFont }
        StyledDropdown {
            Layout.fillWidth: true
            model: newModel.arrangementTemplates
            currentIndex: indexOfValue(root.arrangementKey)
            onActivated: function(index, value) {
                root.arrangementKey = value
                // the doubler's usual instrument differs between 3/4/5-Horn (alto) and 6/7-Horn (soprano)
                root.doublerId = newModel.defaultDoublerId(value)
            }
        }

        // Standard arrangements of 3 or more horns: what the woodwind doubler plays
        StyledTextLabel {
            visible: root.showDoubler
            text: qsTrc("starscore", "Woodwind doubler plays"); font: ui.theme.bodyBoldFont
        }
        StyledDropdown {
            id: doublerDropdown
            visible: root.showDoubler
            Layout.fillWidth: true
            model: newModel.doublerChoices(root.arrangementKey)
            currentIndex: indexOfValue(root.doublerId)
            onActivated: function(index, value) { root.doublerId = value }
        }

        // 7-Horn Standard: the 7th horn (its stand-in versions on the other low horns are made later)
        StyledTextLabel {
            visible: root.showLowHorn
            text: qsTrc("starscore", "Preferred 7th horn"); font: ui.theme.bodyBoldFont
        }
        StyledDropdown {
            visible: root.showLowHorn
            Layout.fillWidth: true
            model: newModel.lowHornChoices(root.doublerId)
            currentIndex: indexOfValue(root.lowHornId)
            onActivated: function(index, value) { root.lowHornId = value }
        }

        StyledTextLabel { text: qsTrc("starscore", "Key signature"); font: ui.theme.bodyBoldFont }
        StyledDropdown {
            Layout.fillWidth: true
            model: newModel.keys
            currentIndex: indexOfValue(root.keyFifths)
            onActivated: function(index, value) { root.keyFifths = value }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            ColumnLayout {
                StyledTextLabel { text: qsTrc("starscore", "Time signature"); font: ui.theme.bodyBoldFont }
                RowLayout {
                    IncrementalPropertyControl {
                        Layout.preferredWidth: 64
                        currentValue: root.timeNum
                        step: 1; decimals: 0; minValue: 1; maxValue: 63
                        onValueEdited: function(v) { root.timeNum = v }
                    }
                    StyledTextLabel { text: "/" }
                    StyledDropdown {
                        Layout.preferredWidth: 64
                        model: [ { "text": "1", "value": 1 }, { "text": "2", "value": 2 }, { "text": "4", "value": 4 },
                                 { "text": "8", "value": 8 }, { "text": "16", "value": 16 }, { "text": "32", "value": 32 } ]
                        currentIndex: indexOfValue(root.timeDen)
                        onActivated: function(index, value) { root.timeDen = value }
                    }
                }
            }

            ColumnLayout {
                // the time signature's beat (6/8: dotted quarter; 2/2: half; 7/8: eighth), as the tempo marking counts
                StyledTextLabel { text: root.tempoLabel(root.timeNum, root.timeDen); font: ui.theme.bodyBoldFont }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: root.tempo
                    step: 1; decimals: 0; minValue: 20; maxValue: 400
                    onValueEdited: function(v) { root.tempo = v }
                }
            }

            ColumnLayout {
                StyledTextLabel { text: qsTrc("starscore", "Bars"); font: ui.theme.bodyBoldFont }
                IncrementalPropertyControl {
                    Layout.preferredWidth: 80
                    currentValue: root.measures
                    step: 1; decimals: 0; minValue: 1; maxValue: 999
                    onValueEdited: function(v) { root.measures = v }
                }
            }
        }

        Item { Layout.fillHeight: true }

        ButtonBox {
            Layout.fillWidth: true

            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 3

            FlatButton {
                text: qsTrc("starscore", "Back")
                buttonRole: ButtonBoxModel.BackRole
                buttonId: ButtonBoxModel.CustomButton + 2
                onClicked: root.page = 0
            }

            FlatButton {
                text: qsTrc("starscore", "Create")
                accentButton: true
                enabled: root.scoreTitle.trim() !== ""
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: root.create()
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) {
                    root.reject()
                }
            }
        }
    }

    // --- Page 2: the Solo Transcription ---
    StyledFlickable {
        anchors.fill: parent
        visible: root.page === 2
        contentHeight: soloColumn.implicitHeight
        clip: true

        ColumnLayout {
            id: soloColumn
            width: parent.width
            spacing: 10

            StyledTextLabel { text: qsTrc("starscore", "Song"); font: ui.theme.bodyBoldFont }
            TextInputField {
                id: soloSongField
                Layout.fillWidth: true
                hint: qsTrc("starscore", "Song title")
                currentText: root.soloSong
                onTextEdited: function(t) { root.soloSong = t }
                onAccepted: root.createSolo()
            }

            StyledTextLabel { text: qsTrc("starscore", "Tied to a Starsign song"); font: ui.theme.bodyBoldFont }
            StyledDropdown {
                Layout.fillWidth: true
                model: newModel.songsForTying
                currentIndex: Math.max(0, indexOfValue(root.soloTiedCode))
                onActivated: function(index, value) { root.soloTiedCode = value }
            }

            StyledTextLabel { text: qsTrc("starscore", "Player"); font: ui.theme.bodyBoldFont }
            TextInputField {
                Layout.fillWidth: true
                hint: qsTrc("starscore", "Who plays the solo")
                onTextEdited: function(t) { root.soloPlayer = t }
                onAccepted: root.createSolo()
            }

            StyledTextLabel { text: qsTrc("starscore", "Band"); font: ui.theme.bodyBoldFont }
            TextInputField {
                Layout.fillWidth: true
                hint: qsTrc("starscore", "Band (optional)")
                onTextEdited: function(t) { root.soloBand = t }
                onAccepted: root.createSolo()
            }

            StyledTextLabel { text: qsTrc("starscore", "Album / recording"); font: ui.theme.bodyBoldFont }
            TextInputField {
                Layout.fillWidth: true
                hint: qsTrc("starscore", "Album or recording (optional)")
                onTextEdited: function(t) { root.soloRecording = t }
                onAccepted: root.createSolo()
            }

            StyledTextLabel { text: qsTrc("starscore", "Link"); font: ui.theme.bodyBoldFont }
            TextInputField {
                Layout.fillWidth: true
                hint: qsTrc("starscore", "YouTube or other link (optional)")
                onTextEdited: function(t) { root.soloLink = t }
                onAccepted: root.createSolo()
            }

            StyledTextLabel { visible: !root.converting; text: qsTrc("starscore", "Instrument"); font: ui.theme.bodyBoldFont }
            StyledDropdown {
                visible: !root.converting
                Layout.fillWidth: true
                model: newModel.soloInstruments
                currentIndex: indexOfValue(root.soloInstrument)
                onActivated: function(index, value) { root.soloInstrument = value }
            }

            StyledTextLabel { visible: !root.converting; text: qsTrc("starscore", "Key signature (concert)"); font: ui.theme.bodyBoldFont }
            StyledDropdown {
                visible: !root.converting
                Layout.fillWidth: true
                model: newModel.keys
                currentIndex: indexOfValue(root.keyFifths)
                onActivated: function(index, value) { root.keyFifths = value }
            }

            RowLayout {
                visible: !root.converting
                Layout.fillWidth: true
                spacing: 12

                ColumnLayout {
                    StyledTextLabel { text: qsTrc("starscore", "Time signature"); font: ui.theme.bodyBoldFont }
                    RowLayout {
                        IncrementalPropertyControl {
                            Layout.preferredWidth: 64
                            currentValue: root.timeNum
                            step: 1; decimals: 0; minValue: 1; maxValue: 63
                            onValueEdited: function(v) { root.timeNum = v }
                        }
                        StyledTextLabel { text: "/" }
                        StyledDropdown {
                            Layout.preferredWidth: 64
                            model: [ { "text": "1", "value": 1 }, { "text": "2", "value": 2 }, { "text": "4", "value": 4 },
                                     { "text": "8", "value": 8 }, { "text": "16", "value": 16 }, { "text": "32", "value": 32 } ]
                            currentIndex: indexOfValue(root.timeDen)
                            onActivated: function(index, value) { root.timeDen = value }
                        }
                    }
                }

                ColumnLayout {
                    StyledTextLabel { text: root.tempoLabel(root.timeNum, root.timeDen); font: ui.theme.bodyBoldFont }
                    IncrementalPropertyControl {
                        Layout.preferredWidth: 80
                        currentValue: root.tempo
                        step: 1; decimals: 0; minValue: 20; maxValue: 400
                        onValueEdited: function(v) { root.tempo = v }
                    }
                }

                ColumnLayout {
                    StyledTextLabel { text: qsTrc("starscore", "Bars"); font: ui.theme.bodyBoldFont }
                    IncrementalPropertyControl {
                        Layout.preferredWidth: 80
                        currentValue: root.measures
                        step: 1; decimals: 0; minValue: 1; maxValue: 999
                        onValueEdited: function(v) { root.measures = v }
                    }
                }
            }

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                color: ui.theme.fontSecondaryColor
                text: root.converting
                      ? qsTrc("starscore", "Copies the solo that's open (%1) into a Solo Transcription file of its own, with twelve part "
                              + "scores, one in each key. Only the soloist's part is copied; the solo in the song stays as it is. "
                              + "Saved in Projects and Sheets/8 Solo Transcriptions.").arg(root.soloName || qsTrc("starscore", "the open score"))
                      : qsTrc("starscore", "Makes the solo's score and twelve part scores, one in each key (the original first). What you "
                              + "write in the score shows in all twelve; fingerings stay in the part score they're written in. Saved in "
                              + "Projects and Sheets/8 Solo Transcriptions.")
            }

            ButtonBox {
                Layout.fillWidth: true

                buttons: [ ButtonBoxModel.Cancel ]

                navigationPanel.section: root.navigationSection
                navigationPanel.order: 4

                FlatButton {
                    visible: root.soloMode === ""
                    text: qsTrc("starscore", "Back")
                    buttonRole: ButtonBoxModel.BackRole
                    buttonId: ButtonBoxModel.CustomButton + 4
                    onClicked: root.page = 0
                }

                FlatButton {
                    text: root.converting ? qsTrc("starscore", "Make") : qsTrc("starscore", "Create")
                    accentButton: true
                    enabled: root.soloSong.trim() !== "" && root.soloPlayer.trim() !== ""
                    buttonRole: ButtonBoxModel.ApplyRole
                    buttonId: ButtonBoxModel.CustomButton + 3
                    onClicked: root.createSolo()
                }

                onStandardButtonClicked: function(buttonId) {
                    if (buttonId === ButtonBoxModel.Cancel) {
                        root.reject()
                    }
                }
            }
        }
    }
}
