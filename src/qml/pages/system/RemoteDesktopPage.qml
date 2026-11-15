// SPDX-License-Identifier: GPL-3.0-or-later
// System > Remote Desktop: turn it on, see the address another PC on
// the same network types in Remote Desktop Connection, and choose who
// may connect (accounts, passwords and port are set in the service's
// own window).
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Remote Desktop") }]

    RemoteDesktop { id: remote }

    // the state can change from the service's window, or on its own
    Timer {
        interval: 4000
        repeat: true
        running: page.visible
        onTriggered: remote.refresh()
    }

    // start wide: a bar first laid out narrow loops on its own width
    InfoBar {
        implicitWidth: Theme.contentMaxWidth
        severity: "warning"
        closable: false
        open: !remote.installed
        title: i18n("Remote Desktop is not installed")
        message: i18n("Install the latest updates to get it back.")
    }

    InfoBar {
        implicitWidth: Theme.contentMaxWidth
        severity: "error"
        closable: false
        open: remote.error !== ""
        title: i18n("Something went wrong")
        message: remote.error
    }

    ToggleCard {
        iconName: "org.raye.settings-remote-desktop"
        iconFallback: "krdc"
        title: i18n("Remote Desktop")
        description: remote.installed && !remote.hasUsers && !remote.running
                     ? i18n("Connect to and use this PC from another device on the same network. Choose who can connect first.")
                     : i18n("Connect to and use this PC from another device on the same network")
        searchId: "remote-desktop"
        enabled: remote.installed && !remote.busy && (remote.hasUsers || remote.running)
        Binding on checked { value: remote.running }
        onToggled: remote.setEnabled(checked)
    }

    ButtonCard {
        iconName: "org.raye.settings-other-users"
        iconFallback: "system-users"
        title: i18n("Who can connect")
        description: remote.hasUsers ? i18n("Change the accounts and passwords other devices use to sign in to this PC")
                                     : i18n("Add an account and a password that other devices use to sign in to this PC")
        searchId: "remote-desktop-users"
        buttonText: remote.hasUsers ? i18nc("@action:button", "Change") : i18nc("@action:button", "Add")
        accent: remote.installed && !remote.hasUsers
        enabled: remote.installed
        onButtonClicked: remote.openOptions()
    }

    InfoCard {
        iconName: "org.raye.settings-network"
        iconFallback: "network-wired"
        title: i18n("PC address")
        description: i18n("Type this in Remote Desktop Connection on the other device")
        searchId: "remote-desktop-address"
        value: remote.addresses.length > 0 ? remote.addresses.join(", ") : i18n("Not connected to a network")
        copyable: remote.addresses.length > 0
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s3
        Layout.leftMargin: Theme.s1
        text: i18n("Remote Desktop works between devices on the same network. Other people on that network can try to connect, so use a strong password.")
        font: Theme.caption
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignLeft
        lineHeightMode: Text.FixedHeight
        lineHeight: Theme.captionLineHeight
    }
}
