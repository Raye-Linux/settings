// SPDX-License-Identifier: GPL-3.0-or-later
// A sign-in from start to end: the browser step, the first sync, the
// question when the account already has other settings, and the line
// that says it worked. Shown on Your info and on the Raye account page.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings
import org.raye.account

ColumnLayout {
    id: flow

    spacing: Theme.s1
    visible: RayeAccount.stage !== "" || RayeAccount.choosePending || RayeAccount.justConnected

    Connections {
        target: RayeAccount
        function onStateChanged() {
            if (RayeAccount.choosePending && !chooseDialog.asked) {
                chooseDialog.asked = true
                chooseDialog.open()
            }
        }
    }

    Card {
        Layout.fillWidth: true
        visible: RayeAccount.stage === "browser"
        iconName: "internet-web-browser"
        title: i18nc("@label", "Finish in your browser")
        description: i18nc("@info", "Sign in or create your account on the page that just opened. New account? Open the link in the email we send, then sign in on that same page.")
        Button {
            text: i18nc("@action:button", "Cancel")
            onClicked: RayeAccount.cancelSignIn()
        }
    }

    Card {
        Layout.fillWidth: true
        visible: RayeAccount.stage === "syncing"
        iconName: "folder-sync"
        title: i18nc("@label", "Syncing your settings")
        description: i18nc("@info", "Your settings and picture are going to your Raye account. This takes a few seconds.")
    }
    ProgressBar {
        Layout.fillWidth: true
        visible: RayeAccount.stage === "syncing"
        indeterminate: true
    }

    Card {
        Layout.fillWidth: true
        visible: RayeAccount.choosePending && RayeAccount.stage === ""
        iconName: "dialog-question"
        title: i18nc("@label", "Which settings do you want to keep?")
        description: i18ncp("@info", "Your Raye account already has settings, and %1 is different on this PC.",
                            "Your Raye account already has settings, and %1 are different on this PC.",
                            RayeAccount.chooseDiffer)
        Row {
            spacing: Theme.s3
            Button {
                text: i18nc("@action:button", "Keep this PC's")
                onClicked: RayeAccount.keepSettings("pc")
            }
            Button {
                accent: true
                text: i18nc("@action:button", "Use the account's")
                onClicked: RayeAccount.keepSettings("account")
            }
        }
    }

    InfoBar {
        Layout.fillWidth: true
        open: RayeAccount.justConnected
        severity: "success"
        title: i18nc("@info", "Your Raye account is connected")
        message: i18nc("@info", "Your settings and picture now stay the same on every PC you sign in to.")
        onClosed: RayeAccount.dismissConnected()
    }

    ContentDialog {
        id: chooseDialog
        property bool asked: false
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        title: i18nc("@title:window", "Which settings do you want to keep?")
        text: i18ncp("@info", "Your Raye account already has settings from another PC, and %1 of them is different here. Keep this PC's and they go to your account, or use your account's and this PC changes to match. Your picture goes with the choice.",
                     "Your Raye account already has settings from another PC, and %1 of them are different here. Keep this PC's and they go to your account, or use your account's and this PC changes to match. Your picture goes with the choice.",
                     RayeAccount.chooseDiffer)
        primaryText: i18nc("@action:button", "Use my account's")
        secondaryText: i18nc("@action:button", "Keep this PC's")
        closeText: i18nc("@action:button", "Decide later")
        onAccepted: RayeAccount.keepSettings("account")
        onSecondaryClicked: RayeAccount.keepSettings("pc")
    }
}
