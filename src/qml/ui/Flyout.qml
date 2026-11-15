// SPDX-License-Identifier: GPL-3.0-or-later
// The raised surface every menu, list and flyout opens on: the flyout
// colour, a 1 px border, a soft shadow, 8 px corners.
//
// T.Popup has no size of its own: without the two implicit sizes below
// a popup that only sets a width is 0 px high, and its items float over
// the page with nothing behind them.
import QtQuick
import QtQuick.Templates as T

T.Popup {
    id: flyout

    // how dark the shadow is, night and day
    property real shadowStrength: Theme.night ? 0.45 : 0.18

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    padding: 4
    margins: 8

    background: Item {
        Glow {
            radius: Theme.radiusOverlay
            spread: 16
            offsetY: 6
            color: Qt.alpha(Theme.shadow, flyout.shadowStrength)
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusOverlay
            color: Theme.flyout
            border.width: 1
            border.color: Theme.strokeSurface
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easingFast }
        NumberAnimation { property: "scale"; from: 0.96; to: 1; duration: Theme.durationNormal; easing.type: Theme.easingEnter }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFaster }
    }
}
