/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Part styles: a default style for every score, plus rules that give
 * particular part books a particular MuseScore style (.mss).
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Part styles")

    contentWidth: 720
    contentHeight: 520
    margins: 16

    StarScoreStylesModel {
        id: stylesModel
    }

    Component.onCompleted: {
        stylesModel.load()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            text: qsTrc("starscore", "Styles are MuseScore style files (.mss, made with Format → Style → Save style). "
                        + "They cover page size, margins, staff size and spacing, fonts and text styles. "
                        + "The default style is applied to the main score, each arrangement score and every part book; then the matching rule below is applied on top, and then the built-in house settings (staff size, chord symbols, title frame, version footer). "
                        + "Styles are applied when a section is created, when a new StarScore is made, and when you click Apply.")
        }

        StyledTextLabel { text: qsTrc("starscore", "Default style for all part scores"); font: ui.theme.bodyBoldFont }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                elide: Text.ElideMiddle
                text: stylesModel.defaultStyle !== "" ? stylesModel.defaultStyle : qsTrc("starscore", "None")
            }
            FlatButton { text: qsTrc("starscore", "Choose…"); onClicked: stylesModel.chooseDefaultStyle() }
            FlatButton { text: qsTrc("starscore", "Clear"); enabled: stylesModel.defaultStyle !== ""; onClicked: stylesModel.clearDefaultStyle() }
        }

        SeparatorLine {}

        RowLayout {
            Layout.fillWidth: true
            StyledTextLabel {
                Layout.fillWidth: true
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "Part book rules (a later rule wins when several match)")
                font: ui.theme.bodyBoldFont
            }
            FlatButton { text: qsTrc("starscore", "+ Add rule"); onClicked: stylesModel.addRule() }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            StyledTextLabel { Layout.preferredWidth: 200; horizontalAlignment: Text.AlignLeft; text: qsTrc("starscore", "Section"); color: ui.theme.fontSecondaryColor }
            StyledTextLabel { Layout.preferredWidth: 200; horizontalAlignment: Text.AlignLeft; text: qsTrc("starscore", "Part name (blank = all)"); color: ui.theme.fontSecondaryColor }
            StyledTextLabel { Layout.fillWidth: true; horizontalAlignment: Text.AlignLeft; text: qsTrc("starscore", "Style"); color: ui.theme.fontSecondaryColor }
        }

        StyledListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
            model: stylesModel.rules

            delegate: RowLayout {
                required property var modelData
                required property int index

                width: ListView.view.width
                spacing: 8

                StyledDropdown {
                    Layout.preferredWidth: 200
                    model: stylesModel.sectionOptions
                    currentIndex: indexOfValue(modelData.section)
                    onActivated: function(i, value) { stylesModel.setRuleSection(index, value) }
                }

                TextInputField {
                    Layout.preferredWidth: 200
                    currentText: modelData.part
                    hint: qsTrc("starscore", "e.g. Tenor Saxophone")
                    onTextEdited: function(t) { stylesModel.setRulePart(index, t) }
                }

                FlatButton {
                    Layout.fillWidth: true
                    text: modelData.missing ? qsTrc("starscore", "Missing: ") + modelData.styleName : modelData.styleName
                    onClicked: stylesModel.chooseRuleStyle(index)
                }

                FlatButton {
                    icon: IconCode.DELETE_TANK
                    transparent: true
                    onClicked: stylesModel.removeRule(index)
                }
            }
        }

        StyledTextLabel {
            id: resultLabel
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            color: ui.theme.fontSecondaryColor
        }

        ButtonBox {
            Layout.fillWidth: true

            buttons: [ ButtonBoxModel.Close ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("starscore", "Apply to open score now")
                accentButton: true
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: {
                    stylesModel.save()
                    stylesModel.applyNow()
                    root.hide()
                }
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Close) {
                    stylesModel.save()
                    root.hide()
                }
            }
        }
    }
}
