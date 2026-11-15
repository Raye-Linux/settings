// SPDX-License-Identifier: GPL-3.0-or-later
// System › Recovery: the ways back when something goes wrong. Restart
// to Windows, a troubleshooter, repair, go back to a snapshot, reset the
// settings, advanced startup, and how to reinstall.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    // the handbook page on making a Raye USB stick, in the user's language
    readonly property bool french: Qt.locale().name.startsWith("fr")
    readonly property string usbStickUrl: french
        ? "https://github.com/Raye-Linux/handbook/blob/main/fr/01-installer.md"
        : "https://github.com/Raye-Linux/handbook/blob/main/en/01-installing.md"
    // what went wrong last, for the bar at the top
    property string problem: ""
    // nothing restarts or goes back while apt is putting files back
    readonly property bool repairing: repairCard.running

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Recovery") }]

    Recovery {
        id: recovery
        onFailed: (what) => page.problem = what
    }
    Component.onCompleted: recovery.refresh()

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.problem !== ""
        severity: page.problem === "logout-failed" ? "warning" : "error"
        title: page.problem === "logout-failed" ? i18nc("@info", "Couldn't sign you out")
             : page.problem === "boot-menu-unavailable" ? i18nc("@info", "The start-up menu can't be opened from here")
             : page.problem === "reset-failed" ? i18nc("@info", "Your settings couldn't be reset")
             : page.problem === "denied" ? i18nc("@info", "This needs root access")
             : i18nc("@info", "Raye couldn't restart")
        message: page.problem === "logout-failed" ? i18nc("@info", "Your settings will change the next time you sign in.")
               : page.problem === "boot-menu-unavailable" ? i18nc("@info", "Restart, then hold Esc while the computer starts.")
               : page.problem === "reset-failed" ? i18nc("@info", "Nothing was changed. Try again.")
               : page.problem === "denied" ? i18nc("@info", "Ask the person who manages this PC.")
               : i18nc("@info", "Save your work and try again.")
        onClosed: page.problem = ""
    }

    // a reset or an undo waits for the next sign-in
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: recovery.resetPending !== "none"
        closable: false
        severity: "informational"
        title: recovery.resetPending === "undo" ? i18nc("@info", "Your settings will come back when you sign in again")
                                                : i18nc("@info", "Your settings will be reset when you sign in again")
        message: i18nc("@info", "Save your work, then sign out.")
        Button {
            text: i18nc("@action:button", "Sign out now")
            accent: true
            enabled: !recovery.busy
            onClicked: recovery.signOut()
        }
        Button {
            text: i18nc("@action:button", "Cancel")
            enabled: !recovery.busy
            onClicked: recovery.cancelReset()
        }
    }

    SectionHeader {
        text: i18nc("@title:group", "Recovery options")
        first: true
    }

    ButtonCard {
        Layout.fillWidth: true
        visible: recovery.windowsAvailable
        searchId: "restart-windows"
        iconName: "org.raye.settings-windows-programs"
        iconFallback: "system-reboot"
        title: i18nc("@label", "Restart to Windows")
        description: i18nc("@info", "Start Windows once, then come back to Raye next time.")
        buttonText: i18nc("@action:button", "Restart to Windows")
        enabled: !recovery.busy && !page.repairing
        onButtonClicked: windowsDialog.open()
    }

    NavCard {
        Layout.fillWidth: true
        searchId: "troubleshoot"
        iconName: "org.raye.settings-troubleshoot"
        iconFallback: "tools-wizard"
        title: i18nc("@label", "Fix problems without resetting your PC")
        description: i18nc("@info", "Resetting takes a while. First, try a troubleshooter.")
        target: "system/troubleshoot"
    }

    ButtonCard {
        id: repairCard
        Layout.fillWidth: true
        readonly property bool running: ["update", "check", "download", "install"].indexOf(recovery.repairState) >= 0
        searchId: "repair"
        iconName: "org.raye.settings-recovery"
        iconFallback: "run-build-install"
        title: i18nc("@label", "Repair Raye")
        description: running ? i18nc("@info repair under way, a percentage", "Repairing… %1%", recovery.repairProgress)
                             : i18nc("@info", "Reinstall Raye's system files. Your files, apps and settings stay.")
        buttonText: running ? i18nc("@action:button", "Show progress") : i18nc("@action:button", "Repair now")
        enabled: !recovery.busy
        onButtonClicked: running || recovery.repairState === "done" || recovery.repairState === "error"
                         ? repairDialog.showProgress() : repairDialog.open()
    }

    ButtonCard {
        Layout.fillWidth: true
        visible: recovery.goBackAvailable
        searchId: "go-back"
        iconName: "org.raye.settings-update-history"
        iconFallback: "edit-undo"
        title: i18nc("@label", "Go back to an earlier state")
        description: i18nc("@info", "Undo a change to the system, like an update that went wrong. Your files stay.")
        buttonText: i18nc("@action:button", "Go back")
        enabled: !recovery.busy && !page.repairing
        onButtonClicked: goBackDialog.open()
    }

    ButtonCard {
        Layout.fillWidth: true
        searchId: "reset-settings"
        iconName: "org.raye.settings-taskbar"
        iconFallback: "edit-reset"
        title: i18nc("@label", "Reset your settings")
        description: i18nc("@info", "Put the desktop, taskbar and app settings back as they were when Raye was installed. Your files and apps stay.")
        buttonText: i18nc("@action:button", "Reset")
        enabled: !recovery.busy && recovery.resetPending === "none"
        onButtonClicked: resetDialog.openReset()
    }

    ButtonCard {
        Layout.fillWidth: true
        visible: recovery.undoAvailable
        searchId: "undo-reset"
        iconName: "org.raye.settings-taskbar"
        iconFallback: "edit-undo"
        title: i18nc("@label", "Undo the last reset")
        description: i18ncp("@info %2 is a date", "Put back the settings you had before %2. You can do this for 1 more day.",
                            "Put back the settings you had before %2. You can do this for %1 more days.",
                            recovery.undoDaysLeft, recovery.undoDate)
        buttonText: i18nc("@action:button", "Undo")
        enabled: !recovery.busy && recovery.resetPending === "none"
        onButtonClicked: resetDialog.openUndo()
    }

    ButtonCard {
        Layout.fillWidth: true
        visible: recovery.firmwareAvailable || recovery.bootMenuAvailable
        searchId: "advanced-startup"
        iconName: "org.raye.settings-firmware"
        iconFallback: "system-reboot"
        title: i18nc("@label", "Advanced startup")
        description: i18nc("@info", "Restart to the firmware settings (UEFI) or to the start-up menu.")
        buttonText: i18nc("@action:button", "Restart now")
        enabled: !recovery.busy && !page.repairing
        onButtonClicked: startupDialog.open()
    }

    Card {
        Layout.fillWidth: true
        searchId: "reinstall"
        iconName: "org.raye.settings-storage"
        iconFallback: "drive-removable-media-usb"
        title: i18nc("@label", "Remove everything and reinstall")
        description: i18nc("@info", "To start over, reinstall Raye from a USB stick. Back up your files first.")
        HyperlinkButton {
            text: i18nc("@action:button", "How to make a Raye USB stick")
            url: page.usbStickUrl
        }
    }

    SectionHeader { text: i18nc("@title:group", "Related support") }

    LinkCard {
        Layout.fillWidth: true
        searchId: "recovery-drive"
        iconName: "org.raye.settings-autoplay"
        iconFallback: "drive-removable-media-usb"
        title: i18nc("@label", "Create a recovery drive")
        description: i18nc("@info", "A Raye USB stick can repair or reinstall this PC, even when it doesn't start.")
        url: page.usbStickUrl
    }

    ContentDialog {
        id: windowsDialog
        title: i18nc("@title:window", "Save your work before you restart")
        text: i18nc("@info", "Your apps close and the computer restarts into Windows. The time after that, it starts Raye again.")
        primaryText: i18nc("@action:button", "Restart now")
        onAccepted: recovery.restartToWindows()
    }

    RecoveryRepairDialog { id: repairDialog; recovery: recovery }
    RecoveryGoBackDialog { id: goBackDialog; recovery: recovery }
    RecoveryResetDialog { id: resetDialog; recovery: recovery }
    RecoveryStartupDialog { id: startupDialog; recovery: recovery }
}
