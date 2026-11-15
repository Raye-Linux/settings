// SPDX-License-Identifier: GPL-3.0-or-later
// Bluetooth & devices: tiles on top as Windows has them (the Bluetooth
// switch with the name this PC is seen under, the paired devices with
// their battery, "Add device"), then a card for each page this PC has
// the hardware for.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    title: i18nc("@title settings category", "Bluetooth & devices")
    showBack: false

    BluetoothSettings { id: bt }

    function statusOf(connected, battery) {
        const state = connected ? i18nc("@info a Bluetooth device", "Connected") : i18nc("@info a Bluetooth device", "Paired")
        return battery >= 0 ? i18nc("@info %1 connected or paired, %2 percent", "%1 · %2%", state, battery) : state
    }

    banner: Component {
        Flow {
            spacing: Theme.s4
            BluetoothDeviceTile {
                visible: bt.available
                iconName: "preferences-system-bluetooth"
                title: i18nc("@title settings page", "Bluetooth")
                subtitle: bt.powered ? (bt.adapterName !== "" ? i18nc("@info %1 is the name of this PC", "Discoverable as \"%1\"", bt.adapterName)
                                                              : i18nc("@label a setting that is on", "On"))
                                     : i18nc("@label a setting that is off", "Off")
                onClicked: Router.navigate("devices/bluetooth")
                trailing: ToggleSwitch {
                    text: i18nc("@title settings page", "Bluetooth")
                    checked: bt.powered
                    onToggled: {
                        bt.setPowered(checked)
                        checked = Qt.binding(() => bt.powered)
                    }
                }
            }
            Repeater {
                model: bt.powered ? bt.pairedDevices : null
                delegate: BluetoothDeviceTile {
                    required property var model
                    iconName: model.iconName
                    title: model.name
                    subtitle: page.statusOf(model.connected, model.battery)
                    battery: model.battery
                    onClicked: Router.navigate("devices/bluetooth")
                }
            }
            BluetoothDeviceTile {
                iconName: "list-add"
                title: i18nc("@action:button", "Add device")
                subtitle: i18nc("@info", "Bluetooth, printers and more")
                onClicked: addDialog.open()
            }
        }
    }

    NavCard {
        Layout.fillWidth: true
        visible: bt.available || Router.isAvailable("devices/bluetooth")
        iconName: "org.raye.settings-bluetooth"
        iconFallback: "preferences-system-bluetooth"
        title: i18nc("@title settings page", "Bluetooth")
        description: i18nc("@info", "Turn Bluetooth on, add devices, remove them")
        target: "devices/bluetooth"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-printers"
        iconFallback: "printer"
        title: i18nc("@title settings page", "Printers & scanners")
        description: i18nc("@info", "Preferences, troubleshoot")
        target: "devices/printers"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-phone"
        iconFallback: "smartphone"
        title: i18nc("@title settings page", "Phone")
        description: i18nc("@info", "Link your phone to get notifications and send files")
        target: "devices/phone"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-cameras"
        iconFallback: "camera-web"
        title: i18nc("@title settings page", "Cameras")
        description: i18nc("@info", "Connected cameras, default image settings")
        target: "devices/cameras"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-mouse"
        iconFallback: "input-mouse"
        title: i18nc("@title settings page", "Mouse")
        description: i18nc("@info", "Buttons, mouse pointer speed, scrolling")
        target: "devices/mouse"
    }
    NavCard {
        Layout.fillWidth: true
        visible: Router.isAvailable("devices/touchpad")
        iconName: "org.raye.settings-touchpad"
        iconFallback: "input-touchpad"
        title: i18nc("@title settings page", "Touchpad")
        description: i18nc("@info", "Taps, gestures, scrolling, zooming")
        target: "devices/touchpad"
    }
    NavCard {
        Layout.fillWidth: true
        visible: Router.isAvailable("devices/pen")
        iconName: "org.raye.settings-pen"
        iconFallback: "input-tablet"
        title: i18nc("@title settings page", "Pen & tablet")
        description: i18nc("@info", "Drawing tablets and pens")
        target: "devices/pen"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-autoplay"
        iconFallback: "media-removable"
        title: i18nc("@title settings page", "AutoPlay")
        description: i18nc("@info", "Defaults for removable drives and memory cards")
        target: "devices/autoplay"
    }

    AddDeviceDialog {
        id: addDialog
        bluetooth: bt
    }
}
