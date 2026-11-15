// SPDX-License-Identifier: GPL-3.0-or-later
// Bluetooth devices on the launch screen: the three paired devices most
// likely to matter (connected ones first) with their battery, "View
// all" and "Add device". Loaded on its own, so the launch screen still
// opens on a computer where Bluetooth is missing.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.bluezqt as BluezQt
import org.raye.settings

ForYouCard {
    id: card

    cardId: "bluetooth-devices"
    searchId: "bluetooth-devices"
    title: i18nc("@title card on the launch screen", "Bluetooth devices")

    BluezQt.DevicesModel { id: devices }

    // paired devices, connected first, at most three
    property int revision: 0
    readonly property var shown: {
        void revision
        const list = []
        for (let i = 0; i < repeater.count; ++i) {
            const row = repeater.itemAt(i)
            if (row && row.paired)
                list.push(row)
        }
        list.sort((a, b) => (b.connected ? 1 : 0) - (a.connected ? 1 : 0))
        return list.slice(0, 3)
    }

    // reads the model; the rows shown are copies laid out below
    Repeater {
        id: repeater
        model: devices
        onItemAdded: card.revision++
        onItemRemoved: card.revision++
        delegate: Item {
            required property var model
            readonly property bool paired: model.Paired === true
            readonly property bool connected: model.Connected === true
            readonly property string name: model.Name || ""
            readonly property string icon: model.Icon || "preferences-system-bluetooth"
            readonly property int battery: model.Device && model.Device.battery ? model.Device.battery.percentage : -1
            visible: false
            onPairedChanged: card.revision++
            onConnectedChanged: card.revision++
            onBatteryChanged: card.revision++
        }
    }

    Repeater {
        model: card.shown
        delegate: RowLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: Theme.s4
            Kirigami.Icon {
                implicitWidth: 20
                implicitHeight: 20
                source: modelData.icon
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: modelData.name
                    font: Theme.body
                    color: Theme.text
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    horizontalAlignment: Text.AlignLeft
                }
                Text {
                    Layout.fillWidth: true
                    text: modelData.connected ? i18nc("@info a Bluetooth device", "Connected") : i18nc("@info a Bluetooth device", "Not connected")
                    font: Theme.caption
                    color: Theme.textSecondary
                    horizontalAlignment: Text.AlignLeft
                }
            }
            Text {
                visible: modelData.battery >= 0
                text: i18nc("@info battery level", "%1%", modelData.battery)
                font: Theme.caption
                color: Theme.textSecondary
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: card.shown.length === 0
        text: i18n("No devices yet.")
        font: Theme.caption
        color: Theme.textSecondary
        horizontalAlignment: Text.AlignLeft
    }

    RowLayout {
        spacing: Theme.s5
        HyperlinkButton {
            font: Theme.captionStrong
            text: i18nc("@action:button opens Bluetooth & devices", "View all")
            onClicked: Router.navigate("devices/bluetooth")
        }
        HyperlinkButton {
            font: Theme.captionStrong
            text: i18nc("@action:button", "Add device")
            onClicked: Router.navigate("devices/bluetooth#add-device")
        }
    }
}
