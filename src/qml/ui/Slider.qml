// SPDX-License-Identifier: GPL-3.0-or-later
// A slider: a 4 px rail, the accent part up to the thumb, and a 20 px
// thumb with an accent centre that grows on hover and shrinks while
// held. The value shows above the thumb while it is dragged.
import QtQuick
import QtQuick.Templates as T

T.Slider {
    id: control

    // how the value reads in the bubble ("40 %", "1.5x")
    property var textFromValue: (value) => Math.round(value).toString()
    // for the gallery: "hover", "pressed" or "focus" shown at rest
    property string previewState: ""

    readonly property bool showHover: enabled && (hovered || previewState === "hover")
    readonly property bool showPressed: enabled && (pressed || previewState === "pressed")

    implicitWidth: horizontal ? Theme.sliderWidth : Theme.sliderThumb
    implicitHeight: horizontal ? Theme.controlHeight : Theme.sliderWidth
    padding: Theme.sliderThumb / 2
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Slider
    Accessible.description: textFromValue(value)

    background: Item {
        x: control.leftPadding + (control.horizontal ? 0 : (control.availableWidth - Theme.sliderTrack) / 2)
        y: control.topPadding + (control.horizontal ? (control.availableHeight - Theme.sliderTrack) / 2 : 0)
        width: control.horizontal ? control.availableWidth : Theme.sliderTrack
        height: control.horizontal ? Theme.sliderTrack : control.availableHeight

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusSmall
            color: control.enabled ? Qt.alpha(Theme.strokeStrong, 0.6) : Theme.controlDisabled
        }
        // the filled part, from the start of the rail to the thumb
        Rectangle {
            readonly property real reach: control.visualPosition
            x: control.horizontal && control.mirrored ? parent.width * reach : 0
            y: control.horizontal ? 0 : parent.height * reach
            width: control.horizontal ? parent.width * (control.mirrored ? 1 - reach : reach) : parent.width
            height: control.horizontal ? parent.height : parent.height * (1 - reach)
            radius: Theme.radiusSmall
            color: control.enabled ? Theme.accent : Theme.accentDisabled
        }
    }

    handle: Item {
        x: control.leftPadding + (control.horizontal ? control.visualPosition * control.availableWidth : control.availableWidth / 2) - width / 2
        y: control.topPadding + (control.horizontal ? control.availableHeight / 2 : control.visualPosition * control.availableHeight) - height / 2
        width: Theme.sliderThumb
        height: Theme.sliderThumb

        Glow {
            visible: Theme.night && control.showHover
            radius: width / 2
            spread: 6
        }
        Rectangle {
            anchors.fill: parent
            radius: width / 2
            color: control.enabled ? Theme.layerSolid : Theme.controlDisabled
            border.width: 1
            border.color: Theme.strokeControl
        }
        Rectangle {
            anchors.centerIn: parent
            property real size: control.showPressed ? 10 : control.showHover ? 14 : 12
            width: size
            height: size
            radius: size / 2
            color: control.enabled ? (control.showPressed ? Theme.accentPressed : Theme.accent) : Theme.textDisabled
            Behavior on size { NumberAnimation { duration: Theme.durationFaster } }
        }
        FocusRing {
            shown: control.visualFocus || control.previewState === "focus"
            radius: Theme.sliderThumb / 2
        }

        // the value, above the thumb while it moves
        Rectangle {
            visible: control.pressed
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.top
            anchors.bottomMargin: 8
            width: bubbleText.implicitWidth + 16
            height: 28
            radius: 4
            color: Theme.flyout
            border.width: 1
            border.color: Theme.strokeSurface
            Text {
                id: bubbleText
                anchors.centerIn: parent
                text: control.textFromValue(control.value)
                font: Theme.caption
                color: Theme.text
            }
        }
    }
}
