// SPDX-License-Identifier: GPL-3.0-or-later
// The small vertical accent bar that marks the chosen item of a list
// or of the navigation pane, with its gradient and glow.
import QtQuick

Item {
    id: pill

    implicitWidth: Theme.pillWidth
    implicitHeight: Theme.pillHeight
    Accessible.ignored: true

    Glow {
        radius: width / 2
        spread: 6
        visible: Theme.night
    }
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        gradient: Gradient {
            GradientStop { position: 0; color: Theme.accent }
            GradientStop { position: 1; color: Theme.accentGlowEnd }
        }
    }
}
