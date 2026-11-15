// SPDX-License-Identifier: GPL-3.0-or-later
// Privacy & security › Activity history: whether the files you open are
// remembered (Start's recommendations, Recent in Files and in open
// dialogs), and clearing what is.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    property bool cleared: false

    crumbs: [{ text: i18nc("@title settings category", "Privacy & security"), target: "privacy" },
             { text: i18nc("@title settings page", "Activity history") }]

    ActivityHistory {
        id: history
        onCleared: page.cleared = true
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.cleared
        severity: "success"
        title: i18nc("@info", "Activity history cleared")
        message: i18nc("@info", "The list of files you opened is empty.")
        onClosed: page.cleared = false
    }

    ToggleCard {
        Layout.fillWidth: true
        searchId: "remember-files"
        iconName: "org.raye.settings-activity-history"
        iconFallback: "document-open-recent"
        title: i18nc("@label", "Remember the files I open")
        description: i18nc("@info", "Shows them in Start, in Recent in Files, and when you open or save a file. They stay on this PC.")
        checked: history.remember
        onToggled: {
            history.remember = checked
            checked = Qt.binding(() => history.remember)
        }
    }

    ButtonCard {
        Layout.fillWidth: true
        searchId: "clear-history"
        iconName: "edit-clear-history"
        title: i18nc("@label", "Clear activity history")
        description: i18nc("@info", "Forget the files and apps you used recently. Your files are not deleted.")
        buttonText: i18nc("@action:button", "Clear history")
        onButtonClicked: history.clear()
    }
}
