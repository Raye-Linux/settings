// SPDX-License-Identifier: GPL-3.0-or-later
// Network & internet › Mobile hotspot: share this PC's connection over
// Wi-Fi, with the network name, password and band to give out.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import "networktext.js" as Net

PageScaffold {
    id: page

    property string problem: ""

    crumbs: [{ text: i18nc("@title settings category", "Network & internet"), target: "network" },
             { text: i18nc("@title settings page", "Mobile hotspot") }]

    Hotspot {
        id: share
        onFailed: (message) => page.problem = message
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.problem !== ""
        severity: "error"
        title: i18nc("@info", "The hotspot didn't start")
        message: Net.styled(page.problem)
        onClosed: page.problem = ""
    }
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: !share.available
        closable: false
        severity: "informational"
        title: i18nc("@info", "Mobile hotspot isn't available")
        message: i18nc("@info", "It needs Wi-Fi turned on and a Wi-Fi adapter that can share a connection.")
    }
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: share.available && share.sharesItsOwnWifi && !share.active
        closable: false
        severity: "warning"
        title: i18nc("@info", "This PC is online through the Wi-Fi it would share")
        message: i18nc("@info", "Turning on the hotspot disconnects that Wi-Fi. Connect with a cable first to share it.")
    }

    ToggleCard {
        Layout.fillWidth: true
        searchId: "hotspot"
        iconName: "org.raye.settings-hotspot"
        iconFallback: "network-wireless-hotspot"
        title: i18nc("@title settings page", "Mobile hotspot")
        description: share.active ? i18np("1 device connected", "%1 devices connected", share.clients)
                                    : share.busy ? i18nc("@info", "Starting…")
                                    : i18nc("@info", "Share my internet connection with other devices")
        enabled: share.available && !share.busy
        checked: share.active
        onToggled: {
            page.problem = share.setActive(checked)
            checked = Qt.binding(() => share.active)
        }
    }

    Card {
        Layout.fillWidth: true
        searchId: "share-from"
        iconName: "org.raye.settings-ethernet"
        iconFallback: "network-wired"
        title: i18nc("@label", "Share my internet connection from")
        Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: share.source !== "" ? share.source : i18nc("@info", "Not connected")
            textFormat: Text.PlainText
            font: Theme.body
            color: Theme.textSecondary
        }
    }

    Card {
        Layout.fillWidth: true
        iconName: "org.raye.settings-wifi"
        iconFallback: "network-wireless"
        title: i18nc("@label", "Share over")
        Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: i18nc("@info", "Wi-Fi")
            font: Theme.body
            color: Theme.textSecondary
        }
    }

    SectionHeader { text: i18nc("@title:group", "Properties") }

    NetworkPropertyList {
        searchId: "hotspot-properties"
        entries: [{ label: i18nc("@label", "Name"), value: share.name },
                  { label: i18nc("@label", "Password"), value: share.password },
                  { label: i18nc("@label", "Band"),
                    value: share.band === "5" ? i18nc("@item Wi-Fi band", "5 GHz")
                         : share.band === "2.4" ? i18nc("@item Wi-Fi band", "2.4 GHz")
                         : i18nc("@item Wi-Fi band", "Any available") }]
    }
    ButtonCard {
        Layout.fillWidth: true
        searchId: "hotspot-edit"
        iconName: "org.raye.settings-hotspot"
        iconFallback: "document-edit"
        title: i18nc("@label", "Network name, password and band")
        description: i18nc("@info", "Devices that join need this name and password")
        buttonText: i18nc("@action:button", "Edit")
        onButtonClicked: editDialog.open()
    }

    HotspotEditDialog {
        id: editDialog
        hotspot: share
    }
}
