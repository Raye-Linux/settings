// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Text input: the on-screen keyboard, and where the rest
// of typing is set.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
             { text: i18nc("@title settings page", "Text input") }]

    TextInputSettings { id: input }

    ToggleCard {
        objectName: "onScreenKeyboard"
        Layout.fillWidth: true
        searchId: "on-screen-keyboard"
        iconName: "org.raye.settings-text-input"
        iconFallback: "input-keyboard-virtual"
        title: i18nc("@title", "On-screen keyboard")
        description: input.available
                     ? i18nc("@info", "Comes up on a touch screen when you tap a place to type")
                     : i18nc("@info", "Not installed on this PC")
        enabled: input.available
        checked: input.onScreenKeyboard
        onCheckedChanged: if (checked !== input.onScreenKeyboard) input.onScreenKeyboard = checked
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    NavCard {
        Layout.fillWidth: true
        searchId: "typing"
        iconName: "org.raye.settings-typing"
        iconFallback: "input-keyboard"
        title: i18nc("@title settings page", "Typing")
        description: i18nc("@info", "Keyboard layouts and typing options")
        target: "time/typing"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-accessibility-keyboard"
        iconFallback: "input-keyboard"
        title: i18nc("@title settings page", "Keyboard accessibility")
        description: i18nc("@info", "Sticky keys, filter keys")
        target: "accessibility/keyboard"
    }
}
