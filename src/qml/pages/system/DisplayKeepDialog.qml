// SPDX-License-Identifier: GPL-3.0-or-later
// "Keep these display settings?" with the seconds left before the
// screen goes back by itself. Esc and "Revert" go back at once.
import QtQuick
import QtQuick.Templates as T
import org.raye.settings

ContentDialog {
    id: dialog

    // a DisplayConfig
    property var config

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    closePolicy: T.Popup.CloseOnEscape
    title: i18nc("@title:window", "Keep these display settings?")
    text: config ? i18ncp("@info", "Going back to the previous display settings in %1 second.",
                          "Going back to the previous display settings in %1 seconds.", config.secondsLeft) : ""
    primaryText: i18nc("@action:button", "Keep changes")
    closeText: i18nc("@action:button go back to the previous display settings", "Revert")

    Connections {
        target: dialog.config
        function onConfirmingChanged() {
            if (dialog.config.confirming && !dialog.visible)
                dialog.open()
            else if (!dialog.config.confirming && dialog.visible)
                dialog.close()
        }
    }

    onAccepted: config.keepChanges()
    onRejected: if (config.confirming) config.revertChanges()
}
