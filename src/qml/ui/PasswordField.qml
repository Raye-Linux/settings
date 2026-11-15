// SPDX-License-Identifier: GPL-3.0-or-later
// A password field: the text field with dots, and an eye that shows
// the password while it is on. What is typed never leaves the field:
// read it once with `text`, then call clear().
import QtQuick

TextField {
    clearButtonEnabled: false
    revealButtonEnabled: true
    // Qt keeps the name of a password field from screen readers: what
    // the field is for goes in the description
    Accessible.name: placeholderText !== "" ? placeholderText : i18nc("@label a password field", "Password")
    Accessible.description: Accessible.name
    inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhHiddenText
    // a password is not copied out of the field, even when shown
    Keys.onShortcutOverride: (event) => {
        if (event.matches(StandardKey.Copy) || event.matches(StandardKey.Cut))
            event.accepted = true
    }
    Keys.onPressed: (event) => {
        if (event.matches(StandardKey.Copy) || event.matches(StandardKey.Cut))
            event.accepted = true
    }
}
