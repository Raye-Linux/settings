// SPDX-License-Identifier: GPL-3.0-or-later
// Recovery › Reset your settings, and its undo. Both happen at the next
// sign-in (Plasma writes its settings again when it quits), so both end
// with signing out.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

Item {
    id: root

    required property Recovery recovery

    // only its dialogs show, over the window; the item takes no room
    visible: false

    function openReset() {
        keepWallpaper.checked = true
        reset.open()
    }
    function openUndo() { undo.open() }

    ContentDialog {
        id: reset
        objectName: "resetSettings"
        title: i18nc("@title:window", "Reset your settings?")
        text: i18nc("@info", "The desktop, the taskbar and the settings of your apps go back to how they were when Raye was installed. Your files and apps stay. You'll be signed out, so save your work first.")
        primaryText: i18nc("@action:button", "Reset and sign out")
        onAccepted: root.recovery.resetSettings(keepWallpaper.checked)

        RecoveryCheckBox {
            id: keepWallpaper
            text: i18nc("@option:check", "Keep my wallpaper")
            checked: true
        }
        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 440
            font: Theme.caption
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            text: i18nc("@info", "You can undo this for 30 days.")
        }
    }

    ContentDialog {
        id: undo
        objectName: "undoReset"
        title: i18nc("@title:window", "Undo the last reset?")
        text: i18nc("@info %1 is a date", "The settings you had before %1 come back. You'll be signed out, so save your work first.",
                    root.recovery.undoDate)
        primaryText: i18nc("@action:button", "Undo and sign out")
        onAccepted: root.recovery.undoReset()
    }
}
