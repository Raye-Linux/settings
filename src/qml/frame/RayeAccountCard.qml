// SPDX-License-Identifier: GPL-3.0-or-later
// "Raye account" on the launch screen: the account this PC is signed in
// to, or the way to sign in to one (Accounts › Your info). Without the
// account service it says the account lives on this PC and points to the
// online accounts instead. Keeps the id home#raye-account.
import QtQuick
import org.raye.settings

HeroCard {
    id: card

    readonly property bool offered: Router.isAvailable("accounts/raye-account")

    searchId: "raye-account"
    title: i18nc("@title", "Raye account")
    message: AccountIdentity.signedIn ? i18nc("@info", "Signed in. Your settings and picture sync across your PCs.")
           : offered ? i18n("Sign in to keep your settings and picture the same on every PC. Not needed to use Raye.")
           : i18n("Not needed to use Raye. Your account is on this PC only.")
    iconName: "raye-eye"
    iconFallback: "org.raye.settings"
    clickable: false

    HyperlinkButton {
        visible: AccountIdentity.signedIn
        font: Theme.captionStrong
        text: i18nc("@action:button opens account.rayelinux.com", "Manage your account")
        onClicked: Qt.openUrlExternally(AccountIdentity.manageUrl)
    }
    HyperlinkButton {
        visible: card.offered && !AccountIdentity.signedIn
        font: Theme.captionStrong
        text: i18nc("@action:button opens Accounts > Your info", "Sign in")
        onClicked: Router.navigate("accounts/your-info")
    }
    HyperlinkButton {
        visible: !card.offered && Router.has("online-accounts")
        font: Theme.captionStrong
        text: i18nc("@action:button opens Accounts > Email & accounts", "Add your online accounts")
        onClicked: Router.navigate("accounts/email")
    }
}
