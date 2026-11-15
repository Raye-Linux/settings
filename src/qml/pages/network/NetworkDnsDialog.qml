// SPDX-License-Identifier: GPL-3.0-or-later
// "Edit DNS settings": Automatic (DHCP) or Manual with a preferred and
// an alternate server for IPv4 and for IPv6. Encrypted DNS is not
// offered.
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

    function load() {
        const config = network.ipConfig(uuid)
        manual.currentIndex = config.dnsAuto === false ? 1 : 0
        const dns4 = config.dns4 || []
        const dns6 = config.dns6 || []
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
        const config = manual.currentIndex === 1
            ? { dnsAuto: false, dns4: [preferred4.text, alternate4.text], dns6: [preferred6.text, alternate6.text] }
            : { dnsAuto: true }
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
            model: [i18nc("@item DNS assignment", "Automatic (DHCP)"), i18nc("@item DNS assignment", "Manual")]
            Accessible.name: i18nc("@label", "DNS server assignment")
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
