// SPDX-License-Identifier: GPL-3.0-or-later
// A small picture of a desktop for Personalization > Desktop environment,
// drawn like Setup's "Choose your desktop": KDE Plasma's windows that
// overlap over a taskbar with Start in the middle, Hyprland's thin bar at
// the top and windows tiled side by side. 16:10, like most screens.
import QtQuick
import org.raye.settings

Rectangle {
    id: preview

    // "plasma" or "hyprland"
    property string desktop: "plasma"

    implicitWidth: 240
    implicitHeight: Math.round(width * 10 / 16)
    radius: Theme.radiusControl
    clip: true
    color: Theme.backdropSolid
    border.width: 1
    border.color: Theme.cardBorder
    Accessible.ignored: true

    component MiniWindow: Rectangle {
        property bool active: false
        radius: 3
        color: active ? Theme.cardSolid : Theme.layerSolid
        border.width: 1
        border.color: active ? Theme.accent : Theme.strokeSurface
        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: 1 }
            height: Math.max(4, parent.height * 0.12)
            radius: 2
            color: Theme.subtleHover
        }
    }

    Rectangle {
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0; color: Qt.alpha(Theme.accent, 0.20) }
            GradientStop { position: 1; color: "transparent" }
        }
    }

    // KDE Plasma
    Item {
        objectName: "plasmaPicture"
        anchors.fill: parent
        visible: preview.desktop !== "hyprland"
        MiniWindow {
            x: parent.width * 0.10; y: parent.height * 0.12
            width: parent.width * 0.52; height: parent.height * 0.52
        }
        MiniWindow {
            active: true
            x: parent.width * 0.36; y: parent.height * 0.26
            width: parent.width * 0.54; height: parent.height * 0.50
        }
        Rectangle {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: 1 }
            height: parent.height * 0.13
            color: Theme.layer
            Row {
                anchors.centerIn: parent
                spacing: parent.height * 0.3
                Repeater {
                    model: 4
                    Rectangle {
                        required property int index
                        width: parent.parent.height * 0.52
                        height: width
                        radius: 2
                        color: index === 0 ? Theme.accent : Theme.textTertiary
                    }
                }
            }
        }
    }

    // Hyprland
    Item {
        id: tiles
        objectName: "hyprlandPicture"
        anchors.fill: parent
        visible: preview.desktop === "hyprland"
        readonly property real gap: Math.max(3, width * 0.025)
        readonly property real below: height * 0.09 + gap
        Rectangle {
            x: tiles.gap; y: tiles.gap * 0.6
            width: parent.width - 2 * tiles.gap
            height: parent.height * 0.08
            radius: height / 2
            color: Theme.layer
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: parent.height * 0.4
                width: parent.height * 0.5; height: width; radius: width / 2
                color: Theme.accent
            }
        }
        MiniWindow {
            active: true
            x: tiles.gap; y: tiles.below
            width: (parent.width - 3 * tiles.gap) * 0.55
            height: parent.height - tiles.below - tiles.gap
        }
        MiniWindow {
            x: tiles.gap * 2 + (parent.width - 3 * tiles.gap) * 0.55; y: tiles.below
            width: (parent.width - 3 * tiles.gap) * 0.45
            height: (parent.height - tiles.below - 2 * tiles.gap) / 2
        }
        MiniWindow {
            x: tiles.gap * 2 + (parent.width - 3 * tiles.gap) * 0.55
            y: tiles.below + (parent.height - tiles.below - 2 * tiles.gap) / 2 + tiles.gap
            width: (parent.width - 3 * tiles.gap) * 0.45
            height: (parent.height - tiles.below - 2 * tiles.gap) / 2
        }
    }
}
