// SPDX-License-Identifier: GPL-3.0-or-later
// A drop-down of the Power page: `choices` is a list of { value, text }
// and `value` the one in use; chosen() gives the value picked.
import QtQuick
import org.raye.settings

ComboBox {
    id: box

    property var choices: []
    property int value: 0
    // what a screen reader says before the choice
    property string label

    signal chosen(int value)

    model: choices
    textRole: "text"
    valueRole: "value"
    currentIndex: Math.max(0, choices.findIndex(c => c.value === box.value))
    Accessible.name: label + ", " + displayText
    onActivated: (index) => box.chosen(choices[index].value)
}
