// SPDX-License-Identifier: GPL-3.0-or-later
// The time zone field: looks like a drop-down list, opens the list with
// a search box (TimeZonePicker) instead of a plain list of 400 zones.
import QtQuick
import QtQuick.Templates as T
import org.raye.settings

T.AbstractButton {
    id: control

    implicitWidth: Math.min(360, Math.max(Theme.controlMinWidth * 2, label.implicitWidth + 11 + 12 + Theme.s3 + 11))
    implicitHeight: Theme.controlHeight
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.ComboBox

    contentItem: Text {
        id: label
        leftPadding: 11
        rightPadding: 11 + 12 + Theme.s3
        text: control.text
        font: Theme.body
        color: !control.enabled ? Theme.textDisabled : control.down ? Theme.textSecondary : Theme.text
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: !control.enabled ? Theme.controlDisabled
                 : control.down ? Theme.controlPressed
                 : control.hovered ? Theme.controlHover : Theme.control
            border.width: 1
            border.color: Theme.strokeControl
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
        Glyph {
            name: "chevron-down"
            size: 12
            color: control.enabled ? Theme.textSecondary : Theme.textDisabled
            x: parent.width - width - 11
            y: Math.round((parent.height - height) / 2) + (control.down ? 1 : 0)
        }
        FocusRing { shown: control.visualFocus }
    }
}
