// SPDX-License-Identifier: GPL-3.0-or-later
// A push button, 32 px high. `accent: true` makes it the one button of
// a place that says "do it" (Restart now, Save): the accent colour with
// its lit top edge and a soft glow on hover.
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

T.Button {
    id: control

    property bool accent: false
    // for the gallery: "hover", "pressed" or "focus" shown at rest
    property string previewState: ""

    readonly property bool showHover: enabled && (hovered || previewState === "hover")
    readonly property bool showPressed: enabled && (down || previewState === "pressed")

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding, 80)
    implicitHeight: Theme.controlHeight
    leftPadding: Theme.s4
    rightPadding: Theme.s4
    topPadding: 0
    bottomPadding: 0
    spacing: Theme.s3
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    icon.width: 16
    icon.height: 16

    Accessible.role: Accessible.Button
    Accessible.name: text

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()

    // the label stays centred when a layout makes the button wider
    contentItem: Item {
        implicitWidth: labelRow.implicitWidth
        implicitHeight: labelRow.implicitHeight
        Row {
            id: labelRow
            anchors.centerIn: parent
            spacing: control.spacing
            Kirigami.Icon {
                visible: control.icon.name !== "" || control.icon.source.toString() !== ""
                anchors.verticalCenter: parent.verticalCenter
                width: control.icon.width
                height: control.icon.height
                source: control.icon.source.toString() !== "" ? control.icon.source : control.icon.name
                color: label.color
            }
            Text {
                id: label
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: Theme.body
                color: !control.enabled ? Theme.textDisabled
                     : control.accent ? Theme.textOnAccent
                     : control.showPressed ? Theme.textSecondary : Theme.text
                Accessible.ignored: true
            }
        }
    }

    background: Item {
        implicitWidth: 80
        implicitHeight: Theme.controlHeight

        Glow {
            visible: control.accent && control.showHover
            radius: Theme.radiusControl
            spread: 10
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: control.accent
                   ? (!control.enabled ? Theme.accentDisabled
                      : control.showPressed ? Theme.accentPressed
                      : control.showHover ? Theme.accentHover : Theme.accent)
                   : (!control.enabled ? Theme.controlDisabled
                      : control.showPressed ? Theme.controlPressed
                      : control.showHover ? Theme.controlHover : Theme.control)
            border.width: control.accent ? 0 : 1
            border.color: Theme.strokeControl
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
        // the elevation edge: a darker bottom line on a plain button,
        // a lit top line on the accent one
        Item {
            width: parent.width
            height: 1
            y: control.accent ? 0 : parent.height - 1
            clip: true
            visible: control.enabled && !control.showPressed
            Rectangle {
                width: parent.width
                height: Theme.radiusControl * 2
                y: control.accent ? 0 : 1 - height
                radius: Theme.radiusControl
                color: "transparent"
                border.width: 1
                border.color: control.accent ? "#1fffffff" : Theme.strokeControlBottom
            }
        }
        FocusRing {
            shown: control.visualFocus || control.previewState === "focus"
        }
    }
}
