// SPDX-License-Identifier: GPL-3.0-or-later
// The recovery key of a disk encrypted from Settings, shown this one
// time. It closes only once the key was saved, printed or written down.
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

ContentDialog {
    id: dialog

    property var encryption
    property bool keptSomewhere: false
    property string saveError: ""

    title: i18nc("@title:window", "Back up your recovery key")
    text: i18nc("@info", "If the security chip can't unlock the disk and you forget its password, this key is the only way to get your files back. Raye shows it only this once.")
    primaryText: i18nc("@action:button", "Done")
    primaryEnabled: keptSomewhere
    closeText: ""
    closePolicy: T.Popup.NoAutoClose

    onAccepted: {
        encryption.forgetRecoveryKey()
        keptSomewhere = false
        saveError = ""
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: keyText.implicitHeight + 2 * Theme.s5
        radius: Theme.radiusControl
        color: Theme.cardSolid
        border.width: 1
        border.color: Theme.strokeSurface
        Text {
            id: keyText
            anchors.centerIn: parent
            width: parent.width - 2 * Theme.s5
            horizontalAlignment: Text.AlignHCenter
            // two lines of four groups read out more easily
            text: {
                const groups = (dialog.encryption ? dialog.encryption.recoveryKey : "").split("-")
                return groups.slice(0, 4).join("-") + "\n" + groups.slice(4).join("-")
            }
            font.family: "Cascadia Mono"
            font.pointSize: 14
            color: Theme.text
            wrapMode: Text.WrapAnywhere
            Accessible.name: dialog.encryption ? dialog.encryption.recoveryKey : ""
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s3
        Button {
            text: i18nc("@action:button", "Save to a file")
            onClicked: saveDialog.open()
        }
        Button {
            visible: dialog.encryption && dialog.encryption.canPrint()
            text: i18nc("@action:button", "Print")
            onClicked: {
                if (dialog.encryption.printRecoveryKey()) {
                    dialog.keptSomewhere = true
                    dialog.saveError = ""
                } else {
                    dialog.saveError = "print"
                }
            }
        }
        Item { Layout.fillWidth: true }
    }
    NetworkCheckBox {
        Layout.fillWidth: true
        text: i18nc("@option:check", "I wrote it down")
        onToggled: if (checked) dialog.keptSomewhere = true
    }
    Text {
        Layout.fillWidth: true
        visible: dialog.saveError !== ""
        text: dialog.saveError === "same-disk" ? i18nc("@info", "Save it on a USB drive or another disk. A key kept on the disk it unlocks can't help you.")
            : dialog.saveError === "print" ? i18nc("@info", "Nothing was printed. Check that a printer is set up, or save the key to a file.")
            : i18nc("@info", "The key couldn't be saved there.")
        font: Theme.body
        color: Theme.statusColor("critical")
        wrapMode: Text.Wrap
    }
    Text {
        Layout.fillWidth: true
        visible: dialog.keptSomewhere && dialog.saveError === ""
        text: i18nc("@info", "Keep it somewhere safe, away from this PC.")
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    FileDialog {
        id: saveDialog
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [i18nc("@item file type", "Text files (*.txt)")]
        onAccepted: {
            const error = dialog.encryption.saveRecoveryKey(selectedFile)
            dialog.saveError = error
            if (error === "")
                dialog.keptSomewhere = true
        }
    }
}
