// SPDX-License-Identifier: GPL-3.0-or-later
// One pointer style: a tile with the theme's arrow (its scalable
// drawing when it has one), the chosen one ringed in the accent colour.
import QtQuick
import QtQuick.Templates as T
import org.raye.settings

T.AbstractButton {
    id: tile

    property string name
    property url preview
    property bool chosen: false

    implicitWidth: 64
    implicitHeight: 64
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.RadioButton
    Accessible.name: name
    Accessible.checkable: true
    Accessible.checked: chosen

    ToolTip {
        visible: tile.hovered
        text: tile.name
        delay: 600
    }

    background: Item {
        Glow {
            visible: tile.chosen
            radius: Theme.radiusOverlay
            spread: 8
            color: Theme.glow
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusOverlay
            color: tile.down ? Theme.controlPressed : tile.hovered ? Theme.controlHover : Theme.control
            border.width: tile.chosen ? 2 : 1
            border.color: tile.chosen ? Theme.accent : Theme.strokeControl
            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
        }
        FocusRing { shown: tile.visualFocus }
    }

    contentItem: Item {
        Image {
            id: arrow
            anchors.centerIn: parent
            width: 32
            height: 32
            sourceSize: Qt.size(64, 64)
            fillMode: Image.PreserveAspectFit
            source: tile.preview
            visible: status === Image.Ready
            Accessible.ignored: true
        }
        // no drawing in the theme: its first letter
        Text {
            anchors.centerIn: parent
            visible: !arrow.visible
            text: tile.name.length > 0 ? tile.name.charAt(0).toUpperCase() : "?"
            font: Theme.subtitle
            color: Theme.text
            Accessible.ignored: true
        }
    }
}
