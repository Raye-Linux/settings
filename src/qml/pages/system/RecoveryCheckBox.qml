// SPDX-License-Identifier: GPL-3.0-or-later
// A check box for a dialog of the Recovery page ("Keep my wallpaper"):
// a 20 px square, the accent with a check mark when ticked.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

T.CheckBox {
    id: box

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    spacing: Theme.s3
    implicitWidth: implicitIndicatorWidth + spacing + implicitContentWidth
    implicitHeight: Math.max(implicitIndicatorHeight, implicitContentHeight, Theme.controlHeight)
    Accessible.role: Accessible.CheckBox
    Accessible.name: text
    Accessible.checked: checked

    indicator: Rectangle {
        x: box.mirrored ? box.width - width : 0
        anchors.verticalCenter: parent.verticalCenter
        implicitWidth: 20
        implicitHeight: 20
        radius: 4
        color: box.checked ? (box.down ? Theme.accentPressed : box.hovered ? Theme.accentHover : Theme.accent)
             : box.down ? Theme.controlPressed : box.hovered ? Theme.controlHover : Theme.control
        border.width: box.checked ? 0 : 1
        border.color: Theme.strokeStrong
        Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        Glyph {
            anchors.centerIn: parent
            name: "check"
            size: 14
            weight: 2
            color: Theme.textOnAccent
            visible: box.checked
        }
        FocusRing { shown: box.visualFocus; radius: 4 }
    }

    contentItem: Text {
        leftPadding: box.mirrored ? 0 : box.indicator.width + box.spacing
        rightPadding: box.mirrored ? box.indicator.width + box.spacing : 0
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignLeft
        text: box.text
        font: Theme.body
        color: box.enabled ? Theme.text : Theme.textDisabled
    }
}
