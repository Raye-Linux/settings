// SPDX-License-Identifier: GPL-3.0-or-later
// "Add a VPN connection": the kind of VPN, a name, and the file the VPN
// provider gives. Other kinds go to the KDE editor.
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    required property var vpns
    property string error: ""
    property url file
    property bool _again: false

    title: i18nc("@title:window", "Add a VPN connection")
    primaryText: i18nc("@action:button", "Save")
    primaryEnabled: file.toString() !== "" && !vpns.importing
    closeText: i18nc("@action:button", "Cancel")

    onAboutToShow: {
        if (_again) {
            _again = false
            return
        }
        error = ""
        file = ""
        name.text = ""
        provider.currentIndex = 0
    }
    onAccepted: vpns.importFile(provider.currentValue, file.toString(), name.text)

    Connections {
        target: dialog.vpns
        function onImportFinished(ok, message) {
            if (ok)
                return
            dialog.error = message
            dialog._again = true
            dialog.open()
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Theme.s2

        Text {
            text: i18nc("@label", "VPN provider")
            font: Theme.body
            color: Theme.text
        }
        ComboBox {
            id: provider
            Layout.fillWidth: true
            model: dialog.vpns.providers
            textRole: "name"
            valueRole: "id"
            Accessible.name: i18nc("@label", "VPN provider")
        }

        Text {
            Layout.topMargin: Theme.s4
            text: i18nc("@label", "Connection name")
            font: Theme.body
            color: Theme.text
        }
        TextField {
            id: name
            Layout.fillWidth: true
            maximumLength: 64
            placeholderText: i18nc("@info:placeholder", "Work VPN")
            Accessible.name: i18nc("@label", "Connection name")
        }

        Text {
            Layout.topMargin: Theme.s4
            text: i18nc("@label", "Configuration file from your VPN provider")
            font: Theme.body
            color: Theme.text
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s3
            TextField {
                Layout.fillWidth: true
                readOnly: true
                clearButtonEnabled: false
                text: dialog.file.toString() !== "" ? decodeURIComponent(dialog.file.toString().replace(/^file:\/\//, "")) : ""
                placeholderText: i18nc("@info:placeholder", "No file chosen")
                Accessible.name: i18nc("@label", "Configuration file")
            }
            Button {
                text: i18nc("@action:button", "Browse…")
                onClicked: picker.open()
            }
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
        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s3
            text: i18nc("@info", "Another kind of VPN? Use <a href=\"more\">More VPN options</a>.")
            textFormat: Text.StyledText
            wrapMode: Text.Wrap
            font: Theme.caption
            color: Theme.textSecondary
            linkColor: Theme.accentText
            onLinkActivated: {
                dialog.reject()
                dialog.vpns.editInKde("")
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
        }
    }

    FileDialog {
        id: picker
        title: i18nc("@title:window", "Choose the VPN configuration file")
        fileMode: FileDialog.OpenFile
        nameFilters: provider.currentValue === "wireguard" ? [i18nc("@item file type", "WireGuard configuration (*.conf)")]
                   : provider.currentValue === "openvpn" ? [i18nc("@item file type", "OpenVPN configuration (*.ovpn *.conf)")]
                   : provider.currentValue === "vpnc" ? [i18nc("@item file type", "Cisco profile (*.pcf)")]
                   : []
        onAccepted: dialog.file = selectedFile
    }
}
