// SPDX-License-Identifier: GPL-3.0-or-later
// Network & internet › Ethernet: for each wired adapter, network profile
// type, metered, IP and DNS with their Edit dialogs, and its properties
// with Copy.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import "networktext.js" as Net

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Network & internet"), target: "network" },
             { text: i18nc("@title settings page", "Ethernet") }]

    NetworkStatus { id: netStatus }

    EmptyState {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s8
        visible: netStatus.ethernet.length === 0
        iconName: "org.raye.settings-ethernet"
        title: i18nc("@info", "No Ethernet adapter")
        message: netStatus.wording.noEthernet
    }

    Repeater {
        model: netStatus.ethernet
        ColumnLayout {
            id: adapter
            required property var modelData
            required property int index
            readonly property bool editable: modelData.uuid !== ""
            Layout.fillWidth: true
            spacing: Theme.cardSpacing

            SectionHeader {
                first: adapter.index === 0
                visible: netStatus.ethernet.length > 1
                text: adapter.modelData.interface
            }

            // what it is connected to
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 72
                radius: Theme.radiusControl
                color: Theme.card
                border.width: 1
                border.color: Theme.cardBorder
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: Theme.cardIconLeft
                    anchors.rightMargin: Theme.cardPadding
                    spacing: Theme.s6
                    NetworkGlyph {
                        kind: "ethernet"
                        size: 28
                        color: adapter.modelData.connected ? Theme.accentText : Theme.textSecondary
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: adapter.modelData.name
                            textFormat: Text.PlainText
                            elide: Text.ElideRight
                            font: Theme.bodyStrong
                            color: Theme.text
                        }
                        Text {
                            Layout.fillWidth: true
                            text: adapter.modelData.statusText
                            textFormat: Text.PlainText
                            font: Theme.caption
                            color: adapter.modelData.connected ? Theme.accentText : Theme.textSecondary
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                visible: adapter.editable
                implicitHeight: profileChoice.implicitHeight
                radius: Theme.radiusControl
                color: Theme.card
                border.width: 1
                border.color: Theme.cardBorder
                NetworkProfileChoice {
                    id: profileChoice
                    width: parent.width
                    network: netStatus
                    uuid: adapter.modelData.uuid
                }
            }

            ToggleCard {
                Layout.fillWidth: true
                enabled: adapter.editable
                searchId: "metered"
                iconName: "org.raye.settings-network-advanced"
                iconFallback: "network-limited"
                title: i18nc("@label", "Metered connection")
                description: i18nc("@info", "Some apps might work differently to use less data when you're connected to this network")
                checked: adapter.modelData.metered
                onToggled: {
                    netStatus.setMetered(adapter.modelData.uuid, checked)
                    checked = Qt.binding(() => adapter.modelData.metered)
                }
            }

            ButtonCard {
                Layout.fillWidth: true
                enabled: adapter.editable
                searchId: "ip-assignment"
                iconName: "org.raye.settings-network-advanced"
                iconFallback: "network-server"
                title: i18nc("@label", "IP assignment:")
                description: adapter.modelData.ipAuto ? i18nc("@info IP assignment", "Automatic (DHCP)")
                                                      : i18nc("@info IP assignment", "Manual")
                buttonText: i18nc("@action:button", "Edit")
                onButtonClicked: {
                    ipDialog.uuid = adapter.modelData.uuid
                    ipDialog.open()
                }
            }

            ButtonCard {
                Layout.fillWidth: true
                enabled: adapter.editable
                searchId: "dns"
                iconName: "org.raye.settings-network-advanced"
                iconFallback: "network-server"
                title: i18nc("@label", "DNS server assignment:")
                description: {
                    if (adapter.modelData.dnsAuto)
                        return i18nc("@info DNS assignment", "Automatic (DHCP)")
                    // read again when the adapter's entry changes
                    const c = netStatus.ipConfig(adapter.modelData.uuid)
                    const preset = netStatus.dnsPresets().find(p => p.id === c.dnsPreset)
                    const what = preset ? preset.name : i18nc("@info DNS assignment", "Manual")
                    return c.dnsEncryption === "only" ? i18nc("@info %1 is a DNS server or a list of them", "%1, encrypted", what)
                         : c.dnsEncryption === "preferred" ? i18nc("@info %1 is a DNS server or a list of them", "%1, encrypted when possible", what)
                         : what
                }
                buttonText: i18nc("@action:button", "Edit")
                onButtonClicked: {
                    dnsDialog.uuid = adapter.modelData.uuid
                    dnsDialog.open()
                }
            }

            NetworkPropertyList {
                searchId: "ethernet-properties"
                entries: {
                    const m = adapter.modelData
                    const list = []
                    const add = (label, value) => { if (value) list.push({ label: label, value: value }) }
                    add(i18nc("@label", "Link speed (Receive/Transmit)"), m.speed ? m.speed + "/" + m.speed : "")
                    add(i18nc("@label", "IPv6 address"), m.ipv6)
                    add(i18nc("@label", "IPv4 address"), m.ipv4)
                    add(i18nc("@label", "Default gateway"), m.gateway)
                    add(i18nc("@label", "IPv4 DNS servers"), m.dns)
                    add(i18nc("@label", "Adapter"), m.interface)
                    add(i18nc("@label", "Driver"), m.driver)
                    add(i18nc("@label", "Physical address (MAC)"), m.mac)
                    return list
                }
            }

            Card {
                Layout.fillWidth: true
                iconName: "org.raye.settings-network-advanced"
                iconFallback: "preferences-system-network"
                title: i18nc("@label", "Authentication and other settings")
                description: i18nc("@info", "802.1X sign-in, MTU and more, in a separate window")
                clickable: true
                actionGlyph: "open-external"
                onClicked: netStatus.openMoreOptions(adapter.modelData.uuid)
            }
        }
    }

    NetworkIpDialog {
        id: ipDialog
        network: netStatus
    }
    NetworkDnsDialog {
        id: dnsDialog
        network: netStatus
    }
}
