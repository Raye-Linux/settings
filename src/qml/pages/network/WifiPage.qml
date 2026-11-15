// SPDX-License-Identifier: GPL-3.0-or-later
// Network & internet › Wi-Fi: the switch, the networks in range (join
// one in place, its password under it), the connected network's
// properties, known networks, random addresses and the adapter.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import "networktext.js" as Net

PageScaffold {
    id: page

    // the open row of the list
    property string selected: ""

    crumbs: [{ text: i18nc("@title settings category", "Network & internet"), target: "network" },
             { text: i18nc("@title settings page", "Wi-Fi") }]

    NetworkStatus { id: netStatus }
    WifiNetworks {
        id: wifi
        active: page.visible
        holdOrder: page.selected !== ""
    }
    KnownNetworks { id: known }

    // the saved settings of the network in use
    readonly property string connectedUuid: netStatus.primaryType === "wifi" ? netStatus.primaryUuid : ""

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: wifi.available && !netStatus.wifiHardwareEnabled
        closable: false
        severity: "warning"
        title: i18nc("@info", "Wi-Fi is turned off by a switch")
        message: i18nc("@info", "Use the Wi-Fi switch or key on your PC to turn it back on.")
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: wifi.available && netStatus.onCable
        closable: false
        title: i18nc("@info", "Connected by cable")
        message: i18nc("@info", "The internet goes through the Ethernet cable. If you unplug it, Wi-Fi takes over when a known network is in range.")
    }

    EmptyState {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s8
        visible: !wifi.available
        iconName: "org.raye.settings-wifi"
        title: i18nc("@info", "No Wi-Fi adapter")
        message: i18nc("@info", "This PC has no Wi-Fi adapter, or its driver is missing. Check Settings › Raye Update › Drivers.")
    }

    ToggleCard {
        Layout.fillWidth: true
        visible: wifi.available
        searchId: "wifi"
        iconName: "org.raye.settings-wifi"
        iconFallback: "network-wireless"
        title: i18nc("@title settings page", "Wi-Fi")
        checked: wifi.enabled
        enabled: netStatus.wifiHardwareEnabled
        onToggled: {
            wifi.enabled = checked
            checked = Qt.binding(() => wifi.enabled)
        }
    }

    Expander {
        Layout.fillWidth: true
        visible: wifi.available && wifi.enabled
        searchId: "available-networks"
        iconName: "org.raye.settings-wifi"
        iconFallback: "network-wireless"
        title: i18nc("@label", "Show available networks")
        description: wifi.count === 0 ? i18nc("@info", "Looking for networks…")
                                          : i18np("1 network in range", "%1 networks in range", wifi.count)
        expanded: true

        Repeater {
            model: wifi
            WifiNetworkRow {
                networks: wifi
                network: netStatus
                selected: page.selected
                onSelect: (ssid) => page.selected = ssid
            }
        }
        ExpanderRow {
            title: i18nc("@label", "Don't see your network?")
            description: i18nc("@info", "Look again for networks in range")
            Button {
                text: i18nc("@action:button", "Refresh")
                onClicked: wifi.scan()
            }
        }
    }

    Expander {
        Layout.fillWidth: true
        visible: page.connectedUuid !== ""
        searchId: "connection-properties"
        iconName: "org.raye.settings-known-networks"
        iconFallback: "network-wireless"
        title: Net.title(i18nc("@label, %1 is a network name", "%1 properties", netStatus.primaryName))
        description: i18nc("@info", "Metered connection, IP and DNS settings")
        NetworkConnectionRows {
            network: netStatus
            uuid: page.connectedUuid
        }
    }

    NavCard {
        Layout.fillWidth: true
        visible: wifi.available
        searchId: "known-networks"
        iconName: "org.raye.settings-known-networks"
        iconFallback: "network-wireless-connected-100"
        title: i18nc("@title settings page", "Manage known networks")
        description: i18np("1 saved network", "%1 saved networks", known.count)
        target: "network/known-networks"
    }

    Expander {
        Layout.fillWidth: true
        visible: wifi.available
        searchId: "hardware-properties"
        iconName: "org.raye.settings-hardware"
        iconFallback: "network-wireless"
        title: i18nc("@label", "Hardware properties")
        description: i18nc("@info", "View and copy the details of this Wi-Fi adapter")
        Item {
            Layout.fillWidth: true
            implicitHeight: hardware.implicitHeight
            NetworkPropertyList {
                id: hardware
                width: parent.width
                color: "transparent"
                border.width: 0
                entries: wifi.hardware
            }
        }
    }

    ToggleCard {
        Layout.fillWidth: true
        visible: wifi.available
        searchId: "random-addresses"
        iconName: "org.raye.settings-privacy"
        iconFallback: "security-high"
        title: i18nc("@label", "Random hardware addresses")
        description: i18nc("@info", "Make it harder for people to track where you go when you connect to Wi-Fi networks. Applies to networks saved on this PC.")
        checked: wifi.randomAddresses
        onToggled: {
            wifi.randomAddresses = checked
            checked = Qt.binding(() => wifi.randomAddresses)
        }
    }
}
