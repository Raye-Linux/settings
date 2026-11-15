// SPDX-License-Identifier: GPL-3.0-or-later
// One paired device in Bluetooth & devices > Bluetooth: its icon, name,
// "Connected" or "Paired" with the battery when the device tells it,
// and the "..." menu (Connect or Disconnect, Remove device).
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

T.Control {
    id: row

    // a row of BluetoothSettings.pairedDevices
    required property var model
    property var bluetooth: null

    readonly property string status: {
        if (model.busy)
            return model.connected ? i18nc("@info a Bluetooth device", "Disconnecting…") : i18nc("@info a Bluetooth device", "Connecting…")
        const state = model.connected ? i18nc("@info a Bluetooth device", "Connected") : i18nc("@info a Bluetooth device", "Paired")
        return model.battery >= 0 ? i18nc("@info %1 connected or paired, %2 percent", "%1 · %2% battery", state, model.battery) : state
    }

    signal removeRequested(string address, string name)

    implicitHeight: Theme.cardMinHeight
    Layout.fillWidth: true
    hoverEnabled: true
    Accessible.role: Accessible.ListItem
    Accessible.name: model.name
    Accessible.description: status

    background: Rectangle {
        radius: Theme.radiusControl
        color: row.hovered ? Theme.cardHover : Theme.card
        border.width: 1
        border.color: Theme.cardBorder
    }

    contentItem: RowLayout {
        spacing: 0
        Kirigami.Icon {
            Layout.leftMargin: Theme.cardIconLeft
            Layout.preferredWidth: Theme.iconSize
            Layout.preferredHeight: Theme.iconSize
            source: row.model.iconName
            fallback: "preferences-system-bluetooth"
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.cardTextLeft - Theme.cardIconLeft - Theme.iconSize
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: row.model.name
                font: Theme.body
                color: Theme.text
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                Layout.fillWidth: true
                text: row.status
                font: Theme.caption
                color: Theme.textSecondary
                elide: Text.ElideRight
            }
        }
        T.AbstractButton {
            id: more
            Layout.rightMargin: Theme.s3
            implicitWidth: 36
            implicitHeight: 32
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            enabled: !row.model.busy
            Accessible.role: Accessible.Button
            Accessible.name: i18nc("@action:button %1 is a device", "More options for %1", row.model.name)
            onClicked: menu.open()
            background: Item {
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusControl
                    color: more.down ? Theme.subtlePressed : more.hovered ? Theme.subtleHover : "transparent"
                }
                FocusRing { shown: more.visualFocus }
            }
            contentItem: Item {
                Glyph {
                    anchors.centerIn: parent
                    name: "more"
                    size: 16
                    color: more.enabled ? Theme.text : Theme.textDisabled
                }
            }
            BluetoothDeviceMenu {
                id: menu
                y: more.height + 2
                x: more.width - width
                actions: [row.model.connected ? { id: "disconnect", text: i18nc("@action:inmenu", "Disconnect") }
                                              : { id: "connect", text: i18nc("@action:inmenu", "Connect") },
                          { id: "remove", text: i18nc("@action:inmenu", "Remove device") }]
                onTriggered: (id) => {
                    if (!row.bluetooth)
                        return
                    if (id === "connect")
                        row.bluetooth.connectDevice(row.model.address)
                    else if (id === "disconnect")
                        row.bluetooth.disconnectDevice(row.model.address)
                    else if (id === "remove")
                        row.removeRequested(row.model.address, row.model.name)
                }
            }
        }
    }
}
