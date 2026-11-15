// SPDX-License-Identifier: GPL-3.0-or-later
// "Edit setup script": use a proxy auto-configuration script, and where
// it is.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    required property var proxy
    property string error: ""
    property bool _again: false

    title: i18nc("@title:window", "Edit setup script")
    primaryText: i18nc("@action:button", "Save")
    closeText: i18nc("@action:button", "Cancel")

    onAboutToShow: {
        if (_again) {
            _again = false
            return
        }
        error = ""
        use.checked = proxy.useScript
        address.text = proxy.scriptUrl
    }
    onAccepted: {
        const problem = proxy.setScript(use.checked, address.text)
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
                text: i18nc("@label", "Use setup script")
                font: Theme.body
                color: Theme.text
            }
            ToggleSwitch { id: use; text: i18nc("@label", "Use setup script") }
        }
        Text {
            Layout.topMargin: Theme.s4
            text: i18nc("@label", "Script address")
            font: Theme.body
            color: use.checked ? Theme.text : Theme.textDisabled
        }
        TextField {
            id: address
            Layout.fillWidth: true
            enabled: use.checked
            placeholderText: "http://example.com/proxy.pac"
            inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase
            Accessible.name: i18nc("@label", "Script address")
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
