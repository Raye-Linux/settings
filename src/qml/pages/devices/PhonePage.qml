// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > Phone: link a phone on the same network, then send it files.
// Notifications, the clipboard and the rest then work on their own.
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Bluetooth & devices"), target: "devices" },
             { text: i18nc("@title settings page", "Phone") }]

    PhoneLink { id: link }
    Timer {
        // phones come and go without always saying so
        interval: 5000
        repeat: true
        running: true
        onTriggered: link.refresh()
    }

    property string sendTo: ""

    function stateText(device) {
        if (device.requestedByPhone)
            return i18nc("@info a phone", "Wants to link with this PC")
        if (device.requested)
            return i18nc("@info a phone", "Waiting for the phone to accept")
        if (device.paired)
            return device.reachable ? i18nc("@info a phone", "Linked and connected") : i18nc("@info a phone", "Linked, not in reach")
        return i18nc("@info a phone", "Not linked")
    }

    InfoBar {
        Layout.fillWidth: true
        open: !link.available
        closable: false
        severity: link.starting ? "informational" : "warning"
        title: link.starting ? i18nc("@title", "Starting phone linking…")
             : link.installed ? i18nc("@title", "Phone linking didn't start")
             : i18nc("@title", "Phone linking isn't installed")
        message: link.starting ? i18nc("@info", "This takes a few seconds.")
               : link.installed ? i18nc("@info", "Restart your PC, then open this page again.")
               : i18nc("@info", "Install updates to get it back.")
    }

    InfoCard {
        visible: link.available
        searchId: "pc-name"
        iconName: "computer"
        iconFallback: "computer"
        title: i18nc("@title", "Your phone sees this PC as")
        value: link.announcedName
        copyable: false
    }

    SectionHeader {
        visible: link.available
        text: i18nc("@title:group", "Phones")
    }

    EmptyState {
        Layout.fillWidth: true
        visible: link.available && link.devices.length === 0
        iconName: "smartphone"
        title: i18nc("@title", "No phone found")
        message: i18nc("@info", "This PC is ready. On your phone, install KDE Connect, then put it on the same Wi-Fi as this PC and open the app.")
        Row {
            spacing: Theme.s4
            HyperlinkButton {
                text: i18nc("@action:button", "KDE Connect for Android")
                url: "https://play.google.com/store/apps/details?id=org.kde.kdeconnect_tp"
            }
            HyperlinkButton {
                text: i18nc("@action:button", "KDE Connect for iPhone")
                url: "https://apps.apple.com/app/kde-connect/id1580245991"
            }
        }
    }

    Repeater {
        model: link.devices
        delegate: Card {
            required property var modelData
            Layout.fillWidth: true
            iconName: "org.raye.settings-phone"
            iconFallback: "smartphone"
            title: modelData.name
            description: page.stateText(modelData)
            Row {
                spacing: Theme.s2
                Button {
                    visible: !modelData.paired && !modelData.requested && !modelData.requestedByPhone
                    enabled: modelData.reachable
                    accent: true
                    text: i18nc("@action:button", "Link")
                    onClicked: link.pair(modelData.id)
                }
                Button {
                    visible: modelData.requestedByPhone
                    accent: true
                    text: i18nc("@action:button", "Accept")
                    onClicked: link.acceptPairing(modelData.id)
                }
                Button {
                    visible: modelData.requested || modelData.requestedByPhone
                    text: i18nc("@action:button", "Cancel")
                    onClicked: link.cancelPairing(modelData.id)
                }
                Button {
                    visible: modelData.paired
                    enabled: modelData.reachable
                    text: i18nc("@action:button", "Send a file")
                    onClicked: {
                        page.sendTo = modelData.id
                        fileDialog.open()
                    }
                }
                Button {
                    visible: modelData.paired
                    text: i18nc("@action:button", "Unlink")
                    onClicked: link.unpair(modelData.id)
                }
            }
        }
    }

    FileDialog {
        id: fileDialog
        title: i18nc("@title:window", "Choose a file to send")
        onAccepted: link.sendFile(page.sendTo, selectedFile)
    }
}
