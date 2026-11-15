// SPDX-License-Identifier: GPL-3.0-or-later
// The on/off switch: a 40 x 20 track, a knob that grows on hover and
// slides across in 167 ms, the accent gradient when on. Space toggles.
import QtQuick
import QtQuick.Templates as T

T.AbstractButton {
    id: control

    // a card that holds the switch shows its hover and press here
    property bool externalHover: false
    property bool externalPress: false
    // for the gallery: "hover", "pressed" or "focus" shown at rest
    property string previewState: ""

    readonly property bool showHover: enabled && (hovered || externalHover || previewState === "hover")
    readonly property bool showPressed: enabled && (down || externalPress || previewState === "pressed")

    checkable: true
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: Theme.toggleWidth
    implicitHeight: Theme.toggleHeight
    padding: 0

    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.checkable: true
    Accessible.checked: checked

    indicator: null
    contentItem: null

    background: Item {
        implicitWidth: Theme.toggleWidth
        implicitHeight: Theme.toggleHeight

        Glow {
            visible: control.checked && control.enabled && control.showHover
            radius: height / 2
            spread: 6
        }
        Rectangle {
            id: track
            anchors.fill: parent
            radius: height / 2
            border.width: control.checked ? 0 : 1
            border.color: control.enabled ? Theme.strokeStrong : Theme.textDisabled
            color: control.checked ? "transparent"
                 : control.showPressed ? Theme.controlPressed
                 : control.showHover ? Theme.controlHover : Theme.controlPressed
            gradient: control.checked ? onGradient : null
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }

            Gradient {
                id: onGradient
                orientation: Gradient.Horizontal
                GradientStop { position: 0; color: !control.enabled ? Theme.accentDisabled
                                                   : control.showPressed ? Theme.accentPressed
                                                   : control.showHover ? Theme.accentHover : Theme.accent }
                GradientStop { position: 1; color: !control.enabled ? Theme.accentDisabled
                                                   : Theme.mix(control.showHover ? Theme.accentHover : Theme.accent,
                                                               Theme.accentGlowEnd, 0.55) }
            }
        }

        Rectangle {
            id: knob
            readonly property real size: control.showHover && !control.showPressed ? Theme.toggleKnobHover : Theme.toggleKnob
            width: control.showPressed ? 17 : size
            height: control.showPressed ? 14 : size
            radius: height / 2
            anchors.verticalCenter: parent.verticalCenter
            // 4 px from the end at rest, 3 px when it grows
            x: {
                const margin = (Theme.toggleHeight - height) / 2
                const onRight = control.checked !== control.mirrored
                return onRight ? parent.width - width - margin : margin
            }
            color: control.checked ? (control.enabled ? Theme.textOnAccent : Theme.textDisabled)
                                   : (control.enabled ? Theme.textSecondary : Theme.textDisabled)
            Behavior on x { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingFast } }
            Behavior on width { NumberAnimation { duration: Theme.durationFaster } }
            Behavior on height { NumberAnimation { duration: Theme.durationFaster } }
        }

        FocusRing {
            shown: control.visualFocus || control.previewState === "focus"
            radius: Theme.toggleHeight / 2
        }
    }
}
