/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Add arrangement
 *
 * The StarScore bar's Add arrangement menu, for a Standard arrangement of 3 or more horns whose horn section the score
 * doesn't have yet: the same questions as File › New (what the woodwind doubler plays; the 7th horn of a 7-Horn).
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    property string arrangementKey: ""

    title: qsTrc("starscore", "Add %1").arg(newModel.arrangementName(root.arrangementKey))

    contentWidth: 460
    contentHeight: root.showLowHorn ? 250 : 170
    margins: 20

    NewStarScoreModel {
        id: newModel
    }

    property string doublerId: newModel.defaultDoublerId(root.arrangementKey)
    property string lowHornId: newModel.lastLowHornId
    // with Ben on Bass Clarinet, the 7th horn can't be a second bass clarinet
    onDoublerIdChanged: {
        if (root.doublerId === "bb-bass-clarinet" && root.lowHornId === "bb-bass-clarinet") {
            root.lowHornId = "bass-trombone"
        }
    }
    property bool showLowHorn: newModel.hasLowHorn(root.arrangementKey)

    function add() {
        if (newModel.addArrangement(root.arrangementKey, root.doublerId, root.lowHornId)) {
            root.ret = { "errcode": 0, "value": "created" }
            root.hide()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel { text: qsTrc("starscore", "Woodwind doubler plays"); font: ui.theme.bodyBoldFont }
        StyledDropdown {
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

        Item { Layout.fillHeight: true }

        ButtonBox {
            Layout.fillWidth: true

            buttons: [ ButtonBoxModel.Cancel ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 1

            FlatButton {
                text: qsTrc("starscore", "Add")
                accentButton: true
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: root.add()
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Cancel) {
                    root.reject()
                }
            }
        }
    }
}
