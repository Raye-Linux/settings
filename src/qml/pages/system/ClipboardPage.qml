// SPDX-License-Identifier: GPL-3.0-or-later
// System > Clipboard: the history Win+V opens, and clearing it.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Clipboard") }]

    // "" at rest, then "done" or "failed" for a moment after Clear
    property string clearResult: ""

    ClipboardSettings {
        id: clipboard
        onCleared: (ok) => {
            page.clearResult = ok ? "done" : "failed"
            resultTimer.restart()
        }
    }

    Timer {
        id: resultTimer
        interval: 4000
        onTriggered: page.clearResult = ""
    }

    ToggleCard {
        iconName: "org.raye.settings-clipboard"
        iconFallback: "edit-paste"
        title: i18n("Clipboard history")
        description: i18n("Save several items to the clipboard to use later. Press the Windows key + V to see your history and paste from it.")
        searchId: "clipboard-history"
        checked: clipboard.history
        onToggled: clipboard.history = checked
    }

    Card {
        iconName: "org.raye.settings-clipboard"
        iconFallback: "edit-clear-history"
        title: i18n("Clear clipboard data")
        description: page.clearResult === "failed"
                     ? i18n("The clipboard could not be cleared. Try again in a moment.")
                     : i18n("Clear everything you copied on this PC")
        searchId: "clear-clipboard"

        Row {
            spacing: Theme.s3
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined

            StatusGlyph {
                anchors.verticalCenter: parent.verticalCenter
                status: "success"
                label: i18n("Cleared")
                visible: page.clearResult === "done"
            }
            Button {
                text: i18nc("@action:button clear the clipboard", "Clear")
                enabled: !clipboard.clearing
                Accessible.description: i18n("Clear clipboard data")
                onClicked: clipboard.clear()
            }
        }
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-phone"
        iconFallback: "smartphone"
        title: i18n("Your phone")
        description: i18n("Share what you copy with your phone")
        target: "devices/phone"
    }
}
