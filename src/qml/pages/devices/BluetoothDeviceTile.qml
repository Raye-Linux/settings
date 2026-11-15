// SPDX-License-Identifier: GPL-3.0-or-later
// A tile on top of Bluetooth & devices (Windows' header cards): a
// device with its battery, the Bluetooth switch, or "Add device".
// 238 x 96 (four in a row of the content width), radius 8.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

T.AbstractButton {
    id: tile

    property string iconName
    property string title
    property string subtitle
    // a percent to draw as a small battery, -1 for none
    property int battery: -1
    // something on the right of the title row (the Bluetooth switch)
    property alias trailing: trailingHolder.data

    implicitWidth: 238
    implicitHeight: 96
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: Theme.cardPadding
    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.description: subtitle

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusOverlay
            color: tile.down ? Theme.cardPressed : tile.hovered ? Theme.cardHover : Theme.card
            border.width: 1
            border.color: tile.hovered ? Theme.strokeSurface : Theme.cardBorder
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
        FocusRing { shown: tile.visualFocus; radius: Theme.radiusOverlay }
    }

    contentItem: Item {
        Kirigami.Icon {
            id: icon
            width: 24
            height: 24
            source: tile.iconName
            fallback: "preferences-system-bluetooth"
        }
        Item {
            id: trailingHolder
            anchors.right: parent.right
            anchors.top: parent.top
            width: childrenRect.width
            height: childrenRect.height
        }
        Column {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            Text {
                width: parent.width
                text: tile.title
                font: Theme.body
                color: Theme.text
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Row {
                spacing: Theme.s2
                // a little battery, filled as much as it is charged
                Item {
                    visible: tile.battery >= 0
                    width: 18
                    height: 16
                    Rectangle {
                        anchors.verticalCenter: parent.verticalCenter
                        width: 15
                        height: 8
                        radius: 2
                        color: "transparent"
                        border.width: 1
                        border.color: Theme.textSecondary
                        Rectangle {
                            x: 2
                            y: 2
                            width: Math.max(1, (parent.width - 4) * tile.battery / 100)
                            height: parent.height - 4
                            radius: 1
                            color: tile.battery <= 15 ? Theme.critical : Theme.textSecondary
                        }
                    }
                    Rectangle {
                        x: 15
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2
                        height: 4
                        color: Theme.textSecondary
                    }
                }
                Text {
                    text: tile.subtitle
                    font: Theme.caption
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                    width: Math.min(implicitWidth, tile.availableWidth - 24)
                }
            }
        }
    }
}
