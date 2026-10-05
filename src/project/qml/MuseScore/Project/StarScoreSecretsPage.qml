/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * StarScore Studio — Home › Joel's Secrets: the Songbooks page behind a password (chosen the first time it opens)
 */
import QtQuick
import QtQuick.Layouts

import Muse.Ui
import Muse.UiComponents
import MuseScore.Project

Rectangle {
    id: root

    color: ui.theme.backgroundSecondaryColor

    SecretsLockModel {
        id: lock
    }

    property string message: ""

    function submit() {
        if (lock.hasPassword) {
            if (!lock.unlock(passwordField.currentText)) {
                root.message = qsTrc("starscore", "Wrong password.")
            }
        } else {
            root.message = lock.setPassword(passwordField.currentText, againField.currentText)
        }
    }

    Loader {
        anchors.fill: parent
        active: lock.unlocked
        sourceComponent: StarScoreSongbooks {}
    }

    FlatButton {
        visible: lock.unlocked
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 12
        icon: IconCode.LOCK_CLOSED
        toolTipTitle: qsTrc("starscore", "Lock")
        onClicked: lock.lock()
    }

    ColumnLayout {
        visible: !lock.unlocked
        anchors.centerIn: parent
        width: Math.min(360, parent.width - 48)
        spacing: 12

        StyledIconLabel {
            Layout.alignment: Qt.AlignHCenter
            iconCode: IconCode.LOCK_CLOSED
            font.pixelSize: 32
        }

        StyledTextLabel {
            Layout.fillWidth: true
            text: qsTrc("starscore", "Joel's Secrets")
            font: ui.theme.headerBoldFont
        }

        StyledTextLabel {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: lock.hasPassword ? qsTrc("starscore", "Enter the password.")
                                   : qsTrc("starscore", "Choose a password for this page. It's asked for each time StarScore starts.")
        }

        TextInputField {
            id: passwordField
            Layout.fillWidth: true
            hint: qsTrc("starscore", "Password")
            Component.onCompleted: {
                passwordField.inputField.echoMode = TextInput.Password
                passwordField.ensureActiveFocus()
            }
            onTextEdited: function(t) { passwordField.currentText = t; root.message = "" }
            onAccepted: root.submit()
        }

        TextInputField {
            id: againField
            visible: !lock.hasPassword
            Layout.fillWidth: true
            hint: qsTrc("starscore", "Password again")
            Component.onCompleted: againField.inputField.echoMode = TextInput.Password
            onTextEdited: function(t) { againField.currentText = t; root.message = "" }
            onAccepted: root.submit()
        }

        StyledTextLabel {
            Layout.fillWidth: true
            visible: root.message !== ""
            text: root.message
            color: "#E0463A"
        }

        FlatButton {
            Layout.alignment: Qt.AlignHCenter
            accentButton: true
            text: lock.hasPassword ? qsTrc("starscore", "Open") : qsTrc("starscore", "Set password")
            onClicked: root.submit()
        }
    }
}
