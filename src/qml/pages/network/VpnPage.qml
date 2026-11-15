// SPDX-License-Identifier: GPL-3.0-or-later
// Network & internet › VPN: Add VPN, the saved connections with Connect
// and their options, and "Allow VPN over metered networks".
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import "networktext.js" as Net

PageScaffold {
    id: page

    property string problem: ""
    property string removeUuid: ""
    property string removeName: ""

    crumbs: [{ text: i18nc("@title settings category", "Network & internet"), target: "network" },
             { text: i18nc("@title settings page", "VPN") }]

    VpnConnections {
        id: vpnList
        onConnectFailed: (name, message) => page.problem = message
        onImportFinished: (ok, message) => { if (ok) page.problem = "" }
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.problem !== ""
        severity: "error"
        title: i18nc("@info", "Couldn't connect")
        message: Net.styled(page.problem)
        onClosed: page.problem = ""
    }

    ButtonCard {
        Layout.fillWidth: true
        searchId: "add-vpn"
        iconName: "org.raye.settings-vpn"
        iconFallback: "network-vpn"
        title: i18nc("@label", "VPN connections")
        description: vpnList.count === 0 ? i18nc("@info", "Add a VPN from the file your VPN provider gives you")
                                      : i18np("1 VPN connection", "%1 VPN connections", vpnList.count)
        buttonText: i18nc("@action:button", "Add VPN")
        accent: true
        onButtonClicked: addDialog.open()
    }

    Repeater {
        model: vpnList
        Expander {
            id: entry
            required property string name
            required property string uuid
            required property string kind
            required property string connectionState
            Layout.fillWidth: true
            iconName: "org.raye.settings-vpn"
            iconFallback: "network-vpn"
            title: Net.title(name)
            description: connectionState === "connected" ? i18nc("@info VPN state, %1 is its kind", "Connected · %1", kind)
                       : connectionState === "connecting" ? i18nc("@info VPN state", "Connecting…")
                       : kind
            headerContent: [
                Button {
                    enabled: entry.connectionState !== "connecting"
                    text: entry.connectionState === "off" ? i18nc("@action:button", "Connect") : i18nc("@action:button", "Disconnect")
                    Accessible.description: entry.name
                    onClicked: {
                        if (entry.connectionState === "off") {
                            const problem = vpnList.connectTo(entry.uuid)
                            page.problem = problem
                        } else {
                            vpnList.disconnectFrom(entry.uuid)
                        }
                    }
                }
            ]
            ExpanderRow {
                title: i18nc("@label", "Advanced options")
                description: i18nc("@info", "Server, sign-in and proxy settings, in a separate window")
                clickable: true
                actionGlyph: "open-external"
                onClicked: vpnList.editInKde(entry.uuid)
            }
            ExpanderRow {
                title: i18nc("@label", "Remove this VPN connection")
                Button {
                    text: i18nc("@action:button", "Remove")
                    Accessible.description: entry.name
                    onClicked: {
                        page.removeUuid = entry.uuid
                        page.removeName = entry.name
                        removeDialog.open()
                    }
                }
            }
        }
    }

    SectionHeader { text: i18nc("@title:group", "Advanced settings for all VPN connections") }

    ToggleCard {
        Layout.fillWidth: true
        searchId: "vpn-metered"
        iconName: "org.raye.settings-network-advanced"
        iconFallback: "network-limited"
        title: i18nc("@label", "Allow VPN over metered networks")
        checked: vpnList.overMetered
        onToggled: {
            vpnList.overMetered = checked
            checked = Qt.binding(() => vpnList.overMetered)
        }
    }

    VpnAddDialog {
        id: addDialog
        vpns: vpnList
    }

    ContentDialog {
        id: removeDialog
        title: i18nc("@title:window", "Remove this VPN connection?")
        text: i18nc("@info, %1 is a VPN name", "%1 and its sign-in info will be removed from this PC.", Net.styled(page.removeName))
        primaryText: i18nc("@action:button", "Remove")
        onAccepted: vpnList.remove(page.removeUuid)
    }
}
