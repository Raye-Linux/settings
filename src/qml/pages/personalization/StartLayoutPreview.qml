// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Start: a small drawing of the menu for each layout,
// pins on top and recommended items under them.
import QtQuick
import org.raye.settings

Rectangle {
    id: preview

    // rows of pins (6 to a row), rows of recommended items (2 to a row)
    property int pinRows: 3
    property int recommendedRows: 2

    implicitHeight: 96
    implicitWidth: 120
    anchors.horizontalCenter: parent ? parent.horizontalCenter : undefined
    width: 120
    height: 96
    radius: Theme.radiusControl
    color: Theme.control
    border.width: 1
    border.color: Theme.cardBorder
    Accessible.ignored: true

    Column {
        anchors { fill: parent; margins: 8 }
        spacing: 4
        Repeater {
            model: preview.pinRows
            delegate: Row {
                spacing: 4
                Repeater {
                    model: 6
                    delegate: Rectangle { width: 13; height: 13; radius: 3; color: Theme.accent; opacity: 0.85 }
                }
            }
        }
        Item { width: 1; height: 2 }
        Repeater {
            model: preview.recommendedRows
            delegate: Row {
                spacing: 4
                Repeater {
                    model: 2
                    delegate: Rectangle { width: 50; height: 9; radius: 2; color: Theme.textSecondary; opacity: 0.45 }
                }
            }
        }
    }
}
