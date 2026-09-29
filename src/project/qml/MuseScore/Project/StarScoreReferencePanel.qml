/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Reference PDF panel: the song's reference PDFs beside the working score.
 * Scroll through the pages; zoom with the buttons (or fit the panel width).
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

Item {
    id: root

    property NavigationSection navigationSection: null
    property int navigationOrderStart: 1

    // 1.0 = fit the panel width
    property real zoom: 1.0

    ReferencePanelModel {
        id: refModel
    }

    Component.onCompleted: refModel.load()

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            StyledDropdown {
                Layout.fillWidth: true
                visible: refModel.references.length > 0
                model: refModel.references
                currentIndex: refModel.currentIndex
                onActivated: function(index, value) { refModel.selectIndex(index) }
            }

            FlatButton {
                icon: IconCode.ZOOM_OUT
                toolTipTitle: qsTrc("starscore", "Zoom out")
                enabled: refModel.pageCount > 0
                onClicked: root.zoom = Math.max(0.4, root.zoom - 0.2)
            }
            FlatButton {
                icon: IconCode.ZOOM_IN
                toolTipTitle: qsTrc("starscore", "Zoom in")
                enabled: refModel.pageCount > 0
                onClicked: root.zoom = Math.min(4.0, root.zoom + 0.2)
            }
            FlatButton {
                text: qsTrc("starscore", "Fit")
                toolTipTitle: qsTrc("starscore", "Fit the panel width")
                enabled: refModel.pageCount > 0
                onClicked: root.zoom = 1.0
            }
            FlatButton {
                text: qsTrc("starscore", "Invert")
                toolTipTitle: qsTrc("starscore", "Invert colors")
                toolTipDescription: qsTrc("starscore", "White on black, to match the dark theme. Saved for each PDF; exports are never inverted.")
                enabled: refModel.currentId !== ""
                accentButton: refModel.invert
                onClicked: refModel.setInvert(!refModel.invert)
            }
            FlatButton {
                icon: IconCode.PLUS
                toolTipTitle: qsTrc("starscore", "Add reference PDF…")
                onClicked: refModel.addReference()
            }
            FlatButton {
                icon: IconCode.OPEN_FILE
                toolTipTitle: qsTrc("starscore", "Open in another app")
                enabled: refModel.currentId !== ""
                onClicked: refModel.openInViewer()
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: refModel.pageCount === 0
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            text: refModel.references.length === 0
                  ? qsTrc("starscore", "No reference PDFs in this score yet. Press + to add one (for example the original chart of a cover). They are saved inside the .starscore.")
                  : qsTrc("starscore", "This PDF can't be shown. Save the score and open it again, or use “Open in another app”.")
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: refModel.pageCount > 0
            clip: true
            contentWidth: pages.width
            contentHeight: pages.height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: StyledScrollBar {}
            ScrollBar.horizontal: StyledScrollBar {}

            readonly property int pageWidth: Math.max(100, Math.round((flick.width - 12) * root.zoom))

            Column {
                id: pages
                spacing: 8

                Repeater {
                    model: refModel.pageCount

                    delegate: Rectangle {
                        required property int index
                        width: flick.pageWidth
                        height: img.status === Image.Ready ? img.paintedHeight : Math.round(width * 1.294)
                        color: refModel.invert ? "black" : "white"
                        border.width: 1
                        border.color: ui.theme.strokeColor

                        Image {
                            id: img
                            width: parent.width
                            fillMode: Image.PreserveAspectFit
                            asynchronous: true
                            cache: false
                            smooth: true
                            mipmap: true
                            source: refModel.currentId !== "" && (refModel.invert || !refModel.invert) ? refModel.pageUrl(parent.index, Math.round(parent.width * Screen.devicePixelRatio)) : ""
                        }
                    }
                }
            }
        }
    }
}
