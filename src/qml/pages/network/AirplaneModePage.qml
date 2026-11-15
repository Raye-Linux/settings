// SPDX-License-Identifier: GPL-3.0-or-later
// Network & internet › Airplane mode: one switch for every radio, and
// Wi-Fi and Bluetooth on their own under it.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Network & internet"), target: "network" },
             { text: i18nc("@title settings page", "Airplane mode") }]

    NetworkStatus { id: netStatus }

    // the big glyph, lit while airplane mode is on
    Item {
        Layout.fillWidth: true
        implicitHeight: 96
        Rectangle {
            id: disc
            width: 72
            height: 72
            radius: 36
            anchors.verticalCenter: parent.verticalCenter
            x: page.LayoutMirroring.enabled ? parent.width - width : 0
            color: netStatus.airplaneMode ? Theme.accent : Theme.control
            border.width: netStatus.airplaneMode ? 0 : 1
            border.color: Theme.strokeControl
            Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
            NetworkGlyph {
                anchors.centerIn: parent
                kind: "airplane"
                size: 36
                color: netStatus.airplaneMode ? Theme.textOnAccent : Theme.textSecondary
            }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            x: page.LayoutMirroring.enabled ? 0 : disc.width + Theme.s6
            width: parent.width - disc.width - Theme.s6
            text: netStatus.airplaneMode ? netStatus.wording.airplaneOn : netStatus.wording.airplaneOff
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            font: Theme.body
            color: Theme.textSecondary
            horizontalAlignment: Text.AlignLeft
        }
    }

    ToggleCard {
        Layout.fillWidth: true
        searchId: "airplane-mode"
        iconName: "org.raye.settings-airplane-mode"
        iconFallback: "airplane-mode"
        title: i18nc("@title settings page", "Airplane mode")
        description: i18nc("@info", "Stop all wireless communication")
        checked: netStatus.airplaneMode
        onToggled: {
            netStatus.airplaneMode = checked
            checked = Qt.binding(() => netStatus.airplaneMode)
        }
    }

    SectionHeader {
        text: i18nc("@title:group", "Wireless devices")
        visible: netStatus.hasWifi || netStatus.hasBluetooth
    }

    ToggleCard {
        Layout.fillWidth: true
        visible: netStatus.hasWifi
        searchId: "airplane-wifi"
        iconName: "org.raye.settings-wifi"
        iconFallback: "network-wireless"
        title: i18nc("@title settings page", "Wi-Fi")
        checked: netStatus.wifiEnabled
        enabled: netStatus.wifiHardwareEnabled
        onToggled: {
            netStatus.wifiEnabled = checked
            checked = Qt.binding(() => netStatus.wifiEnabled)
        }
    }

    ToggleCard {
        Layout.fillWidth: true
        visible: netStatus.hasBluetooth
        searchId: "airplane-bluetooth"
        iconName: "org.raye.settings-bluetooth"
        iconFallback: "preferences-system-bluetooth"
        title: i18nc("@title", "Bluetooth")
        checked: netStatus.bluetoothEnabled
        onToggled: {
            netStatus.bluetoothEnabled = checked
            checked = Qt.binding(() => netStatus.bluetoothEnabled)
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s4
        visible: netStatus.hasWifi
        text: i18nc("@info", "Turning Wi-Fi back on ends airplane mode, as the network icon on the taskbar does.")
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
        font: Theme.caption
        color: Theme.textTertiary
    }
}
