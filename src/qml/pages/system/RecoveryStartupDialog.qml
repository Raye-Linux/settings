// SPDX-License-Identifier: GPL-3.0-or-later
// Recovery › Advanced startup: restart into the firmware settings, or
// to the start-up menu (shown once, then Raye starts as usual).
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    required property Recovery recovery

    objectName: "advancedStartup"
    title: i18nc("@title:window", "Advanced startup")
    text: i18nc("@info", "Choose where the computer goes when it restarts. Save your work first.")
    primaryText: i18nc("@action:button", "Restart now")
    primaryEnabled: (firmware.visible && firmware.checked) || (menu.visible && menu.checked)
    onAboutToShow: {
        firmware.checked = recovery.firmwareAvailable
        menu.checked = !recovery.firmwareAvailable && recovery.bootMenuAvailable
    }
    onAccepted: {
        if (firmware.visible && firmware.checked)
            recovery.restartToFirmware()
        else if (menu.visible && menu.checked)
            recovery.restartToBootMenu()
    }

    ColumnLayout {
        Layout.fillWidth: true
        Layout.preferredWidth: 440
        spacing: Theme.s3

        RadioCard {
            id: firmware
            Layout.fillWidth: true
            visible: dialog.recovery.firmwareAvailable
            iconName: "org.raye.settings-firmware"
            title: i18nc("@option:radio", "Firmware settings")
            description: i18nc("@info", "The settings of the PC itself (UEFI): start order, Secure Boot, hardware.")
        }
        RadioCard {
            id: menu
            Layout.fillWidth: true
            visible: dialog.recovery.bootMenuAvailable
            iconName: "org.raye.settings-recovery"
            title: i18nc("@option:radio", "Start-up menu")
            description: i18nc("@info", "Choose a system or a way to start, this time only.")
        }
    }
}
