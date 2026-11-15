// SPDX-License-Identifier: GPL-3.0-or-later
// The speaker (or microphone) button beside a volume slider: one click
// mutes, the next one brings the sound back. The glyph says which.
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

T.AbstractButton {
    id: button

    property bool muted: false
    property bool input: false
    property int volume: 100

    implicitWidth: 32
    implicitHeight: 32
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: muted ? (input ? i18nc("@action:button", "Unmute the microphone") : i18nc("@action:button", "Unmute"))
                           : (input ? i18nc("@action:button", "Mute the microphone") : i18nc("@action:button", "Mute"))
    ToolTip {
        visible: button.hovered
        text: button.Accessible.name
    }
    Keys.onReturnPressed: clicked()

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: button.down ? Theme.subtlePressed : button.hovered ? Theme.subtleHover : "transparent"
        }
        FocusRing { shown: button.visualFocus }
    }
    contentItem: Item {
        Kirigami.Icon {
            anchors.centerIn: parent
            width: 20
            height: 20
            source: button.input ? (button.muted ? "microphone-sensitivity-muted" : "audio-input-microphone")
                  : button.muted || button.volume === 0 ? "audio-volume-muted"
                  : button.volume < 34 ? "audio-volume-low"
                  : button.volume < 67 ? "audio-volume-medium" : "audio-volume-high"
            color: Theme.text
            isMask: true
        }
    }
}
