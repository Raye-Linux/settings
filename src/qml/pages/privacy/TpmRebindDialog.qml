// SPDX-License-Identifier: GPL-3.0-or-later
// "Use the security chip again": asks for the disk password, the one
// typed at start-up. The dialog closes before the work starts, so the
// administrator's password prompt never opens over it; the page shows
// how it went, and reopens this dialog when the password was wrong.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    // the password was tried and did not open the disk
    property bool wrongPassword: false

    signal passwordGiven(string password)

    title: i18nc("@title:window", "Use the security chip again")
    text: i18nc("@info", "Type the password you use to unlock the disk when the PC starts. The security chip then opens the disk by itself again.")
    primaryText: i18nc("@action:button", "Continue")
    primaryEnabled: field.text.length > 0

    onOpened: field.forceActiveFocus()
    onAccepted: {
        // read once, then gone from the field
        const password = field.text
        field.clear()
        dialog.passwordGiven(password)
    }
    onRejected: {
        field.clear()
        dialog.wrongPassword = false
    }

    PasswordField {
        id: field
        Layout.fillWidth: true
        placeholderText: i18nc("@label", "Disk password")
        onAccepted: dialog._acceptByKey()
        onTextChanged: if (text.length > 0) dialog.wrongPassword = false
    }
    RowLayout {
        Layout.fillWidth: true
        visible: dialog.wrongPassword
        spacing: Theme.s3
        StatusGlyph {
            status: "critical"
            size: 16
            Layout.alignment: Qt.AlignTop
        }
        Text {
            Layout.fillWidth: true
            text: i18nc("@info", "That password didn't open the disk.")
            font: Theme.body
            color: Theme.statusColor("critical")
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignLeft
        }
    }
}
