// SPDX-License-Identifier: GPL-3.0-or-later
// A setting that is on or off: "On"/"Off" and the switch on the right,
// and a click anywhere on the card flips it (Space too). Changes apply
// at once: handle toggled() (the user flipped it) and read `checked`.
import QtQuick

Card {
    id: card

    // what the label says in each state
    property string onText: i18nc("@label a setting that is on", "On")
    property string offText: i18nc("@label a setting that is off", "Off")

    clickable: true
    checkable: true
    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: checked

    Text {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        text: card.checked ? card.onText : card.offText
        font: Theme.body
        color: card.enabled ? Theme.text : Theme.textDisabled
        Accessible.ignored: true
    }
    ToggleSwitch {
        id: toggle
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        focusPolicy: Qt.NoFocus
        enabled: card.enabled
        externalHover: card.showHover
        externalPress: card.showPressed
        previewState: card.previewState === "focus" ? "" : card.previewState
        text: card.title
        Accessible.ignored: true
        // a click right on the switch: the card follows and says so
        onToggled: {
            card.checked = checked
            card.toggled()
        }
        Binding on checked { value: card.checked }
    }
}
