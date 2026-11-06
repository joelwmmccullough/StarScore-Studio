/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Practice in a window of its own (1.29.1, Joel: the docked or floating panel is "very tough to
 * wrangle"; "an option for it to open in a standalone window"). An ordinary window: move it anywhere (another screen),
 * any size, and it opens again at the size it last had. Everything the panel has: Watch, Jam, Listen. Closing it puts
 * the speed back to 100%.
 */
import QtQuick

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

StyledDialogView {
    id: root

    title: qsTrc("starscore", "Practice")

    contentWidth: 1200
    contentHeight: 760
    resizable: true
    modal: false
    closeOnEscape: false

    onClosed: practicePanel.windowClosed(root.width, root.height)

    StarScorePracticePanel {
        id: practicePanel
        anchors.fill: parent
        inWindow: true
    }
}
