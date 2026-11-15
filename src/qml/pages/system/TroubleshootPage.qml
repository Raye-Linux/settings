// SPDX-License-Identifier: GPL-3.0-or-later
// System > Troubleshoot: the troubleshooters, most frequent first, each
// with Run. A troubleshooter runs in a dialog that shows each check,
// asks before fixing anything, and ends with what was fixed.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Troubleshoot") }]

    function run(name) {
        dialog.start(name)
    }

    TroubleshootDialog { id: dialog }

    SectionHeader {
        text: i18n("Most frequent")
        first: true
    }

    ButtonCard {
        iconName: "org.raye.settings-sound"
        iconFallback: "audio-volume-high"
        title: i18nc("@title troubleshooter", "Audio")
        description: i18n("Find and fix problems with playing sound")
        searchId: "audio"
        buttonText: i18nc("@action:button run a troubleshooter", "Run")
        onButtonClicked: page.run("audio")
    }
    ButtonCard {
        iconName: "org.raye.settings-network"
        iconFallback: "network-wired"
        title: i18nc("@title troubleshooter", "Internet connections")
        description: i18n("Find and fix problems with connecting to the internet")
        searchId: "network"
        buttonText: i18nc("@action:button run a troubleshooter", "Run")
        onButtonClicked: page.run("network")
    }
    ButtonCard {
        iconName: "org.raye.settings-printers"
        iconFallback: "printer"
        title: i18nc("@title troubleshooter", "Printer")
        description: i18n("Find and fix problems with printing")
        searchId: "printer"
        buttonText: i18nc("@action:button run a troubleshooter", "Run")
        onButtonClicked: page.run("printer")
    }
    ButtonCard {
        iconName: "org.raye.settings-raye-update"
        iconFallback: "system-software-update"
        title: i18nc("@title troubleshooter", "Raye Update")
        description: i18n("Find and fix problems that keep updates from installing")
        searchId: "update"
        buttonText: i18nc("@action:button run a troubleshooter", "Run")
        onButtonClicked: page.run("update")
    }

    SectionHeader { text: i18n("Other") }

    ButtonCard {
        iconName: "org.raye.settings-bluetooth"
        iconFallback: "preferences-system-bluetooth"
        title: i18nc("@title troubleshooter", "Bluetooth")
        description: i18n("Find and fix problems with Bluetooth devices")
        searchId: "bluetooth"
        buttonText: i18nc("@action:button run a troubleshooter", "Run")
        onButtonClicked: page.run("bluetooth")
    }
    ButtonCard {
        iconName: "org.raye.settings-display"
        iconFallback: "preferences-desktop-display"
        title: i18nc("@title troubleshooter", "Display")
        description: i18n("Find and fix problems with a screen that stays dark")
        searchId: "display"
        buttonText: i18nc("@action:button run a troubleshooter", "Run")
        onButtonClicked: page.run("display")
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-recovery"
        iconFallback: "system-reboot"
        title: i18n("Recovery")
        description: i18n("If these did not help, repair Raye or go back to an earlier point")
        target: "system/recovery"
    }
}
