// SPDX-License-Identifier: GPL-3.0-or-later
// "Edit IP settings", as Windows asks it: Automatic (DHCP) or Manual,
// and for Manual an IPv4 and an IPv6 part, each with its own switch.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    // the NetworkStatus that saves, and the connection it saves to
    required property var network
    property string uuid
    property string error: ""
    // shown again after a wrong value: keep what was typed
    property bool _again: false

    title: i18nc("@title:window", "Edit IP settings")
    primaryText: i18nc("@action:button", "Save")
    closeText: i18nc("@action:button", "Cancel")

    function load() {
        const config = network.ipConfig(uuid)
        const v4 = config.ipv4 || {}
        const v6 = config.ipv6 || {}
        manual.currentIndex = v4.method === "manual" || v6.method === "manual" ? 1 : 0
        ipv4On.checked = v4.method === "manual"
        address4.text = v4.address || ""
        mask4.text = v4.prefix || ""
        gateway4.text = v4.gateway || ""
        ipv6On.checked = v6.method === "manual"
        address6.text = v6.address || ""
        prefix6.text = v6.prefix !== undefined ? String(v6.prefix) : ""
        gateway6.text = v6.gateway || ""
        error = ""
    }

    onAboutToShow: {
        if (!_again)
            load()
        _again = false
    }
    onAccepted: {
        const isManual = manual.currentIndex === 1
        const config = {
            ipv4: isManual && ipv4On.checked
                  ? { method: "manual", address: address4.text, prefix: mask4.text, gateway: gateway4.text }
                  : { method: "auto" },
            ipv6: isManual && ipv6On.checked
                  ? { method: "manual", address: address6.text, prefix: prefix6.text, gateway: gateway6.text }
                  : { method: "auto" }
        }
        const problem = network.setIpConfig(uuid, config)
        if (problem !== "") {
            error = problem
            _again = true
            Qt.callLater(dialog.open)
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.s3

        ComboBox {
            id: manual
            Layout.fillWidth: true
            model: [i18nc("@item IP assignment", "Automatic (DHCP)"), i18nc("@item IP assignment", "Manual")]
            Accessible.name: i18nc("@label", "IP assignment")
        }

        Text {
            Layout.fillWidth: true
            visible: dialog.error !== ""
            text: dialog.error
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font: Theme.caption
            color: Theme.critical
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: manual.currentIndex === 1
            spacing: Theme.s3

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.s3
                Text {
                    Layout.fillWidth: true
                    text: "IPv4"
                    font: Theme.bodyStrong
                    color: Theme.text
                }
                ToggleSwitch { id: ipv4On; text: "IPv4" }
            }
            Field { id: address4; label: i18nc("@label", "IP address"); visible: ipv4On.checked }
            Field { id: mask4; label: i18nc("@label", "Subnet mask"); visible: ipv4On.checked }
            Field { id: gateway4; label: i18nc("@label", "Gateway"); visible: ipv4On.checked }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: Theme.s3
                Text {
                    Layout.fillWidth: true
                    text: "IPv6"
                    font: Theme.bodyStrong
                    color: Theme.text
                }
                ToggleSwitch { id: ipv6On; text: "IPv6" }
            }
            Field { id: address6; label: i18nc("@label", "IP address"); visible: ipv6On.checked }
            Field { id: prefix6; label: i18nc("@label", "Subnet prefix length"); visible: ipv6On.checked }
            Field { id: gateway6; label: i18nc("@label", "Gateway"); visible: ipv6On.checked }
        }
    }

    component Field: ColumnLayout {
        property alias label: caption.text
        property alias text: input.text
        property alias placeholderText: input.placeholderText
        Layout.fillWidth: true
        spacing: Theme.s2
        Text {
            id: caption
            font: Theme.body
            color: Theme.text
        }
        TextField {
            id: input
            Layout.fillWidth: true
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            Accessible.name: caption.text
        }
    }
}
