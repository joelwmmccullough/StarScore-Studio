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
    // pages are drawn at this zoom; while pinching, the drawn pages are stretched and redrawn when the pinch ends
    property real renderZoom: 1.0
    property bool pinching: false
    onZoomChanged: if (!root.pinching) root.renderZoom = root.zoom

    // the list of reference PDFs (rename, instrument, order, delete) instead of the pages
    property bool managing: false

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
                icon: IconCode.SETTINGS_COG
                toolTipTitle: qsTrc("starscore", "Manage reference PDFs")
                toolTipDescription: qsTrc("starscore", "Rename, mark the instrument, reorder or delete")
                accentButton: root.managing
                onClicked: root.managing = !root.managing
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
            visible: refModel.pageCount === 0 && !(root.managing && refModel.references.length > 0)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
            text: refModel.references.length === 0
                  ? qsTrc("starscore", "No reference PDFs in this score yet. Press + to add one (for example the original chart of a cover). They are saved inside the .starscore.")
                  : qsTrc("starscore", "This PDF can't be shown. Save the score and open it again, or use “Open in another app”.")
        }

        StyledListView {
            id: manageList
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.managing && refModel.references.length > 0
            spacing: 6
            clip: true
            model: refModel.references

            delegate: Rectangle {
                id: row
                required property var modelData
                required property int index

                width: manageList.width
                height: rowColumn.implicitHeight + 12
                radius: 4
                color: modelData.id === refModel.currentId ? ui.theme.accentColor : ui.theme.backgroundSecondaryColor
                opacity: modelData.id === refModel.currentId ? 1.0 : 0.95

                ColumnLayout {
                    id: rowColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.margins: 6
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        TextInputField {
                            Layout.fillWidth: true
                            currentText: row.modelData.name
                            hint: qsTrc("starscore", "Name")
                            onTextEditingFinished: function(newTextValue) {
                                if (newTextValue !== row.modelData.name) {
                                    refModel.rename(row.modelData.id, newTextValue)
                                }
                            }
                        }
                        FlatButton {
                            icon: IconCode.ARROW_UP
                            toolTipTitle: qsTrc("starscore", "Move up")
                            enabled: row.index > 0
                            onClicked: refModel.move(row.modelData.id, -1)
                        }
                        FlatButton {
                            icon: IconCode.ARROW_DOWN
                            toolTipTitle: qsTrc("starscore", "Move down")
                            enabled: row.index < refModel.references.length - 1
                            onClicked: refModel.move(row.modelData.id, 1)
                        }
                        FlatButton {
                            icon: IconCode.DELETE_TANK
                            toolTipTitle: qsTrc("starscore", "Delete")
                            onClicked: refModel.remove(row.modelData.id)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4

                        StyledDropdown {
                            Layout.fillWidth: true
                            model: refModel.instrumentChoices
                            currentIndex: indexOfValue(row.modelData.instrument)
                            onActivated: function(index, value) { refModel.setInstrument(row.modelData.id, value) }
                        }
                        FlatButton {
                            text: qsTrc("starscore", "Show")
                            onClicked: {
                                refModel.selectId(row.modelData.id)
                                root.managing = false
                            }
                        }
                    }
                }
            }
        }

        Flickable {
            id: flick
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: refModel.pageCount > 0 && !root.managing
            clip: true
            contentWidth: pages.width
            contentHeight: pages.height
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: StyledScrollBar {}
            ScrollBar.horizontal: StyledScrollBar {}

            readonly property int pageWidth: Math.max(100, Math.round((flick.width - 12) * root.zoom))
            readonly property int renderWidth: Math.max(100, Math.round((flick.width - 12) * root.renderZoom))

            // Trackpad pinch to zoom, keeping the point under the cursor in place
            PinchHandler {
                id: pinch
                target: null
                property real startZoom: 1.0
                property real anchorX: 0      // content point under the cursor when the pinch began (at startZoom)
                property real anchorY: 0

                function viewportPoint() {
                    return flick.mapFromItem(null, pinch.centroid.scenePosition.x, pinch.centroid.scenePosition.y)
                }

                onActiveChanged: {
                    if (active) {
                        startZoom = root.zoom
                        const p = viewportPoint()
                        anchorX = flick.contentX + p.x
                        anchorY = flick.contentY + p.y
                        root.pinching = true
                    } else {
                        root.pinching = false
                        root.renderZoom = root.zoom
                        flick.returnToBounds()
                    }
                }
                onActiveScaleChanged: {
                    if (!active) {
                        return
                    }
                    root.zoom = Math.max(0.4, Math.min(4.0, startZoom * activeScale))
                    const r = root.zoom / startZoom
                    const p = viewportPoint()
                    flick.contentX = Math.max(0, Math.min(anchorX * r - p.x, flick.contentWidth - flick.width))
                    flick.contentY = Math.max(0, Math.min(anchorY * r - p.y, flick.contentHeight - flick.height))
                }
            }

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
                            source: refModel.currentId !== "" && (refModel.invert || !refModel.invert) ? refModel.pageUrl(parent.index, Math.round(flick.renderWidth * Screen.devicePixelRatio)) : ""
                        }
                    }
                }
            }
        }
    }
}
