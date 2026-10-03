/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

pragma ComponentBehavior: Bound

import QtQuick

Item {
    id: root

    property var model: null
    property int orientation: Qt.Horizontal
    readonly property bool isHorizontal: orientation === Qt.Horizontal

    property Component sectionDelegate: Item {}
    property Component itemDelegate: Item {}

    property alias contentWidth: loader.width
    property alias contentHeight: loader.height

    property int cellWidth: 0
    property int cellHeight: 0

    property int sectionWidth: 0
    property int sectionHeight: 0
    property string sectionRole: "sectionRole"

    readonly property int noLimit: -1
    property int rows: noLimit
    property int rowSpacing: 2
    property int columns: noLimit
    property int columnSpacing: 2

    //! StarScore: when set (horizontal only), the sections wrap onto further lines within this width
    property int wrapWidth: 0
    readonly property bool wrapping: root.isHorizontal && root.wrapWidth > 0

    //! The width the sections take on a single line (horizontal, one row per section); kept up to date
    readonly property int singleLineWidth: {
        var n = root.model ? root.model.length : 0 // updates when the items change
        return n >= 0 ? root.unwrappedWidth() : 0
    }

    function unwrappedWidth() {
        if (!root.model) {
            return 0
        }
        var counts = {}
        var order = []
        for (var i = 0; i < root.model.length; i++) {
            var section = root.model.get(i)[root.sectionRole]
            if (counts[section] === undefined) {
                counts[section] = 0
                order.push(section)
            }
            counts[section]++
        }
        var result = 0
        for (var j = 0; j < order.length; j++) {
            var n = counts[order[j]]
            result += root.sectionWidth + privateProperties.spacingAfterSection
                    + n * root.cellWidth + Math.max(0, n - 1) * root.columnSpacing
        }
        return result + Math.max(0, order.length - 1) * privateProperties.spacingBeforeSection
    }

    QtObject {
        id: privateProperties

        property int spacingBeforeSection: root.isHorizontal ? root.columnSpacing : root.rowSpacing
        property int spacingAfterSection: spacingBeforeSection

        function modelSections() {
            var _sections = []

            for (var i = 0; i < root.model.length; i++) {
                var element = root.model.get(i)

                var section = element[root.sectionRole]
                if (!_sections.includes(section)) {
                    _sections.push(section)
                }
            }

            return _sections
        }
    }

    Loader {
        id: loader

        sourceComponent: root.wrapping ? wrappedView : root.isHorizontal ? horizontalView : verticalView
    }

    Component {
        id: wrappedView

        Flow {
            width: root.wrapWidth

            spacing: privateProperties.spacingBeforeSection

            Repeater {
                model: Boolean(root.model) ? privateProperties.modelSections() : []

                Row {
                    id: wrappedRow

                    required property var modelData
                    required property int index

                    spacing: privateProperties.spacingAfterSection

                    height: root.sectionHeight

                    GridViewSection {
                        width: root.sectionWidth
                        height: root.sectionHeight

                        sectionDelegate: root.sectionDelegate
                        itemModel: wrappedRow.modelData
                        index: wrappedRow.index
                    }

                    GridViewDelegate {
                        anchors.verticalCenter: parent.verticalCenter

                        model: root.model

                        itemDelegate: root.itemDelegate
                        sectionRole: root.sectionRole
                        sectionValue: wrappedRow.modelData

                        cellWidth: root.cellWidth
                        cellHeight: root.cellHeight

                        rows: 1
                        rowSpacing: root.rowSpacing
                        columns: root.noLimit
                        columnSpacing: root.columnSpacing
                    }
                }
            }
        }
    }

    Component {
        id: horizontalView

        Row {
            spacing: privateProperties.spacingBeforeSection

            height: childrenRect.height

            Repeater {
                model: Boolean(root.model) ? privateProperties.modelSections() : []

                Row {
                    id: delegateRow

                    required property var modelData
                    required property int index

                    spacing: privateProperties.spacingAfterSection

                    height: root.sectionHeight

                    GridViewSection {
                        width: root.sectionWidth
                        height: root.sectionHeight

                        sectionDelegate: root.sectionDelegate
                        itemModel: delegateRow.modelData
                        index: delegateRow.index
                    }

                    GridViewDelegate {
                        anchors.verticalCenter: parent.verticalCenter

                        model: root.model

                        itemDelegate: root.itemDelegate
                        sectionRole: root.sectionRole
                        sectionValue: delegateRow.modelData

                        cellWidth: root.cellWidth
                        cellHeight: root.cellHeight

                        rows: root.rows
                        rowSpacing: root.rowSpacing
                        columns: root.columns
                        columnSpacing: root.columnSpacing
                    }
                }
            }
        }
    }

    Component {
        id: verticalView

        Column {
            spacing: privateProperties.spacingBeforeSection

            width: childrenRect.width

            Repeater {
                model: Boolean(root.model) ? privateProperties.modelSections() : []

                Column {
                    id: delegateColumn

                    required property var modelData
                    required property int index

                    spacing: privateProperties.spacingAfterSection

                    width: root.sectionWidth

                    GridViewSection {
                        width: root.sectionWidth
                        height: root.sectionHeight

                        sectionDelegate: root.sectionDelegate
                        itemModel: delegateColumn.modelData
                        index: delegateColumn.index
                    }

                    GridViewDelegate {
                        anchors.horizontalCenter: parent.horizontalCenter

                        model: root.model

                        itemDelegate: root.itemDelegate
                        sectionRole: root.sectionRole
                        sectionValue: delegateColumn.modelData

                        cellWidth: root.cellWidth
                        cellHeight: root.cellHeight

                        rows: root.rows
                        rowSpacing: root.rowSpacing
                        columns: root.columns
                        columnSpacing: root.columnSpacing
                    }
                }
            }
        }
    }
}
