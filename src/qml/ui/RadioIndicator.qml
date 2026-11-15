// SPDX-License-Identifier: GPL-3.0-or-later
// The round radio mark: a ring at rest, the accent with a dot in the
// middle when chosen; the dot grows on hover and shrinks while held.
import QtQuick

Item {
    id: mark

    property bool checked: false
    property bool hovered: false
    property bool pressed: false

    implicitWidth: 20
    implicitHeight: 20
    Accessible.ignored: true

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: !mark.enabled ? (mark.checked ? Theme.accentDisabled : Theme.controlDisabled)
             : mark.checked ? (mark.pressed ? Theme.accentPressed : mark.hovered ? Theme.accentHover : Theme.accent)
             : mark.pressed ? Theme.controlPressed : mark.hovered ? Theme.controlHover : Theme.controlPressed
        border.width: mark.checked ? 0 : 1
        border.color: mark.enabled ? Theme.strokeStrong : Theme.textDisabled
        Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
    }
    Rectangle {
        anchors.centerIn: parent
        property real size: !mark.checked ? (mark.pressed ? 10 : 0)
                                   : mark.pressed ? 10 : mark.hovered ? 14 : 12
        width: size
        height: size
        radius: size / 2
        // Windows' inverted dot: the surface colour inside the accent ring
        color: mark.checked ? (Theme.night ? "#1b1a1f" : "#ffffff") : Theme.textSecondary
        Behavior on size { NumberAnimation { duration: Theme.durationFaster } }
    }
}
