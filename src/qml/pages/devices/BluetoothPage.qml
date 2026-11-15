// SPDX-License-Identifier: GPL-3.0-or-later
// Bluetooth & devices > Bluetooth: the switch with the name this PC is
// seen under, "Add device", then the paired devices in Windows' groups
// (mice, keyboards & pens; audio; other devices), each with its battery
// and a "..." menu to connect, disconnect or remove it.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Bluetooth & devices"), target: "devices" },
             { text: i18nc("@title settings page", "Bluetooth") }]

    BluetoothSettings {
        id: bt
        onActionFailed: (address, message) => page.failure = message
    }

    property string failure: ""
    property string removeAddress: ""

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.failure !== ""
        severity: "error"
        title: i18nc("@info", "That didn't work")
        message: page.failure
        onClosed: page.failure = ""
    }

    EmptyState {
        Layout.fillWidth: true
        visible: !bt.available
        iconName: "org.raye.settings-bluetooth"
        title: i18nc("@info", "Bluetooth isn't available on this PC")
        message: i18nc("@info", "If your PC has Bluetooth, make sure it is not turned off in the firmware settings.")
    }

    Card {
        Layout.fillWidth: true
        visible: bt.available
        iconName: "org.raye.settings-bluetooth"
        iconFallback: "preferences-system-bluetooth"
        title: i18nc("@title settings page", "Bluetooth")
        description: bt.powered && bt.adapterName !== ""
                     ? i18nc("@info %1 is the name of this PC", "Discoverable as \"%1\"", bt.adapterName) : ""
        searchId: "bluetooth"
        Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: bt.powered ? i18nc("@label a setting that is on", "On") : i18nc("@label a setting that is off", "Off")
            font: Theme.body
            color: Theme.text
        }
        ToggleSwitch {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: i18nc("@title settings page", "Bluetooth")
            checked: bt.powered
            onToggled: {
                bt.setPowered(checked)
                checked = Qt.binding(() => bt.powered)
            }
        }
    }

    ButtonCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-devices"
        iconFallback: "list-add"
        title: i18nc("@label", "Devices")
        description: i18nc("@info", "Mouse, keyboard, pen, audio, displays and docks, other devices")
        buttonText: i18nc("@action:button", "Add device")
        searchId: "add-device"
        onButtonClicked: addDialog.open()
    }

    // a new reading of the devices: the group headers count again
    property int revision: 0
    Connections {
        target: bt.pairedDevices
        function onDevicesChanged() { page.revision++ }
    }

    Repeater {
        model: [{ id: "input", title: i18nc("@title:group", "Mice, keyboards & pens") },
                { id: "audio", title: i18nc("@title:group", "Audio") },
                { id: "other", title: i18nc("@title:group", "Other devices") }]
        delegate: ColumnLayout {
            id: groupColumn
            required property var modelData
            Layout.fillWidth: true
            spacing: Theme.cardSpacing
            visible: bt.powered && (page.revision, bt.pairedDevices.countIn(modelData.id)) > 0
            SectionHeader { text: groupColumn.modelData.title }
            Repeater {
                model: bt.pairedDevices
                delegate: BluetoothDeviceRow {
                    bluetooth: bt
                    visible: model.group === groupColumn.modelData.id
                    onRemoveRequested: (address, name) => {
                        page.removeAddress = address
                        removeDialog.title = i18nc("@title:window %1 is a device", "Remove %1?", name)
                        removeDialog.open()
                    }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s5
        visible: bt.available && bt.powered && bt.pairedDevices.count === 0
        text: i18nc("@info", "No devices yet. Select Add device to pair a mouse, a keyboard, headphones or a phone.")
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-phone"
        iconFallback: "smartphone"
        title: i18nc("@title settings page", "Phone")
        description: i18nc("@info", "Link your phone, send files")
        target: "devices/phone"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-sound"
        iconFallback: "audio-speakers"
        title: i18nc("@title settings page", "Sound")
        description: i18nc("@info", "Choose where sound plays")
        target: "system/sound"
    }

    AddDeviceDialog {
        id: addDialog
        bluetooth: bt
    }

    ContentDialog {
        id: removeDialog
        text: i18nc("@info", "To use it again, you will need to pair it again.")
        primaryText: i18nc("@action:button", "Remove")
        onAccepted: bt.removeDevice(page.removeAddress)
    }
}
