// SPDX-License-Identifier: GPL-3.0-or-later
// The settings of one saved Wi-Fi connection, as rows of an expander:
// connect automatically, network profile type, private address,
// metered, IP and DNS with their Edit dialogs, and the KDE editor for
// the rest.
//
//   Expander { NetworkConnectionRows { network: net; uuid: "…" } }
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ColumnLayout {
    id: rows

    required property var network
    property string uuid
    // for Wi-Fi: "Connect automatically when in range"
    property bool showAutoconnect: false
    property bool autoconnect: true
    signal autoconnectToggled(bool on)
    // what the rows show, read again when the connection changes
    property var config: ({})
    property bool metered: false
    property bool randomAddress: true

    Layout.fillWidth: true
    spacing: 0

    // "Automatic (DHCP)", "Cloudflare, encrypted", "Manual: 1.1.1.1, …"
    function dnsSummary(c) {
        if (c.dnsAuto !== false)
            return i18nc("@info DNS assignment", "Automatic (DHCP)")
        const preset = (network.dnsPresets() || []).find(p => p.id === c.dnsPreset)
        const what = preset ? preset.name
                            : i18nc("@info", "Manual: %1", (c.dns4 || []).concat(c.dns6 || []).join(", "))
        return c.dnsEncryption === "only" ? i18nc("@info %1 is a DNS server or a list of them", "%1, encrypted", what)
             : c.dnsEncryption === "preferred" ? i18nc("@info %1 is a DNS server or a list of them", "%1, encrypted when possible", what)
             : what
    }
    function reload() {
        if (uuid === "")
            return
        config = network.ipConfig(uuid)
        metered = network.isMetered(uuid)
        randomAddress = network.randomAddress(uuid)
    }
    Component.onCompleted: reload()
    onUuidChanged: reload()
    Connections {
        target: rows.network
        function onChanged() { rows.reload() }
    }

    ExpanderRow {
        visible: rows.showAutoconnect
        title: i18nc("@label", "Connect automatically when in range")
        ToggleSwitch {
            checked: rows.autoconnect
            text: i18nc("@label", "Connect automatically when in range")
            onToggled: {
                rows.autoconnectToggled(checked)
                checked = Qt.binding(() => rows.autoconnect)
            }
        }
    }
    NetworkProfileChoice {
        network: rows.network
        uuid: rows.uuid
        inExpander: true
    }
    ExpanderRow {
        title: i18nc("@label", "Random hardware addresses")
        description: i18nc("@info", "This network sees an address of its own for this PC, so it's harder to follow you from place to place. Counts from the next connection.")
        ToggleSwitch {
            checked: rows.randomAddress
            text: i18nc("@label", "Random hardware addresses")
            onToggled: {
                rows.network.setRandomAddress(rows.uuid, checked)
                rows.reload()
                checked = Qt.binding(() => rows.randomAddress)
            }
        }
    }
    ExpanderRow {
        title: i18nc("@label", "Metered connection")
        description: i18nc("@info", "Some apps might work differently to use less data on this network")
        ToggleSwitch {
            checked: rows.metered
            text: i18nc("@label", "Metered connection")
            onToggled: {
                rows.network.setMetered(rows.uuid, checked)
                rows.reload()
                checked = Qt.binding(() => rows.metered)
            }
        }
    }
    ExpanderRow {
        title: i18nc("@label", "IP assignment")
        description: rows.config.ipv4 && rows.config.ipv4.method === "manual"
                     ? i18nc("@info", "Manual: %1", rows.config.ipv4.address || "")
                     : i18nc("@info IP assignment", "Automatic (DHCP)")
        Button {
            text: i18nc("@action:button", "Edit")
            Accessible.description: i18nc("@label", "IP assignment")
            onClicked: ipDialog.open()
        }
    }
    ExpanderRow {
        title: i18nc("@label", "DNS server assignment")
        description: rows.dnsSummary(rows.config)
        Button {
            text: i18nc("@action:button", "Edit")
            Accessible.description: i18nc("@label", "DNS server assignment")
            onClicked: dnsDialog.open()
        }
    }
    ExpanderRow {
        title: i18nc("@label", "More options for this network")
        description: i18nc("@info", "Proxy, security and sign-in settings, in a separate window")
        clickable: true
        actionGlyph: "open-external"
        onClicked: rows.network.openMoreOptions(rows.uuid)
    }

    NetworkIpDialog {
        id: ipDialog
        network: rows.network
        uuid: rows.uuid
        onClosed: rows.reload()
    }
    NetworkDnsDialog {
        id: dnsDialog
        network: rows.network
        uuid: rows.uuid
        onClosed: rows.reload()
    }
}
