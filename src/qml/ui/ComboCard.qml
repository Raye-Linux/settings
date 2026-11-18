// SPDX-License-Identifier: GPL-3.0-or-later
// A setting picked from a list: the card with a drop-down on the right.
import QtQuick

Card {
    id: card

    property alias model: combo.model
    property alias currentIndex: combo.currentIndex
    property alias textRole: combo.textRole
    property alias valueRole: combo.valueRole
    readonly property alias currentValue: combo.currentValue
    readonly property alias currentText: combo.currentText
    readonly property alias comboBox: combo

    // the user picked an item (not sent when the page sets currentIndex)
    signal activated(int index)

    ComboBox {
        id: combo
        enabled: card.enabled
        Accessible.name: card.title + ", " + displayText
        onActivated: (index) => card.activated(index)
    }

    // Reading count and valueRole here ties a binding that calls this to
    // the model: set before the model, it would stay at -1 for good.
    function indexOfValue(value) {
        void combo.count
        void combo.valueRole
        return combo.indexOfValue(value)
    }
}
