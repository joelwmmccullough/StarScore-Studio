/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2023 MuseScore Limited and others
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

import Muse.Ui
import Muse.UiComponents
import Muse.Dock

Rectangle {
    id: root

    clip: true

    color: ui.theme.backgroundSecondaryColor

    property QtObject tabBarCpp: null

    property alias tabsModel: tabs.model
    property alias currentIndex: tabs.currentIndex

    property bool draggingTabsAllowed: true

    property NavigationPanel navigationPanel: null
    readonly property string currentItemNavigationName: tabs.currentItem && tabs.currentItem.navigation ? tabs.currentItem.navigation.name : ""

    signal tabClicked(int index)
    signal handleContextMenuItemRequested(string itemId)

    function updateMouseArea() {
        if (tabBarCpp) {
            tabBarCpp.setDraggableMouseArea(draggingTabsMouseArea)
            tabBarCpp.tabBarQmlItem = this
        }
    }

    function updateToolBarComponent() {
        const idx = root.tabsModel.index(root.currentIndex, 0);
        const toolBar = root.tabsModel.data(idx, DockTabsModel.ToolBarComponent)
        toolbarLoader.sourceComponent = toolBar
    }

    function doubleClicked(pos) {
        if (root.tabBarCpp) {
            root.tabBarCpp.doubleClicked(pos)
        }
    }

    Component.onDestruction: { root.tabsModel.clear() }

    onTabBarCppChanged: { updateMouseArea() }
    onDraggingTabsAllowedChanged: { updateMouseArea() }
    onCurrentIndexChanged: { updateToolBarComponent() }

    Connections {
        target: root.tabsModel
        function onDataChanged() { root.updateToolBarComponent() }
        function onModelReset() { root.updateToolBarComponent() }
    }

    ListView {
        id: tabs

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: parent.left

        width: Math.min(contentWidth, availableWidth)

        orientation: Qt.Horizontal
        interactive: false
        spacing: 0

        readonly property real availableWidth: root.width + 1  // + 1, because we don't need to see the rightmost separator

        onAvailableWidthChanged: { layoutTabs() }

        property bool layoutTabsScheduled: false

        function layoutTabs() {
            if (!layoutTabsScheduled) {
                Qt.callLater(doLayoutTabs)
                layoutTabsScheduled = true
            }
        }

        function doLayoutTabs() {
            layoutTabsScheduled = false

            let implicitWidthOfActiveTab = currentItem?.implicitWidth ?? 0
            let implicitWidthOfAllTabsTogether = 0

            for (let tab of contentItem.children) {
                implicitWidthOfAllTabsTogether += tab?.implicitWidth ?? 0
            }

            const enoughSpace = implicitWidthOfAllTabsTogether <= availableWidth;
            // StarScore Studio (1.25.0): every tab keeps at least a sliver (MIN_TAB) of its name, so it can still be seen
            // and clicked. In a narrow window the other tabs got (available - active) / … of their width, which went to
            // nothing or below once the current tab took most of the bar: only the current tab (To-do) was left, and
            // Palettes, Layout and Properties vanished (Joel, 4 Nov 2026). When even that doesn't fit, every tab,
            // the current one too, gets an equal share.
            const MIN_TAB = 28
            let others = 0
            for (let tab of contentItem.children) {
                if (tab && !tab.isCurrent && (tab.implicitWidth ?? 0) > 0) {
                    ++others
                }
            }
            const roomForOthers = availableWidth - implicitWidthOfActiveTab
            const othersFit = roomForOthers >= MIN_TAB * others
            const count = others + (implicitWidthOfActiveTab > 0 ? 1 : 0)
            for (let tab of contentItem.children) {
                if (!tab || (tab.implicitWidth ?? 0) <= 0) {
                    continue
                }
                if (enoughSpace) {
                    tab.width = tab.implicitWidth
                    tab.isCutOff = false
                } else if (othersFit) {
                    if (tab.isCurrent) {
                        tab.width = tab.implicitWidth
                        tab.isCutOff = false
                    } else {
                        tab.width = Math.max(MIN_TAB, roomForOthers / (implicitWidthOfAllTabsTogether - implicitWidthOfActiveTab)
                                             * tab.implicitWidth)
                        tab.isCutOff = true
                    }
                } else {
                    tab.width = Math.max(MIN_TAB, Math.min(tab.implicitWidth, availableWidth / Math.max(1, count)))
                    tab.isCutOff = tab.width < tab.implicitWidth
                }
            }
        }

        delegate: DockPanelTab {
            required property var model

            text: model.title
            isCurrent: root.currentIndex === model.index
            contextMenuModel: model.contextMenu

            navigation.name: text
            navigation.panel: root.navigationPanel
            navigation.order: model.index * 2 // NOTE '...' button will have +1 order

            Component.onCompleted: { tabs.layoutTabs() }

            onImplicitWidthChanged: { tabs.layoutTabs() }

            onNavigationTriggered: {
                root.tabClicked(model.index)
            }

            onClicked: {
                root.tabClicked(model.index)
            }

            onHandleContextMenuItemRequested: function(itemId) {
                root.handleContextMenuItemRequested(itemId)
            }
        }
    }

    MouseArea {
        id: draggingTabsMouseArea

        anchors.top: parent.top
        anchors.left: parent.left
        width: tabs.contentWidth
        height: root.height - bottomSeparator.height

        hoverEnabled: false
        propagateComposedEvents: true
        enabled: root.visible && root.draggingTabsAllowed
        cursorShape: Qt.SizeAllCursor

        onClicked: function(mouse) {
            if (!root.tabBarCpp) {
                return
            }
            // Hack - if the current tab changed on this click, do not propagate the click event
            // to the tab's menu button. We don't want the menu to appear when changing tabs.
            mouse.accepted = root.tabBarCpp.tabChangedOnClick
        }
    }

    Loader {
        id: toolbarLoader

        anchors.top: parent.top
        anchors.left: tabs.right
        anchors.right: parent.right
        anchors.bottom: bottomSeparatorContainer.top
    }

    Item {
        id: bottomSeparatorContainer

        anchors.left: tabs.right
        anchors.right: parent.right
        anchors.bottom: parent.bottom

        height: bottomSeparator.height

        SeparatorLine { id: bottomSeparator }
    }
}
