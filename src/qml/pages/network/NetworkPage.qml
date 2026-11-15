// SPDX-License-Identifier: GPL-3.0-or-later
// Network & internet: the connection banner, then one card per page,
// with the Wi-Fi, hotspot and airplane mode switches on their cards.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    title: i18nc("@title settings category", "Network & internet")
    showBack: false

    NetworkStatus { id: netStatus }
    Hotspot { id: share }

    banner: Component {
        NetworkBanner { network: netStatus }
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: netStatus.portal
        closable: false
        severity: "warning"
        title: i18nc("@info", "Action needed")
        message: i18nc("@info", "This network asks you to sign in before you can use the internet.")
        Button {
            text: i18nc("@action:button", "Open sign-in page")
            onClicked: Qt.openUrlExternally(netStatus.portalUrl)
        }
    }

    Card {
        Layout.fillWidth: true
        visible: netStatus.hasWifi
        searchId: "wifi"
        iconName: "org.raye.settings-wifi"
        iconFallback: "network-wireless"
        title: i18nc("@title settings page", "Wi-Fi")
        description: netStatus.wifiSummary
        clickable: true
        actionGlyph: "chevron-right"
        onClicked: Router.navigate("network/wifi")
        Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: wifiSwitch.checked ? i18nc("@label a setting that is on", "On") : i18nc("@label a setting that is off", "Off")
            font: Theme.body
            color: Theme.text
        }
        ToggleSwitch {
            id: wifiSwitch
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: i18nc("@title settings page", "Wi-Fi")
            checked: netStatus.wifiEnabled
            enabled: netStatus.wifiHardwareEnabled
            onToggled: {
                netStatus.wifiEnabled = checked
                checked = Qt.binding(() => netStatus.wifiEnabled)
            }
        }
    }

    NavCard {
        Layout.fillWidth: true
        visible: netStatus.hasEthernet
        iconName: "org.raye.settings-ethernet"
        iconFallback: "network-wired"
        title: i18nc("@title settings page", "Ethernet")
        description: netStatus.primaryType === "ethernet" ? i18nc("@info", "Authentication, IP and DNS settings, metered network")
                                                         : i18nc("@info", "Not connected")
        target: "network/ethernet"
    }

    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-vpn"
        iconFallback: "network-vpn"
        title: i18nc("@title settings page", "VPN")
        description: i18nc("@info", "Add, connect, manage")
        target: "network/vpn"
    }

    Card {
        Layout.fillWidth: true
        visible: share.available || netStatus.hasWifi
        searchId: "hotspot"
        iconName: "org.raye.settings-hotspot"
        iconFallback: "network-wireless-hotspot"
        title: i18nc("@title settings page", "Mobile hotspot")
        description: i18nc("@info", "Share your internet connection")
        clickable: true
        actionGlyph: "chevron-right"
        onClicked: Router.navigate("network/hotspot")
        Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: hotspotSwitch.checked ? i18nc("@label a setting that is on", "On") : i18nc("@label a setting that is off", "Off")
            font: Theme.body
            color: hotspotSwitch.enabled ? Theme.text : Theme.textDisabled
        }
        ToggleSwitch {
            id: hotspotSwitch
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: i18nc("@title settings page", "Mobile hotspot")
            checked: share.active
            enabled: share.available && !share.busy
            onToggled: {
                share.setActive(checked)
                checked = Qt.binding(() => share.active)
            }
        }
    }

    Card {
        Layout.fillWidth: true
        visible: netStatus.hasWifi || netStatus.hasBluetooth
        searchId: "airplane-mode"
        iconName: "org.raye.settings-airplane-mode"
        iconFallback: "airplane-mode"
        title: i18nc("@title settings page", "Airplane mode")
        description: i18nc("@info", "Stop all wireless communication")
        clickable: true
        actionGlyph: "chevron-right"
        onClicked: Router.navigate("network/airplane-mode")
        Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: airplaneSwitch.checked ? i18nc("@label a setting that is on", "On") : i18nc("@label a setting that is off", "Off")
            font: Theme.body
            color: Theme.text
        }
        ToggleSwitch {
            id: airplaneSwitch
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: i18nc("@title settings page", "Airplane mode")
            checked: netStatus.airplaneMode
            onToggled: {
                netStatus.airplaneMode = checked
                checked = Qt.binding(() => netStatus.airplaneMode)
            }
        }
    }

    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-proxy"
        iconFallback: "preferences-system-network-proxy"
        title: i18nc("@title settings page", "Proxy")
        description: netStatus.wording.proxy
        target: "network/proxy"
    }

    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-network-advanced"
        iconFallback: "preferences-system-network"
        title: i18nc("@title settings page", "Advanced network settings")
        description: i18nc("@info", "View all network adapters, network reset")
        target: "network/advanced"
    }
}
