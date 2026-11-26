/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — (1.40.0) Song settings: the song's version number, copyright year and where it's kept.
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Song settings")

    contentWidth: 420
    contentHeight: 300
    margins: 16

    SongSettingsModel {
        id: settingsModel
    }

    Component.onCompleted: {
        settingsModel.load()
    }

    function applyAndClose() {
        if (settingsModel.apply()) {
            root.ret = { "errcode": 0 }
            root.hide()
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        StyledTextLabel {
            text: qsTrc("starscore", "Version number")
            font: ui.theme.bodyBoldFont
            horizontalAlignment: Text.AlignLeft
        }
        TextInputField {
            Layout.fillWidth: true
            currentText: settingsModel.version
            hint: qsTrc("starscore", "e.g. 4.0.1 (printed at the foot of the score and every part)")
            onTextEdited: function(newTextValue) { settingsModel.version = newTextValue }
            onAccepted: root.applyAndClose()
        }

        StyledTextLabel {
            text: settingsModel.hasCopyrightYear ? qsTrc("starscore", "Copyright year")
                                                 : qsTrc("starscore", "Copyright year (none: a cover)")
            font: ui.theme.bodyBoldFont
            horizontalAlignment: Text.AlignLeft
        }
        TextInputField {
            Layout.fillWidth: true
            enabled: settingsModel.hasCopyrightYear
            currentText: settingsModel.copyrightYear
            onTextEdited: function(newTextValue) { settingsModel.copyrightYear = newTextValue }
            onAccepted: root.applyAndClose()
        }

        StyledTextLabel {
            text: settingsModel.canMove ? qsTrc("starscore", "Kept in")
                                        : qsTrc("starscore", "Kept in (only for songs in 1 Starsign)")
            font: ui.theme.bodyBoldFont
            horizontalAlignment: Text.AlignLeft
        }
        StyledDropdown {
            Layout.fillWidth: true
            enabled: settingsModel.canMove
            model: settingsModel.groups
            currentIndex: settingsModel.groupIndex
            onActivated: function(index, value) { settingsModel.groupIndex = index }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: settingsModel.problem !== ""
            text: settingsModel.problem
            color: ui.theme.accentColor
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
        }

        Item { Layout.fillHeight: true }

        ButtonBox {
            Layout.fillWidth: true
            buttons: [ ButtonBoxModel.Cancel, ButtonBoxModel.Ok ]
            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Ok) {
                    root.applyAndClose()
                } else if (buttonId === ButtonBoxModel.Cancel) {
                    root.reject()
                }
            }
        }
    }
}
