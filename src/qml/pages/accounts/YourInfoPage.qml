// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts > Your info: your picture and your name. Both show on the
// sign-in screen and at the top of Settings as soon as they change.
// Signed in to a Raye account, they are the account's (every PC gets
// them); on a local account, this is where you switch to a Raye account.
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accounts"), target: "accounts" },
             { text: i18nc("@title settings page", "Your info") }]

    banner: Component {
        AccountBanner { }
    }

    AccountUsers {
        id: users
        onDone: CurrentUser.reload()
    }

    readonly property var raye: AccountIdentity.signedIn ? AccountIdentity.raye : null

    function setPicture(url) {
        if (raye)
            raye.uploadPicture(url)
        else
            users.setMyPicture(url)
    }

    InfoBar {
        Layout.fillWidth: true
        open: users.error !== ""
        severity: "error"
        title: i18nc("@title", "That didn't work")
        message: users.error
        onClosed: users.clearError()
    }

    SectionHeader { text: i18nc("@title:group", "Adjust your picture") }

    ButtonCard {
        searchId: "picture"
        iconName: "folder-pictures"
        iconFallback: "folder-pictures"
        title: i18nc("@title", "Choose a file")
        description: page.raye ? i18nc("@info", "Your other PCs get it too") : i18nc("@info", "A photo from this PC")
        buttonText: i18nc("@action:button", "Browse files")
        enabled: !users.busy
        onButtonClicked: pictureDialog.open()
    }

    Card {
        searchId: "raye-pictures"
        visible: users.avatars.length > 0
        iconName: "org.raye.settings-your-info"
        iconFallback: "user-identity"
        title: i18nc("@title", "Or pick one of these")
    }

    Flow {
        Layout.fillWidth: true
        visible: users.avatars.length > 0
        spacing: Theme.s3
        Repeater {
            model: users.avatars
            delegate: Button {
                required property string modelData
                width: 72
                height: 72
                padding: 4
                enabled: !users.busy
                Accessible.name: i18nc("@action:button", "Use this picture")
                contentItem: Image {
                    source: "file://" + modelData
                    sourceSize: Qt.size(64, 64)
                    fillMode: Image.PreserveAspectFit
                }
                onClicked: page.setPicture("file://" + modelData)
            }
        }
    }

    SectionHeader { text: i18nc("@title:group", "Account settings") }

    Loader {
        Layout.fillWidth: true
        active: Router.isAvailable("accounts/raye-account")
        visible: status === Loader.Ready
        source: Qt.resolvedUrl("RayeAccountInfo.qml")
    }

    ButtonCard {
        searchId: "name"
        iconName: "org.raye.settings-your-info"
        iconFallback: "user-identity"
        title: i18nc("@title", "Your name")
        description: page.raye ? AccountIdentity.name
                   : CurrentUser.realName !== "" ? CurrentUser.realName : CurrentUser.userName
        buttonText: i18nc("@action:button", "Change")
        enabled: !users.busy
        onButtonClicked: {
            nameField.text = page.raye ? AccountIdentity.name : CurrentUser.realName
            nameDialog.open()
        }
    }

    InfoCard {
        searchId: "account-type"
        iconName: "org.raye.settings-accounts"
        iconFallback: "system-users"
        title: i18nc("@title", "Account type")
        value: CurrentUser.accountLabel
        copyable: false
    }

    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-sign-in"
        iconFallback: "preferences-desktop-user-password"
        title: i18nc("@title settings page", "Sign-in options")
        description: i18nc("@info", "Password, fingerprint, automatic sign-in")
        target: "accounts/sign-in"
    }

    FileDialog {
        id: pictureDialog
        title: i18nc("@title:window", "Choose a picture")
        nameFilters: [i18nc("@item file type", "Pictures (*.png *.jpg *.jpeg *.webp *.bmp)")]
        onAccepted: page.setPicture(selectedFile)
    }

    ContentDialog {
        id: nameDialog
        title: i18nc("@title:window", "Change your name")
        primaryText: i18nc("@action:button", "Change")
        primaryEnabled: nameField.text.trim() !== ""
        onAccepted: page.raye ? page.raye.setDisplayName(nameField.text) : users.setMyName(nameField.text)
        TextField {
            id: nameField
            Layout.fillWidth: true
            Accessible.name: i18nc("@title", "Your name")
        }
    }
}
