// SPDX-License-Identifier: GPL-3.0-or-later
// A tooltip on the flyout surface. The attached T.ToolTip would take the
// look of whatever Qt Quick style the session runs, so controls put one
// of these inside them instead:
//
//   ToolTip { visible: button.hovered; text: button.label }
import QtQuick
import QtQuick.Templates as T

T.ToolTip {
    id: tip

    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: -height - 4
    // long names wrap instead of running off the screen
    implicitWidth: Math.min(320, Math.max(implicitBackgroundWidth + leftInset + rightInset,
                                          implicitContentWidth + leftPadding + rightPadding))
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding)
    margins: 8
    leftPadding: 8
    rightPadding: 8
    topPadding: 5
    bottomPadding: 6
    delay: 500
    timeout: 5000
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent | T.Popup.CloseOnReleaseOutsideParent

    contentItem: Text {
        text: tip.text
        font: Theme.caption
        color: Theme.text
        wrapMode: Text.Wrap
        textFormat: Text.PlainText
    }

    background: Item {
        implicitWidth: 24
        implicitHeight: 24
        Glow {
            radius: 4
            spread: 8
            offsetY: 2
            color: Qt.alpha(Theme.shadow, Theme.night ? 0.4 : 0.14)
        }
        Rectangle {
            anchors.fill: parent
            radius: 4
            color: Theme.flyout
            border.width: 1
            border.color: Theme.strokeSurface
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easingFast }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFaster }
    }
}
