// SPDX-License-Identifier: GPL-3.0-or-later
// The big picture at the top of the account pages: you, and what kind of
// account you have. Signed in to a Raye account, it is that account: its
// picture, its name, its email and a link to manage it online.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

RowLayout {
    spacing: Theme.s6

    PersonPicture {
        size: 112
        source: AccountIdentity.picture
        name: AccountIdentity.name
    }
    ColumnLayout {
        spacing: Theme.s1
        Layout.fillWidth: true
        Text {
            Layout.fillWidth: true
            text: AccountIdentity.name
            elide: Text.ElideRight
            font: Theme.subtitle
            color: Theme.text
        }
        Text {
            text: AccountIdentity.signedIn ? AccountIdentity.email : CurrentUser.userName
            font: Theme.body
            color: Theme.textSecondary
        }
        Text {
            text: AccountIdentity.signedIn
                ? i18nc("@label %1 is Sudo user or Standard user", "Raye account · %1", CurrentUser.accountLabel)
                : i18nc("@label %1 is Sudo user or Standard user", "Local account · %1", CurrentUser.accountLabel)
            font: Theme.caption
            color: Theme.textSecondary
        }
        HyperlinkButton {
            visible: AccountIdentity.signedIn
            font: Theme.captionStrong
            text: i18nc("@action:button opens account.rayelinux.com", "Manage your account")
            onClicked: Qt.openUrlExternally(AccountIdentity.manageUrl)
        }
    }
}
