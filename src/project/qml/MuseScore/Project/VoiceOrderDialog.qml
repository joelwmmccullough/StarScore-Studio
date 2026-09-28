/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Check voice order: within each section, whether each part stays above the parts it
 * should be above. Red = crossed, yellow = crossed where that is allowed but not preferred,
 * purple = doubling (same pitch), grey = in order, pale = the two parts don't play together.
 */
import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Check voice order")

    contentWidth: 980
    contentHeight: 640
    margins: 16

    readonly property color okColor: ui.theme.buttonColor
    readonly property color crossColor: "#E0463A"
    readonly property color softColor: "#E8C22B"
    readonly property color doubleColor: "#A56BFF"
    readonly property color emptyColor: Qt.rgba(ui.theme.fontPrimaryColor.r, ui.theme.fontPrimaryColor.g, ui.theme.fontPrimaryColor.b, 0.06)

    function cellColor(v) {
        return v === 1 ? crossColor : v === 3 ? softColor : v === 2 ? doubleColor : v === 0 ? okColor : emptyColor
    }

    VoiceOrderModel {
        id: orderModel
    }

    Component.onCompleted: orderModel.load()

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 14

            Repeater {
                model: [
                    { "c": root.crossColor, "t": qsTrc("starscore", "Crossed") },
                    { "c": root.softColor, "t": qsTrc("starscore", "Crossed (allowed, not preferred)") },
                    { "c": root.doubleColor, "t": qsTrc("starscore", "Doubling") },
                    { "c": root.okColor, "t": qsTrc("starscore", "In order") },
                    { "c": root.emptyColor, "t": qsTrc("starscore", "Not playing together") }
                ]
                delegate: RowLayout {
                    required property var modelData
                    spacing: 5
                    Rectangle { width: 14; height: 14; color: modelData.c; border.width: 1; border.color: ui.theme.strokeColor }
                    StyledTextLabel { text: modelData.t }
                }
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignLeft
            wrapMode: Text.WordWrap
            color: ui.theme.fontSecondaryColor
            text: orderModel.sections.length === 0
                  ? qsTrc("starscore", "No section has two or more parts with an expected order.")
                  : qsTrc("starscore", "Compares the highest sounding note of each part wherever both play. Click a bar to select it in the main score.")
        }

        StyledFlickable {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentHeight: sectionsColumn.height
            clip: true

            Column {
                id: sectionsColumn
                width: parent.width
                spacing: 18

                Repeater {
                    model: orderModel.sections

                    delegate: Column {
                        id: sec
                        required property var modelData
                        width: sectionsColumn.width
                        spacing: 6

                        StyledTextLabel {
                            text: sec.modelData.section
                            font: ui.theme.largeBodyBoldFont
                        }

                        Repeater {
                            model: sec.modelData.rules

                            delegate: RowLayout {
                                id: row
                                required property var modelData
                                width: sec.width
                                spacing: 10

                                Column {
                                    Layout.preferredWidth: 300
                                    spacing: 2
                                    StyledTextLabel {
                                        width: 300
                                        horizontalAlignment: Text.AlignLeft
                                        wrapMode: Text.WordWrap
                                        font: ui.theme.bodyBoldFont
                                        text: row.modelData.label
                                    }
                                    StyledTextLabel {
                                        width: 300
                                        horizontalAlignment: Text.AlignLeft
                                        wrapMode: Text.WordWrap
                                        color: ui.theme.fontSecondaryColor
                                        text: row.modelData.summary
                                    }
                                }

                                Item {
                                    id: strip
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 22
                                    readonly property real cell: width / Math.max(1, row.modelData.bars.length)

                                    Row {
                                        anchors.fill: parent
                                        Repeater {
                                            model: row.modelData.bars
                                            delegate: Rectangle {
                                                required property var modelData
                                                required property int index
                                                width: strip.cell
                                                height: strip.height
                                                color: root.cellColor(modelData)
                                                border.width: strip.cell > 6 ? 1 : 0
                                                border.color: ui.theme.backgroundPrimaryColor

                                                MouseArea {
                                                    anchors.fill: parent
                                                    hoverEnabled: true
                                                    onClicked: orderModel.selectBar(row.modelData.upperPartId, index + 1)
                                                    ToolTip.visible: containsMouse
                                                    ToolTip.text: qsTrc("starscore", "Bar %1").arg(index + 1)
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

        ButtonBox {
            Layout.fillWidth: true
            buttons: [ ButtonBoxModel.Close ]

            navigationPanel.section: root.navigationSection
            navigationPanel.order: 2

            FlatButton {
                text: qsTrc("starscore", "Refresh")
                buttonRole: ButtonBoxModel.ApplyRole
                buttonId: ButtonBoxModel.CustomButton + 1
                onClicked: orderModel.load()
            }

            onStandardButtonClicked: function(buttonId) {
                if (buttonId === ButtonBoxModel.Close) {
                    root.hide()
                }
            }
        }
    }
}
