// SPDX-License-Identifier: GPL-3.0-or-later
// The Raye account on Accounts › Your info, where Windows puts the
// Microsoft account: on a local account, the way to sign in with one
// instead; signed in, the account, a link to manage it online and the
// way back to a local account. Loaded only when the account is there.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import org.raye.account

ColumnLayout {
    id: info

    spacing: Theme.s1

    property string failure: ""

    Connections {
        target: RayeAccount
        function onActionFailed(action, code) {
            if (action !== "SyncNow" && code !== "choose")
                info.failure = code
        }
    }

    AccountErrorBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        code: info.failure !== "" ? info.failure
            : (RayeAccount.error !== "choose" ? RayeAccount.error : "")
        onClosed: info.failure = ""
    }

    RayeAccountConnect {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
    }

    Card {
        Layout.fillWidth: true
        visible: RayeAccount.loaded && !RayeAccount.signedIn && RayeAccount.stage === ""
        iconName: "raye-eye"
        iconFallback: "org.raye.settings"
        title: i18nc("@label", "Sign in with a Raye account instead")
        description: i18nc("@info", "Your settings and picture on every PC you use. This account and your files stay as they are.")
        Row {
            spacing: Theme.s3
            Button {
                text: i18nc("@action:button", "Create account")
                onClicked: RayeAccount.signIn(true)
            }
            Button {
                accent: true
                text: i18nc("@action:button", "Sign in")
                onClicked: RayeAccount.signIn(false)
            }
        }
    }

    Card {
        Layout.fillWidth: true
        visible: RayeAccount.signedIn
        iconName: "raye-eye"
        iconFallback: "org.raye.settings"
        title: i18nc("@label", "Raye account")
        description: RayeAccount.email
        Button {
            text: i18nc("@action:button", "Manage your account")
            onClicked: Qt.openUrlExternally(RayeAccount.manageUrl)
        }
    }
    NavCard {
        Layout.fillWidth: true
        visible: RayeAccount.signedIn
        iconName: "org.raye.settings-backup"
        iconFallback: "folder-sync"
        title: i18nc("@title settings page", "Raye backup")
        description: RayeAccount.lastSync !== ""
            ? i18nc("@info %1 is a date and time", "Last synced %1", new Date(RayeAccount.lastSync).toLocaleString(Qt.locale(), Locale.ShortFormat))
            : i18nc("@info", "Choose what syncs and remember your apps")
        target: "accounts/raye-backup"
    }
    ButtonCard {
        Layout.fillWidth: true
        visible: RayeAccount.signedIn
        iconName: "system-log-out"
        title: i18nc("@label", "Stop using this account on this PC")
        description: i18nc("@info", "Sign in with this PC's account only. Your settings and files stay here.")
        buttonText: i18nc("@action:button", "Sign out")
        onButtonClicked: signOutDialog.open()
    }

    ContentDialog {
        id: signOutDialog
        title: i18nc("@title:window", "Sign out of your Raye account?")
        text: i18nc("@info", "Your settings stay on this PC and stop syncing. You can keep the picture and name your account gave this PC, or put back the ones it had before.")
        primaryText: i18nc("@action:button", "Sign out")
        secondaryText: i18nc("@action:button", "Put them back and sign out")
        onAccepted: RayeAccount.signOut(false)
        onSecondaryClicked: RayeAccount.signOut(true)
    }
}
