// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts: who you are on this PC, then one card for each page.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    title: i18nc("@title settings category", "Accounts")
    showBack: false

    banner: Component {
        AccountBanner { }
    }

    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-your-info"
        iconFallback: "user-identity"
        title: i18nc("@title settings page", "Your info")
        description: i18nc("@info", "Your picture and your name")
        target: "accounts/your-info"
    }
    NavCard {
        Layout.fillWidth: true
        visible: Router.isAvailable("accounts/raye-account")
        iconName: "org.raye.settings-your-info"
        iconFallback: "user-identity"
        title: i18nc("@title settings page", "Raye account")
        description: i18nc("@info", "Sign in, sync your settings across your PCs")
        target: "accounts/raye-account"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-backup"
        iconFallback: "folder-download"
        title: i18nc("@title settings page", "Files from Windows")
        description: i18nc("@info", "Copy your documents, pictures and more from Windows")
        target: "accounts/windows-files"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-sign-in"
        iconFallback: "preferences-desktop-user-password"
        title: i18nc("@title settings page", "Sign-in options")
        description: i18nc("@info", "Password, fingerprint, automatic sign-in")
        target: "accounts/sign-in"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-email"
        iconFallback: "preferences-system-users"
        visible: Router.has("online-accounts")
        title: i18nc("@title settings page", "Email & accounts")
        description: i18nc("@info", "Your online accounts")
        target: "accounts/email"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-other-users"
        iconFallback: "system-users"
        title: i18nc("@title settings page", "Other users")
        description: i18nc("@info", "Add and remove people on this PC")
        target: "accounts/other-users"
    }
}
