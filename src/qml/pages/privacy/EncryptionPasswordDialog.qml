// SPDX-License-Identifier: GPL-3.0-or-later
// Turning disk encryption on (a new disk password, typed twice) or off
// (the disk password). The dialog closes before the work starts, so the
// sudo prompt never opens over it.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    // true: turning encryption on
    property bool turningOn: true
    property bool wrongPassword: false

    signal passwordGiven(string password)

    readonly property bool matches: !turningOn || field.text === confirm.text

    title: turningOn ? i18nc("@title:window", "Turn on device encryption")
                     : i18nc("@title:window", "Turn off device encryption")
    text: turningOn ? i18nc("@info", "Raye encrypts the disk in the background while you use your PC. It starts after one restart and can take an hour or more. The security chip then unlocks the disk at start-up, so you don't have to type anything.")
                    : i18nc("@info", "Raye decrypts the disk in the background while you use your PC. Once it's done, anyone who takes the disk out of this PC can read your files.")
    primaryText: turningOn ? i18nc("@action:button", "Next") : i18nc("@action:button", "Turn off")
    primaryEnabled: field.text.length > 0 && dialog.matches

    onOpened: field.forceActiveFocus()
    onAccepted: {
        const password = field.text
        field.clear()
        confirm.clear()
        dialog.passwordGiven(password)
    }
    onRejected: {
        field.clear()
        confirm.clear()
        dialog.wrongPassword = false
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s2
        text: dialog.turningOn ? i18nc("@label", "New disk password") : i18nc("@label", "Disk password")
        font: Theme.bodyStrong
        color: Theme.text
    }
    PasswordField {
        id: field
        Layout.fillWidth: true
        placeholderText: dialog.turningOn ? i18nc("@label", "New disk password") : i18nc("@label", "Disk password")
        onAccepted: dialog.turningOn ? confirm.forceActiveFocus() : dialog._acceptByKey()
        onTextChanged: if (text.length > 0) dialog.wrongPassword = false
    }
    Text {
        Layout.fillWidth: true
        visible: dialog.turningOn
        text: i18nc("@info", "You need it only if the security chip can't unlock the disk, for example after a firmware change.")
        font: Theme.caption
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s2
        visible: dialog.turningOn
        text: i18nc("@label", "Confirm password")
        font: Theme.bodyStrong
        color: Theme.text
    }
    PasswordField {
        id: confirm
        Layout.fillWidth: true
        visible: dialog.turningOn
        placeholderText: i18nc("@label", "Confirm password")
        onAccepted: dialog._acceptByKey()
    }
    RowLayout {
        Layout.fillWidth: true
        visible: dialog.wrongPassword || (dialog.turningOn && confirm.text.length > 0 && !dialog.matches)
        spacing: Theme.s3
        StatusGlyph {
            status: "critical"
            size: 16
            Layout.alignment: Qt.AlignTop
        }
        Text {
            Layout.fillWidth: true
            text: dialog.wrongPassword ? i18nc("@info", "That password didn't open the disk.")
                                       : i18nc("@info", "The passwords don't match.")
            font: Theme.body
            color: Theme.statusColor("critical")
            wrapMode: Text.Wrap
        }
    }
}
