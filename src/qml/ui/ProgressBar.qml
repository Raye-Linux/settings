// SPDX-License-Identifier: GPL-3.0-or-later
// A 3 px progress bar on a thin rail. Indeterminate: a glowing segment
// slides across every two seconds. `status` "caution" (paused) or
// "critical" (failed) colours it like Windows does.
import QtQuick
import QtQuick.Templates as T

T.ProgressBar {
    id: control

    property string status: ""

    readonly property color barColor: !enabled ? Theme.textDisabled
                                    : status === "" ? Theme.accent : Theme.statusColor(status)

    implicitWidth: 200
    implicitHeight: Theme.progressHeight
    Accessible.role: Accessible.ProgressBar
    Accessible.description: indeterminate ? "" : Math.round(position * 100) + " %"

    background: Item {
        implicitWidth: 200
        implicitHeight: Theme.progressHeight
        Rectangle {
            width: parent.width
            height: 1
            anchors.verticalCenter: parent.verticalCenter
            radius: 0.5
            color: Theme.strokeStrong
        }
    }

    contentItem: Item {
        clip: true

        Rectangle {
            visible: !control.indeterminate
            width: control.visualPosition * parent.width
            x: control.mirrored ? parent.width - width : 0
            height: parent.height
            radius: height / 2
            color: control.barColor
            Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }
        }

        Rectangle {
            id: segment
            visible: control.indeterminate
            width: parent.width * 0.4
            height: parent.height
            radius: height / 2
            x: Theme.animationsOff ? parent.width * 0.3 : -width
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: Qt.alpha(control.barColor, 0) }
                GradientStop { position: 0.35; color: control.barColor }
                GradientStop { position: 0.75; color: control.status === "" ? Theme.accentGlowEnd : control.barColor }
                GradientStop { position: 1; color: Qt.alpha(control.barColor, 0) }
            }
            NumberAnimation on x {
                running: control.indeterminate && control.visible && !Theme.animationsOff
                from: -segment.width
                to: segment.parent.width
                duration: 2000
                loops: Animation.Infinite
                easing.type: Easing.InOutQuad
            }
        }
    }
}
