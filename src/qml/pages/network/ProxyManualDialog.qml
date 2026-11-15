// SPDX-License-Identifier: GPL-3.0-or-later
// "Edit proxy server": the address and port, the addresses that go
// around it, and local addresses.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    required property var proxy
    property string error: ""
    property bool _again: false

    title: i18nc("@title:window", "Edit proxy server")
    primaryText: i18nc("@action:button", "Save")
    closeText: i18nc("@action:button", "Cancel")

    onAboutToShow: {
        if (_again) {
            _again = false
            return
        }
        error = ""
        use.checked = proxy.useManual
        host.text = proxy.host
        port.text = proxy.port > 0 ? String(proxy.port) : ""
        exceptions.text = proxy.exceptions
        local.checked = proxy.bypassLocal
    }
    onAccepted: {
        const problem = proxy.setManual(use.checked, host.text, parseInt(port.text) || 0, exceptions.text, local.checked)
        if (problem !== "") {
            error = problem
            _again = true
            Qt.callLater(dialog.open)
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.s2

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: i18nc("@label", "Use a proxy server")
                font: Theme.body
                color: Theme.text
            }
            ToggleSwitch { id: use; text: i18nc("@label", "Use a proxy server") }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s4
            spacing: Theme.s3
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s2
                Text { text: i18nc("@label", "Proxy IP address"); font: Theme.body; color: use.checked ? Theme.text : Theme.textDisabled }
                TextField {
                    id: host
                    Layout.fillWidth: true
                    enabled: use.checked
                    placeholderText: "proxy.example.com"
                    inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
                    Accessible.name: i18nc("@label", "Proxy IP address")
                }
            }
            ColumnLayout {
                spacing: Theme.s2
                Text { text: i18nc("@label", "Port"); font: Theme.body; color: use.checked ? Theme.text : Theme.textDisabled }
                TextField {
                    id: port
                    Layout.preferredWidth: 96
                    enabled: use.checked
                    placeholderText: "8080"
                    clearButtonEnabled: false
                    maximumLength: 5
                    inputMethodHints: Qt.ImhDigitsOnly
                    validator: IntValidator { bottom: 1; top: 65535 }
                    Accessible.name: i18nc("@label", "Port")
                }
            }
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s4
            text: i18nc("@label", "Use the proxy server except for addresses that start with the following entries. Use semicolons (;) to separate entries.")
            wrapMode: Text.Wrap
            font: Theme.body
            color: use.checked ? Theme.text : Theme.textDisabled
        }
        TextField {
            id: exceptions
            Layout.fillWidth: true
            enabled: use.checked
            placeholderText: "*.example.com; intranet"
            Accessible.name: i18nc("@label", "Addresses that don't use the proxy")
        }
        NetworkCheckBox {
            id: local
            Layout.topMargin: Theme.s3
            enabled: use.checked
            text: i18nc("@option:check", "Don't use the proxy server for local addresses")
        }
        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s3
            visible: dialog.error !== ""
            text: dialog.error
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font: Theme.caption
            color: Theme.critical
        }
    }
}
