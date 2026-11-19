// SPDX-License-Identifier: GPL-3.0-or-later
// "Edit DNS settings": Automatic (DHCP), a known service (Cloudflare,
// Quad9, Google) or Manual with a preferred and an alternate server for
// IPv4 and for IPv6. With chosen servers, the lookups can be encrypted
// (DNS over TLS): only, or when the server allows it.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    required property var network
    property string uuid
    property string error: ""
    // shown again after a wrong value: keep what was typed
    property bool _again: false

    title: i18nc("@title:window", "Edit DNS settings")
    primaryText: i18nc("@action:button", "Save")
    closeText: i18nc("@action:button", "Cancel")

    readonly property var presets: network.dnsPresets()
    // 0 automatic, 1..n a preset, n+1 manual
    readonly property int manualIndex: presets.length + 1
    readonly property var chosenPreset: manual.currentIndex > 0 && manual.currentIndex < manualIndex
                                        ? presets[manual.currentIndex - 1] : null
    readonly property var encryptions: ["off", "only", "preferred"]

    function load() {
        const config = network.ipConfig(uuid)
        const dns4 = config.dns4 || []
        const dns6 = config.dns6 || []
        const preset = presets.findIndex(p => p.id === config.dnsPreset)
        manual.currentIndex = config.dnsAuto !== false ? 0 : preset >= 0 ? preset + 1 : manualIndex
        encryption.currentIndex = Math.max(0, encryptions.indexOf(config.dnsEncryption || "off"))
        preferred4.text = dns4.length > 0 ? dns4[0] : ""
        alternate4.text = dns4.length > 1 ? dns4[1] : ""
        preferred6.text = dns6.length > 0 ? dns6[0] : ""
        alternate6.text = dns6.length > 1 ? dns6[1] : ""
        error = ""
    }

    onAboutToShow: {
        if (!_again)
            load()
        _again = false
    }
    onAccepted: {
        const enc = encryptions[encryption.currentIndex]
        const config = manual.currentIndex === 0 ? { dnsAuto: true }
            : chosenPreset ? { dnsAuto: false, dns4: chosenPreset.dns4, dns6: chosenPreset.dns6, dnsEncryption: enc }
            : { dnsAuto: false, dns4: [preferred4.text, alternate4.text], dns6: [preferred6.text, alternate6.text],
                dnsEncryption: enc }
        const problem = network.setDnsConfig(uuid, config)
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
            model: [i18nc("@item DNS assignment", "Automatic (DHCP)")]
                   .concat(dialog.presets.map(p => p.name))
                   .concat([i18nc("@item DNS assignment", "Manual")])
            Accessible.name: i18nc("@label", "DNS server assignment")
            // a known service is worth encrypting; typed servers start plain
            onActivated: (index) => {
                encryption.currentIndex = index > 0 && index < dialog.manualIndex ? 1 : 0
            }
        }

        Text {
            Layout.fillWidth: true
            visible: dialog.chosenPreset !== null
            text: dialog.chosenPreset ? dialog.chosenPreset.dns4.concat(dialog.chosenPreset.dns6).join(", ") : ""
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font: Theme.caption
            color: Theme.textSecondary
        }

        ColumnLayout {
            Layout.fillWidth: true
            visible: manual.currentIndex > 0
            spacing: Theme.s2
            Text {
                text: i18nc("@label", "DNS encryption")
                font: Theme.body
                color: Theme.text
            }
            ComboBox {
                id: encryption
                Layout.fillWidth: true
                model: [i18nc("@item DNS encryption", "Unencrypted only"),
                        i18nc("@item DNS encryption", "Encrypted only (DNS over TLS)"),
                        i18nc("@item DNS encryption", "Encrypted preferred, unencrypted allowed")]
                Accessible.name: i18nc("@label", "DNS encryption")
            }
            Text {
                Layout.fillWidth: true
                visible: encryption.currentIndex === 1
                text: i18nc("@info", "If this network shows a sign-in page (in a hotel or an airport), choose “Encrypted preferred” for it, or the page can't open.")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font: Theme.caption
                color: Theme.textSecondary
            }
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
            visible: manual.currentIndex === dialog.manualIndex
            spacing: Theme.s3

            Text {
                Layout.topMargin: Theme.s3
                text: "IPv4"
                font: Theme.bodyStrong
                color: Theme.text
            }
            Field { id: preferred4; label: i18nc("@label", "Preferred DNS") }
            Field { id: alternate4; label: i18nc("@label", "Alternate DNS") }
            Text {
                Layout.topMargin: Theme.s3
                text: "IPv6"
                font: Theme.bodyStrong
                color: Theme.text
            }
            Field { id: preferred6; label: i18nc("@label", "Preferred DNS") }
            Field { id: alternate6; label: i18nc("@label", "Alternate DNS") }
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
