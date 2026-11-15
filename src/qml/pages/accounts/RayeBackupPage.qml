// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts › Raye backup: what this PC syncs with the Raye account. Each
// switch is a category of settings; "Remember my apps" sends the list of
// installed apps to the Store library. Passwords and Wi-Fi wait for
// end-to-end encryption and say so.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import org.raye.account

PageScaffold {
    id: page

    property string failure: ""

    crumbs: [{ text: i18nc("@title settings category", "Accounts"), target: "accounts" },
             { text: i18nc("@title settings page", "Raye account"), target: "accounts/raye-account" },
             { text: i18nc("@title settings page", "Raye backup") }]

    Component.onCompleted: RayeAccount.refresh()

    Connections {
        target: RayeAccount
        function onActionFailed(action, code) { page.failure = code }
    }

    function syncedText(iso) {
        if (iso === "")
            return i18nc("@info", "Not synced yet")
        const when = new Date(iso)
        const today = new Date()
        const time = when.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
        if (when.toDateString() === today.toDateString())
            return i18nc("@info %1 is a time", "Last synced today at %1", time)
        return i18nc("@info %1 is a date, %2 a time", "Last synced %1 at %2",
                     when.toLocaleDateString(Qt.locale(), Locale.ShortFormat), time)
    }

    AccountErrorBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        code: page.failure !== "" ? page.failure : RayeAccount.error
        onClosed: page.failure = ""
    }

    Card {
        Layout.fillWidth: true
        searchId: "sync-status"
        iconName: "folder-sync"
        title: RayeAccount.syncing ? i18nc("@label", "Syncing…") : page.syncedText(RayeAccount.lastSync)
        description: RayeAccount.email
        enabled: RayeAccount.signedIn

        Button {
            text: i18nc("@action:button", "Sync now")
            enabled: RayeAccount.signedIn && !RayeAccount.syncing
            onClicked: RayeAccount.syncNow()
        }
    }

    SectionHeader { text: i18nc("@title", "Remember my preferences") }

    Repeater {
        model: [
            { id: "personalization", icon: "preferences-desktop-theme-global",
              title: i18nc("@label", "Personalization"),
              text: i18nc("@info", "Theme, colors, accent color and wallpaper") },
            { id: "taskbar", icon: "org.raye.settings-taskbar", fallback: "preferences-desktop",
              title: i18nc("@label", "Taskbar"),
              text: i18nc("@info", "Pinned apps on the taskbar") },
            { id: "language", icon: "preferences-desktop-locale",
              title: i18nc("@label", "Language preferences"),
              text: i18nc("@info", "Display language, formats and keyboard layouts") },
            { id: "accessibility", icon: "preferences-desktop-accessibility",
              title: i18nc("@label", "Accessibility"),
              text: i18nc("@info", "Text size, cursor, keyboard and mouse keys, magnifier, color filters") },
            { id: "system", icon: "preferences-system",
              title: i18nc("@label", "Other settings"),
              text: i18nc("@info", "Sound, notifications, power, privacy, default apps, multitasking and more") }
        ]
        delegate: ToggleCard {
            required property var modelData
            Layout.fillWidth: true
            searchId: "sync-" + modelData.id
            iconName: modelData.icon
            iconFallback: modelData.fallback || ""
            title: modelData.title
            description: modelData.text
            enabled: RayeAccount.signedIn
            checked: RayeAccount.sync[modelData.id] !== false
            onToggled: RayeAccount.setSync(modelData.id, checked)
        }
    }
    ToggleCard {
        Layout.fillWidth: true
        searchId: "sync-passwords"
        iconName: "dialog-password"
        title: i18nc("@label", "Passwords and Wi-Fi networks")
        description: i18nc("@info", "Coming later. They will be encrypted on this PC so only your devices can read them.")
        enabled: false
        checked: false
    }

    SectionHeader { text: i18nc("@title", "Apps") }
    ToggleCard {
        Layout.fillWidth: true
        searchId: "sync-apps"
        iconName: "org.raye.store"
        iconFallback: "system-software-install"
        title: i18nc("@label", "Remember my apps")
        description: i18nc("@info", "Lists the apps on this PC in your Store library, so you can install them on your other PCs.")
        enabled: RayeAccount.signedIn
        checked: RayeAccount.sync.apps !== false
        onToggled: RayeAccount.setSync("apps", checked)
    }
}
