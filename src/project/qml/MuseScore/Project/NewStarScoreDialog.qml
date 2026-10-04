/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — New StarScore
 *
 * File › New: first "Starsign Score" or "Non-Starsign Score". Non-Starsign hands over to MuseScore's own new-score
 * wizard (the dialog closes with the value "musescore-wizard"; ProjectActionsController::newProject opens the wizard);
 * Starsign goes on to the StarScore form below.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "New Score")

    // 0 = the Starsign / Non-Starsign choice, 1 = the StarScore form
    property int page: 0

    contentWidth: 460
    contentHeight: root.page === 0 ? 220 : 620
    margins: 20

    NewStarScoreModel {
        id: newModel
    }

    property string scoreTitle: ""
    property string subtitle: ""
    property string composer: ""
    property int keyFifths: 0
    property int timeNum: 4
    property int timeDen: 4
    property int tempo: 120
    property int measures: 32
    // the last choices, remembered in settings
    property string arrangementKey: newModel.lastArrangementKey
    property string doublerId: newModel.lastDoublerId
    property string lowHornId: newModel.lastLowHornId

    property bool showDoubler: newModel.hasDoubler(root.arrangementKey)
    property bool showLowHorn: newModel.hasLowHorn(root.arrangementKey)

    function chooseStarsign() {
        root.page = 1
        titleField.ensureActiveFocus()
    }

    function chooseMuseScore() {
        root.ret = { "errcode": 0, "value": "musescore-wizard" }
        root.hide()
    }

    function create() {
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
            "lowHornInstrumentId": root.lowHornId
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
                toolTipTitle: qsTrc("starscore", "A .starscore with a Starsign horn arrangement, lead sheet and rhythm section")
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
            model: newModel.lowHornChoices()
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
                StyledTextLabel { text: qsTrc("starscore", "Tempo (♩ =)"); font: ui.theme.bodyBoldFont }
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
}
