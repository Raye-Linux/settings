// SPDX-License-Identifier: GPL-3.0-or-later
// The user name and password a browser types to sign in to Device
// Portal. They belong to the portal only, not to an account of the PC.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    property DeveloperSettings settings
    property string problem: ""

    title: i18nc("@title:window", "Device Portal sign-in")
    text: i18nc("@info", "Choose a user name and a password for the portal. Use one you don't use anywhere else.")
    primaryText: i18nc("@action:button", "Save")
    primaryEnabled: user.text.trim().length > 0 && password.text.length >= 8 && password.text === confirm.text

    onOpened: {
        problem = ""
        user.text = settings ? settings.portalUser : ""
        user.forceActiveFocus()
    }
    onAccepted: {
        const typed = password.text
        password.clear()
        confirm.clear()
        settings.setPortalLogin(user.text, typed)
    }
    onRejected: {
        password.clear()
        confirm.clear()
    }

    Connections {
        target: dialog.settings
        function onLoginSaved(ok, reason) {
            if (!ok) {
                dialog.problem = reason === "short" ? i18nc("@info", "Use at least 8 characters.")
                                                    : i18nc("@info", "That didn't work. Try another user name.")
                dialog.open()
            }
        }
    }

    TextField {
        id: user
        Layout.fillWidth: true
        placeholderText: i18nc("@label", "User name")
        maximumLength: 64
    }
    PasswordField {
        id: password
        Layout.fillWidth: true
        placeholderText: i18nc("@label", "Password (8 characters or more)")
    }
    PasswordField {
        id: confirm
        Layout.fillWidth: true
        placeholderText: i18nc("@label", "Confirm the password")
        onAccepted: if (dialog.primaryEnabled) dialog._acceptByKey()
    }
    Text {
        Layout.fillWidth: true
        visible: dialog.problem !== "" || (confirm.text.length > 0 && confirm.text !== password.text)
        text: dialog.problem !== "" ? dialog.problem : i18nc("@info", "The passwords don't match.")
        font: Theme.body
        color: Theme.statusColor("critical")
        wrapMode: Text.Wrap
    }
}
