// SPDX-License-Identifier: GPL-3.0-or-later
// A one-line text field, 32 px high. Focused, it darkens and shows a
// 2 px accent line along its bottom edge; an "x" clears what was typed.
import QtQuick
import QtQuick.Templates as T

T.TextField {
    id: control

    // the "x" that clears the field while it has focus and text
    property bool clearButtonEnabled: true
    // the eye that shows a password while it is down (PasswordField)
    property bool revealButtonEnabled: false
    property bool revealed: false
    // for the gallery: "hover" or "focus" shown at rest
    property string previewState: ""

    readonly property bool showHover: enabled && (hovered || previewState === "hover")
    readonly property bool showFocus: activeFocus || previewState === "focus"
    readonly property bool _clearShown: clearButtonEnabled && !readOnly && enabled && length > 0 && activeFocus
    // right to left: the buttons go to the left end
    readonly property bool mirrored: LayoutMirroring.enabled || Qt.application.layoutDirection === Qt.RightToLeft
    readonly property real _buttonsWidth: (_clearShown ? 28 : 0) + (revealButtonEnabled ? 28 : 0)

    implicitWidth: Math.max(Theme.controlMinWidth * 2, placeholder.implicitWidth + leftPadding + rightPadding)
    implicitHeight: Theme.controlHeight
    leftPadding: mirrored ? 11 + _buttonsWidth : 11
    rightPadding: mirrored ? 11 : 11 + _buttonsWidth
    topPadding: 0
    bottomPadding: 0
    // explicit, so a right-to-left layout mirrors it
    horizontalAlignment: TextInput.AlignLeft
    verticalAlignment: TextInput.AlignVCenter
    hoverEnabled: true

    font: Theme.body
    color: enabled ? Theme.text : Theme.textDisabled
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    placeholderTextColor: enabled ? Theme.textTertiary : Theme.textDisabled
    echoMode: revealButtonEnabled && !revealed ? TextInput.Password : TextInput.Normal

    Accessible.role: Accessible.EditableText
    Accessible.name: placeholderText
    Accessible.passwordEdit: revealButtonEnabled && !revealed

    Text {
        id: placeholder
        x: control.leftPadding
        width: control.width - control.leftPadding - control.rightPadding
        height: control.height
        text: control.placeholderText
        font: control.font
        color: control.placeholderTextColor
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        visible: control.length === 0 && control.preeditText === ""
        Accessible.ignored: true
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        x: control.mirrored ? 4 : control.width - width - 4
        layoutDirection: control.mirrored ? Qt.RightToLeft : Qt.LeftToRight

        FieldButton {
            visible: control._clearShown
            glyph: "close"
            Accessible.name: i18n("Clear")
            onClicked: {
                control.clear()
                control.forceActiveFocus()
            }
        }
        FieldButton {
            visible: control.revealButtonEnabled
            glyph: control.revealed ? "eye-off" : "eye"
            Accessible.name: control.revealed ? i18n("Hide password") : i18n("Show password")
            onClicked: control.revealed = !control.revealed
        }
    }

    background: Item {
        implicitWidth: Theme.controlMinWidth * 2
        implicitHeight: Theme.controlHeight
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: !control.enabled ? Theme.controlDisabled
                 : control.showFocus ? Theme.inputActive
                 : control.showHover ? Theme.controlHover : Theme.control
            border.width: 1
            border.color: Theme.strokeControl
        }
        // the bottom edge: a quiet line at rest, the accent when focused
        Item {
            width: parent.width
            height: control.showFocus ? 2 : 1
            y: parent.height - height
            clip: true
            Rectangle {
                width: parent.width
                height: Theme.radiusControl * 2
                y: parent.height - height
                radius: Theme.radiusControl
                color: control.showFocus ? Theme.accent : "transparent"
                border.width: control.showFocus ? 0 : 1
                border.color: control.enabled ? Theme.strokeStrong : Theme.strokeControl
                opacity: control.showFocus ? 1 : 0.6
            }
        }
    }

    component FieldButton: T.AbstractButton {
        id: button
        property string glyph
        width: 28
        height: 24
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        Accessible.role: Accessible.Button
        background: Rectangle {
            radius: 4
            color: button.down ? Theme.subtlePressed : button.hovered ? Theme.subtleHover : "transparent"
        }
        contentItem: Item {
            Glyph {
                anchors.centerIn: parent
                name: button.glyph
                size: 12
                color: Theme.textSecondary
            }
        }
    }
}
