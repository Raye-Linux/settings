// SPDX-License-Identifier: GPL-3.0-or-later
// "Edit network info" of the mobile hotspot: name, password, band.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    required property var hotspot
    property string error: ""
    property bool _again: false

    title: i18nc("@title:window", "Edit network info")
    primaryText: i18nc("@action:button", "Save")
    closeText: i18nc("@action:button", "Cancel")

    onAboutToShow: {
        if (_again) {
            _again = false
            return
        }
        error = ""
        name.text = hotspot.name
        password.text = hotspot.password
        band.currentIndex = Math.max(0, band.indexOfValue(hotspot.band))
    }
    onAccepted: {
        const problem = hotspot.save(name.text, password.text, band.currentValue)
        if (problem !== "") {
            error = problem
            _again = true
            Qt.callLater(dialog.open)
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.s2

        Text { text: i18nc("@label", "Network name"); font: Theme.body; color: Theme.text }
        TextField {
            id: name
            Layout.fillWidth: true
            maximumLength: 32
            Accessible.name: i18nc("@label", "Network name")
        }
        Text { Layout.topMargin: Theme.s4; text: i18nc("@label", "Network password (at least 8 characters)"); font: Theme.body; color: Theme.text }
        PasswordField {
            id: password
            Layout.fillWidth: true
            maximumLength: 63
            placeholderText: i18nc("@label", "Network password")
        }
        Text { Layout.topMargin: Theme.s4; text: i18nc("@label", "Network band"); font: Theme.body; color: Theme.text }
        ComboBox {
            id: band
            Layout.fillWidth: true
            textRole: "text"
            valueRole: "value"
            model: dialog.hotspot.band5Available
                   ? [{ text: i18nc("@item Wi-Fi band", "Any available"), value: "any" },
                      { text: i18nc("@item Wi-Fi band", "5 GHz"), value: "5" },
                      { text: i18nc("@item Wi-Fi band", "2.4 GHz"), value: "2.4" }]
                   : [{ text: i18nc("@item Wi-Fi band", "2.4 GHz"), value: "2.4" }]
            Accessible.name: i18nc("@label", "Network band")
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
