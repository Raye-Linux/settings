// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts › Raye account. Signed out: what the account does and the
// buttons to sign in or create one (both open the browser; no password
// is ever typed here). Signed in: who you are, what the account changes
// on this PC, links to Raye backup and Your devices, and signing out.
// Only shown when RayeAccount.available (the hidden preview switch).
import QtQuick
import QtQuick.Layouts
import QtQuick.Dialogs
import org.raye.settings
import org.raye.account

PageScaffold {
    id: page

    property string failure: ""
    property bool syncedOnOpen: false

    crumbs: [{ text: i18nc("@title settings category", "Accounts"), target: "accounts" },
             { text: i18nc("@title settings page", "Raye account") }]

    Component.onCompleted: RayeAccount.refresh()

    Connections {
        target: RayeAccount
        function onStateChanged() {
            // Opening the page is a good moment to pick up other PCs' changes.
            if (RayeAccount.signedIn && !page.syncedOnOpen) {
                page.syncedOnOpen = true
                RayeAccount.syncNow()
            }
        }
        function onActionFailed(action, code) {
            if (action !== "SyncNow" && code !== "choose")
                page.failure = code
        }
    }

    AccountErrorBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        code: page.failure !== "" ? page.failure : RayeAccount.error
        onClosed: page.failure = ""
    }

    RayeAccountConnect {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
    }

    // Signed out

    Card {
        Layout.fillWidth: true
        visible: RayeAccount.loaded && !RayeAccount.signedIn && RayeAccount.stage === ""
        searchId: "sign-in"
        iconName: "raye-eye"
        iconFallback: "org.raye.settings"
        title: i18nc("@label", "Sign in with a Raye account")
        description: i18nc("@info", "Your settings and apps on all your PCs. Raye works the same without one.")

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

    SectionHeader {
        visible: RayeAccount.loaded && !RayeAccount.signedIn
        text: i18nc("@title", "With a Raye account")
    }
    Card {
        Layout.fillWidth: true
        visible: RayeAccount.loaded && !RayeAccount.signedIn
        iconName: "preferences-desktop-theme-global"
        title: i18nc("@label", "Your settings follow you")
        description: i18nc("@info", "Theme, wallpaper, taskbar, language, accessibility and the rest of Settings, on every PC you sign in to.")
    }
    Card {
        Layout.fillWidth: true
        visible: RayeAccount.loaded && !RayeAccount.signedIn
        iconName: "org.raye.store"
        iconFallback: "system-software-install"
        title: i18nc("@label", "Your apps in the Store")
        description: i18nc("@info", "See what's installed on your other PCs and get it here in one click.")
    }
    Card {
        Layout.fillWidth: true
        visible: RayeAccount.loaded && !RayeAccount.signedIn
        iconName: "computer"
        title: i18nc("@label", "Your devices")
        description: i18nc("@info", "See your PCs in one list and sign out one you no longer have.")
    }

    // Signed in

    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s5
        visible: RayeAccount.signedIn
        spacing: Theme.s6

        PersonPicture {
            size: 72
            source: RayeAccount.picture
            name: RayeAccount.displayName
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.s1
            Text {
                Layout.fillWidth: true
                text: RayeAccount.displayName
                font: Theme.subtitle
                color: Theme.text
                elide: Text.ElideRight
            }
            Text {
                Layout.fillWidth: true
                text: RayeAccount.email
                font: Theme.body
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
            HyperlinkButton {
                font: Theme.captionStrong
                text: i18nc("@action:button opens account.rayelinux.com", "Manage your account")
                onClicked: Qt.openUrlExternally(RayeAccount.manageUrl)
            }
        }
    }

    SectionHeader {
        visible: RayeAccount.signedIn
        first: true
        text: i18nc("@title", "Your info")
    }
    ComboCard {
        id: modeCard
        Layout.fillWidth: true
        visible: RayeAccount.signedIn
        searchId: "picture-and-name"
        iconName: "org.raye.settings-your-info"
        iconFallback: "user-identity"
        title: i18nc("@label", "Use my account's picture and name on this PC")
        description: i18nc("@info", "Shown on the lock screen and in Start. Your sign-in name doesn't change.")
        textRole: "text"
        valueRole: "value"
        model: [
            { value: "both", text: i18nc("@item", "Picture and name") },
            { value: "picture", text: i18nc("@item", "Picture only") },
            { value: "name", text: i18nc("@item", "Name only") },
            { value: "none", text: i18nc("@item", "Neither") }
        ]
        currentIndex: Math.max(0, indexOfValue(RayeAccount.profileMode))
        onActivated: (index) => RayeAccount.setProfileMode(model[index].value)
    }
    ButtonCard {
        Layout.fillWidth: true
        visible: RayeAccount.signedIn
        searchId: "account-name"
        iconName: "edit-rename"
        title: i18nc("@label", "Name")
        description: RayeAccount.displayName
        buttonText: i18nc("@action:button", "Change")
        onButtonClicked: {
            nameField.text = RayeAccount.displayName
            nameDialog.open()
        }
    }
    ButtonCard {
        Layout.fillWidth: true
        visible: RayeAccount.signedIn
        searchId: "account-picture"
        iconName: "camera-photo"
        title: i18nc("@label", "Picture")
        description: i18nc("@info", "Your other devices get it too")
        buttonText: i18nc("@action:button", "Choose a picture")
        onButtonClicked: pictureDialog.open()
    }

    SectionHeader {
        visible: RayeAccount.signedIn
        text: i18nc("@title", "Settings and devices")
    }
    NavCard {
        Layout.fillWidth: true
        visible: RayeAccount.signedIn
        searchId: "raye-backup"
        iconName: "org.raye.settings-backup"
        iconFallback: "folder-sync"
        title: i18nc("@title settings page", "Raye backup")
        description: i18nc("@info", "Choose what syncs and remember your apps")
        target: "accounts/raye-backup"
    }
    NavCard {
        Layout.fillWidth: true
        visible: RayeAccount.signedIn
        searchId: "your-devices"
        iconName: "computer"
        title: i18nc("@title settings page", "Your devices")
        description: i18nc("@info", "The PCs signed in to your account")
        target: "accounts/your-devices"
    }

    SectionHeader {
        visible: RayeAccount.signedIn
        text: i18nc("@title", "Sign-in")
    }
    ButtonCard {
        Layout.fillWidth: true
        visible: RayeAccount.signedIn
        searchId: "sign-out"
        iconName: "system-log-out"
        title: i18nc("@label", "Sign out on this PC")
        description: i18nc("@info", "Your settings stay on this PC and stop syncing.")
        buttonText: i18nc("@action:button", "Sign out")
        onButtonClicked: signOutDialog.open()
    }

    ContentDialog {
        id: nameDialog
        title: i18nc("@title:window", "Change your name")
        primaryText: i18nc("@action:button", "Save")
        primaryEnabled: nameField.text.trim().length > 0 && nameField.text.trim().length <= 64
        onAccepted: RayeAccount.setDisplayName(nameField.text)

        TextField {
            id: nameField
            Layout.fillWidth: true
            maximumLength: 64
            Accessible.name: i18nc("@label", "Name")
        }
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

    FileDialog {
        id: pictureDialog
        title: i18nc("@title:window", "Choose a picture")
        fileMode: FileDialog.OpenFile
        nameFilters: [i18nc("@item file type", "Pictures (*.jpg *.jpeg *.png *.webp)")]
        onAccepted: RayeAccount.uploadPicture(selectedFile)
    }
}
