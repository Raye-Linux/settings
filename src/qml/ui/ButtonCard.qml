// SPDX-License-Identifier: GPL-3.0-or-later
// A card with one button on the right ("Rename this PC", "Restart now").
import QtQuick

Card {
    id: card

    property alias buttonText: button.text
    // the one action of its page that says "do it"
    property alias accent: button.accent
    readonly property alias button: button

    signal buttonClicked()

    Button {
        id: button
        enabled: card.enabled
        Accessible.description: card.title
        onClicked: card.buttonClicked()
    }
}
