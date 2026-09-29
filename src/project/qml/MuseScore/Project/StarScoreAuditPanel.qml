/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Audit panel.
 *   Checks: things to look at in the parts. Click one to select its bars in the score; mark it intentional
 *           if it's meant to be that way. Mark an arrangement audited when you're done with it.
 *   Listen: each horn section at each rehearsal mark where it plays, biggest section first. Approve each
 *           one to move on; when a rehearsal mark is done, the next one starts.
 */
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

Item {
    id: root

    property NavigationSection navigationSection: null
    property int navigationOrderStart: 1

    property bool listenTab: false

    AuditPanelModel {
        id: auditModel
    }

    Component.onCompleted: auditModel.load()
    onVisibleChanged: auditModel.setActive(root.visible)

    readonly property color likelyColor: "#E0504A"
    readonly property color lookColor: "#E8892B"
    readonly property color minorColor: "#8A8FA0"
    readonly property color doneColor: "#3FA66B"

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        // Audit all songs: which song this is, and moving between them
        Rectangle {
            Layout.fillWidth: true
            visible: auditModel.walkActive
            implicitHeight: walkColumn.implicitHeight + 12
            radius: 4
            color: ui.theme.backgroundSecondaryColor
            border.color: ui.theme.accentColor
            border.width: 1

            ColumnLayout {
                id: walkColumn
                anchors.fill: parent
                anchors.margins: 6
                spacing: 6

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    elide: Text.ElideRight
                    font: ui.theme.bodyBoldFont
                    text: auditModel.walkText
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6

                    FlatButton {
                        text: qsTrc("starscore", "Previous song")
                        enabled: auditModel.walkHasPrevious
                        onClicked: auditModel.walkPrevious()
                    }
                    FlatButton {
                        Layout.fillWidth: true
                        text: auditModel.walkIsLast ? qsTrc("starscore", "Finish") : qsTrc("starscore", "Next song")
                        accentButton: true
                        onClicked: auditModel.walkNext()
                    }
                    FlatButton {
                        text: qsTrc("starscore", "Stop")
                        toolTipTitle: qsTrc("starscore", "Stop auditing all songs")
                        toolTipDescription: qsTrc("starscore", "Leaves this song open. Start again from the Audit library.")
                        onClicked: auditModel.walkStop()
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 6

            FlatButton {
                Layout.fillWidth: true
                text: qsTrc("starscore", "Checks")
                accentButton: !root.listenTab
                onClicked: root.listenTab = false
            }
            FlatButton {
                Layout.fillWidth: true
                text: qsTrc("starscore", "Listen")
                accentButton: root.listenTab
                onClicked: root.listenTab = true
            }
            FlatButton {
                text: qsTrc("starscore", "Library…")
                toolTipTitle: qsTrc("starscore", "Audit library")
                toolTipDescription: qsTrc("starscore", "Every .starscore in your projects folder: what's left to look at in each song")
                onClicked: auditModel.openLibrary()
            }
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: !auditModel.hasScore
            wrapMode: Text.WordWrap
            text: qsTrc("starscore", "Open a score to audit it.")
        }

        // =========================== Checks ===========================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: auditModel.hasScore && !root.listenTab
            spacing: 8

            StyledDropdown {
                Layout.fillWidth: true
                model: auditModel.arrangementChoices
                currentIndex: {
                    for (var i = 0; i < auditModel.arrangementChoices.length; ++i) {
                        if (auditModel.arrangementChoices[i].value === auditModel.arrangementId) {
                            return i
                        }
                    }
                    return 0
                }
                onActivated: function(index, value) { auditModel.setArrangementId(value) }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: auditModel.referenceChoices.length > 1
                spacing: 6

                StyledTextLabel {
                    text: qsTrc("starscore", "Compare with:")
                }
                StyledDropdown {
                    Layout.fillWidth: true
                    model: auditModel.referenceChoices
                    currentIndex: {
                        for (var i = 0; i < auditModel.referenceChoices.length; ++i) {
                            if (auditModel.referenceChoices[i].value === auditModel.referenceId) {
                                return i
                            }
                        }
                        return -1
                    }
                    onActivated: function(index, value) { auditModel.setReferenceId(value) }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                CheckBox {
                    text: qsTrc("starscore", "Minor")
                    checked: auditModel.showMinor
                    onClicked: auditModel.setShowMinor(!checked)
                }
                CheckBox {
                    text: qsTrc("starscore", "Intentional")
                    checked: auditModel.showIntentional
                    onClicked: auditModel.setShowIntentional(!checked)
                }
                Item { Layout.fillWidth: true }
                FlatButton {
                    text: qsTrc("starscore", "Re-check")
                    onClicked: auditModel.recheck()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 6

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    text: auditModel.summary
                }
                FlatButton {
                    icon: IconCode.ARROW_LEFT
                    toolTipTitle: qsTrc("starscore", "Previous")
                    enabled: auditModel.issues.length > 0
                    onClicked: auditModel.previousIssue()
                }
                FlatButton {
                    icon: IconCode.ARROW_RIGHT
                    toolTipTitle: qsTrc("starscore", "Next")
                    enabled: auditModel.issues.length > 0
                    onClicked: auditModel.nextIssue()
                }
            }

            StyledListView {
                id: issueList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 4
                model: auditModel.issues
                currentIndex: auditModel.currentIssue
                onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)


                delegate: Rectangle {
                    id: issueRow
                    required property var modelData
                    required property int index

                    width: issueList.width
                    height: issueColumn.implicitHeight + 12
                    radius: 3
                    color: index === auditModel.currentIssue ? ui.theme.buttonColor : "transparent"
                    border.width: 1
                    border.color: ui.theme.strokeColor
                    opacity: modelData.intentional ? 0.55 : 1.0

                    MouseArea {
                        anchors.fill: parent
                        onClicked: auditModel.selectIssue(issueRow.index)
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 4
                        radius: 2
                        color: issueRow.modelData.severity === 2 ? root.likelyColor
                             : issueRow.modelData.severity === 1 ? root.lookColor : root.minorColor
                    }

                    ColumnLayout {
                        id: issueColumn
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.leftMargin: 12
                        anchors.rightMargin: 6
                        anchors.topMargin: 6
                        spacing: 2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            StyledTextLabel {
                                text: issueRow.modelData.bars
                                font: ui.theme.bodyBoldFont
                            }
                            StyledTextLabel {
                                Layout.fillWidth: true
                                horizontalAlignment: Text.AlignLeft
                                elide: Text.ElideRight
                                text: issueRow.modelData.check + " · " + issueRow.modelData.title
                            }
                            FlatButton {
                                text: issueRow.modelData.intentional ? qsTrc("starscore", "Not intentional") : qsTrc("starscore", "Intentional")
                                toolTipTitle: issueRow.modelData.intentional
                                              ? qsTrc("starscore", "Show this again")
                                              : qsTrc("starscore", "It's meant to be this way: stop showing it")
                                onClicked: auditModel.setIntentional(issueRow.index, !issueRow.modelData.intentional)
                            }
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.WordWrap
                            text: issueRow.modelData.part
                            opacity: 0.8
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            wrapMode: Text.WordWrap
                            text: issueRow.modelData.message
                        }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                visible: auditModel.arrangementId !== ""
                spacing: 6

                StyledTextLabel {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignLeft
                    wrapMode: Text.WordWrap
                    text: auditModel.arrangementStatus
                }
                FlatButton {
                    Layout.fillWidth: true
                    text: auditModel.arrangementAudited ? qsTrc("starscore", "Unmark audited") : qsTrc("starscore", "Mark arrangement audited")
                    accentButton: !auditModel.arrangementAudited
                    onClicked: auditModel.markArrangementAudited(!auditModel.arrangementAudited)
                }
            }
        }

        // =========================== Listen ===========================
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: auditModel.hasScore && root.listenTab
            spacing: 8

            StyledTextLabel {
                Layout.fillWidth: true
                visible: !auditModel.hasListenSteps
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignLeft
                text: qsTrc("starscore", "No horn sections to listen to. Horn sections are sections named like “3-Horn Section” or “3-Horn Any”.")
            }

            StyledTextLabel {
                Layout.fillWidth: true
                visible: auditModel.hasListenSteps
                horizontalAlignment: Text.AlignLeft
                wrapMode: Text.WordWrap
                text: auditModel.listenProgress
            }

            RowLayout {
                Layout.fillWidth: true
                visible: auditModel.hasListenSteps
                spacing: 6

                FlatButton {
                    icon: IconCode.ARROW_LEFT
                    toolTipTitle: qsTrc("starscore", "Previous rehearsal mark")
                    onClicked: auditModel.previousRehearsal()
                }
                StyledTextLabel {
                    Layout.fillWidth: true
                    text: auditModel.rehearsalTitle
                    font: ui.theme.largeBodyBoldFont
                }
                FlatButton {
                    icon: IconCode.ARROW_RIGHT
                    toolTipTitle: qsTrc("starscore", "Next rehearsal mark")
                    onClicked: auditModel.nextRehearsal()
                }
            }

            StyledListView {
                id: stepList
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: auditModel.hasListenSteps
                clip: true
                spacing: 4
                model: auditModel.steps

                delegate: Rectangle {
                    id: stepRow
                    required property var modelData
                    required property int index

                    width: stepList.width
                    height: 34
                    radius: 3
                    color: index === auditModel.currentStep ? ui.theme.buttonColor : "transparent"
                    border.width: 1
                    border.color: ui.theme.strokeColor

                    MouseArea {
                        anchors.fill: parent
                        onClicked: auditModel.selectStep(stepRow.index)
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 8

                        StyledTextLabel {
                            text: stepRow.modelData.approved ? "✓" : (stepRow.index === auditModel.currentStep ? "▶" : "·")
                            color: stepRow.modelData.approved ? root.doneColor : ui.theme.fontPrimaryColor
                            font: ui.theme.bodyBoldFont
                        }
                        StyledTextLabel {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignLeft
                            text: stepRow.modelData.text
                        }
                        FlatButton {
                            visible: stepRow.modelData.approved
                            text: qsTrc("starscore", "Undo")
                            toolTipTitle: qsTrc("starscore", "Not approved after all")
                            onClicked: auditModel.unapproveStep(stepRow.index)
                        }
                    }
                }
            }

            StyledTextLabel {
                Layout.fillWidth: true
                visible: auditModel.listenDone
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignLeft
                color: root.doneColor
                text: qsTrc("starscore", "Every horn section has been listened to at every rehearsal mark.")
            }

            RowLayout {
                Layout.fillWidth: true
                visible: auditModel.hasListenSteps
                spacing: 12

                CheckBox {
                    text: qsTrc("starscore", "With rhythm section")
                    checked: auditModel.withRhythm
                    onClicked: auditModel.setWithRhythm(!checked)
                }
                CheckBox {
                    text: qsTrc("starscore", "Play the next one automatically")
                    checked: auditModel.autoPlay
                    onClicked: auditModel.setAutoPlay(!checked)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: auditModel.hasListenSteps
                spacing: 6

                FlatButton {
                    Layout.fillWidth: true
                    text: qsTrc("starscore", "▶ Play")
                    toolTipTitle: qsTrc("starscore", "Play this section at this rehearsal mark")
                    toolTipDescription: qsTrc("starscore", "Only its instruments are shown (and heard) until you press Done")
                    onClicked: auditModel.playStep()
                }
                FlatButton {
                    Layout.fillWidth: true
                    text: qsTrc("starscore", "■ Stop")
                    onClicked: auditModel.stopPlaying()
                }
                FlatButton {
                    Layout.fillWidth: true
                    text: qsTrc("starscore", "✓ Approve")
                    accentButton: true
                    onClicked: auditModel.approveStep()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                visible: auditModel.hasListenSteps
                spacing: 6

                FlatButton {
                    Layout.fillWidth: true
                    text: qsTrc("starscore", "Back")
                    onClicked: auditModel.previousStep()
                }
                FlatButton {
                    Layout.fillWidth: true
                    text: qsTrc("starscore", "Skip")
                    onClicked: auditModel.skipStep()
                }
                FlatButton {
                    Layout.fillWidth: true
                    text: qsTrc("starscore", "Done")
                    toolTipTitle: qsTrc("starscore", "Stop listening")
                    toolTipDescription: qsTrc("starscore", "Show the instruments that were showing before")
                    enabled: auditModel.listening
                    onClicked: auditModel.finishListening()
                }
            }
        }
    }
}
