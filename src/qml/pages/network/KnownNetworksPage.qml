// SPDX-License-Identifier: GPL-3.0-or-later
// Wi-Fi › Manage known networks: every saved network, with Forget and
// its settings (connect automatically, metered, IP, DNS) inside.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import "networktext.js" as Net

PageScaffold {
    id: page

    property string forgetUuid: ""
    property string forgetName: ""

    crumbs: [{ text: i18nc("@title settings category", "Network & internet"), target: "network" },
             { text: i18nc("@title settings page", "Wi-Fi"), target: "network/wifi" },
             { text: i18nc("@title settings page", "Manage known networks") }]

    NetworkStatus { id: netStatus }
    KnownNetworks { id: known }

    function securityName(security) {
        switch (security) {
        case "wpa-psk": return i18nc("@info Wi-Fi security", "WPA2-Personal")
        case "sae": return i18nc("@info Wi-Fi security", "WPA3-Personal")
        case "enterprise": return i18nc("@info Wi-Fi security", "WPA2-Enterprise")
        case "wep": return i18nc("@info Wi-Fi security", "WEP")
        case "owe": return i18nc("@info Wi-Fi security", "Enhanced Open")
        default: return i18nc("@info Wi-Fi security", "Open")
        }
    }

    SectionHeader {
        first: true
        text: i18np("1 known network", "%1 known networks", known.count)
    }

    EmptyState {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s7
        visible: known.count === 0
        iconName: "org.raye.settings-known-networks"
        title: i18nc("@info", "No known networks")
        message: i18nc("@info", "Networks you connect to are saved here, so you connect again on your own.")
        Button {
            text: i18nc("@action:button", "Show available networks")
            onClicked: Router.navigate("network/wifi#available-networks")
        }
    }

    Repeater {
        model: known
        Expander {
            id: entry
            required property string name
            required property string uuid
            required property bool autoconnect
            required property string security
            Layout.fillWidth: true
            iconName: "org.raye.settings-wifi"
            iconFallback: "network-wireless"
            title: Net.title(name)
            description: page.securityName(security)
            headerContent: [
                Button {
                    text: i18nc("@action:button", "Forget")
                    Accessible.description: entry.name
                    onClicked: {
                        page.forgetUuid = entry.uuid
                        page.forgetName = entry.name
                        forgetDialog.open()
                    }
                }
            ]
            NetworkConnectionRows {
                network: netStatus
                uuid: entry.uuid
                showAutoconnect: true
                autoconnect: entry.autoconnect
                onAutoconnectToggled: (on) => known.setAutoconnect(entry.uuid, on)
            }
        }
    }

    ContentDialog {
        id: forgetDialog
        title: i18nc("@title:window", "Forget this network?")
        text: i18nc("@info, %1 is a network name", "Raye won't connect to %1 on its own, and you'll need its password to connect again.",
                    Net.styled(page.forgetName))
        primaryText: i18nc("@action:button", "Forget")
        onAccepted: known.forget(page.forgetUuid)
    }
}
