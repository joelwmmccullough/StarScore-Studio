/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Changelog: what's new in each StarScore version
 */
import QtQuick

import Muse.Ui
import Muse.UiComponents
import Muse.Learn

Rectangle {
    id: root

    color: ui.theme.backgroundSecondaryColor

    LearnPageModel {
        id: pageModel
    }

    StyledFlickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: changelogText.implicitHeight + 72
        clip: true

        StyledTextLabel {
            id: changelogText

            x: 46
            y: 36
            width: Math.min(flick.width - 92, 900)

            textFormat: Text.MarkdownText
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignTop
            elide: Text.ElideNone
            text: pageModel.starscoreChangelog()
        }
    }
}
