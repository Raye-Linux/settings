// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts > Email & accounts: online accounts for the apps of this PC.
// Adding one needs the small file that describes each service: without
// any the page is not in the list (layout: requires online-accounts).
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accounts"), target: "accounts" },
             { text: i18nc("@title settings page", "Email & accounts") }]

    ExternalModule { id: external }
    property string failure: ""

    InfoBar {
        Layout.fillWidth: true
        open: page.failure !== ""
        severity: "error"
        title: i18nc("@title", "It could not be opened")
        message: page.failure
        onClosed: page.failure = ""
    }

    ButtonCard {
        searchId: "online-accounts"
        iconName: "org.raye.settings-email"
        iconFallback: "preferences-system-users"
        title: i18nc("@title", "Online accounts")
        description: i18nc("@info", "Accounts your apps can use, like cloud storage")
        buttonText: i18nc("@action:button", "Manage")
        onButtonClicked: {
            if (!external.open("kcm_kaccounts"))
                page.failure = i18nc("@info", "The online accounts window is not installed.")
        }
    }


    SectionHeader { text: i18nc("@title:group", "Accounts on this PC") }

    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-other-users"
        iconFallback: "system-users"
        title: i18nc("@title settings page", "Other users")
        description: i18nc("@info", "Add and remove people on this PC")
        target: "accounts/other-users"
    }
}
