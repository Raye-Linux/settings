// SPDX-License-Identifier: GPL-3.0-or-later
// Privacy & security › Device security: what protects this PC (Windows
// Security's page, in Raye's words). The banner says whether anything
// needs a look; then Secure Boot (and how to turn it on), disk encryption
// (turned on or off with the PC on, "Use the security chip again"), the
// security chip, the recovery key and the firewall.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    readonly property bool encrypted: system.encrypted
    readonly property bool hasChip: system.tpm
    // the chip opened the disk until a firmware or kernel change
    readonly property bool chipStale: tpm.state === "stale"
    readonly property var actions: system.actionsNeeded(system.secureBoot, tpm.state, encrypted && system.recoveryKeyFile,
                                                        firewall.installed ? (firewall.enabled ? 1 : 0) : -1)

    crumbs: [{ text: i18nc("@title settings category", "Privacy & security"), target: "privacy" },
             { text: i18nc("@title settings page", "Device security") }]

    banner: Component {
        SecurityBanner { actions: page.actions }
    }

    Tpm {
        id: tpm
        onRebindFinished: (result) => {
            if (result === "wrong-password") {
                rebindDialog.wrongPassword = true
                rebindDialog.open()
            }
            system.refresh()
        }
    }
    Firewall { id: firewall }
    Recovery { id: recovery }
    DiskEncryption {
        id: encryption
        onResultChanged: {
            if (result === "wrong-password") {
                passwordDialog.wrongPassword = true
                passwordDialog.open()
            } else if (result === "started" && recoveryKey !== "") {
                keyDialog.open()
            }
            system.refresh()
        }
        // the background run just ended: the disk and the chip changed
        property string lastPhase: ""
        onChanged: {
            if ((lastPhase === "running" || lastPhase === "paused") && phase !== lastPhase) {
                system.refresh()
                tpm.refresh()
            }
            lastPhase = phase
        }
    }

    // the encryption in the background, as the page shows it
    readonly property bool encryptionMoving: encryption.phase === "running" || encryption.phase === "paused"
    readonly property bool canTurnOn: !page.encrypted && encryption.known && encryption.blocked === ""
                                      && encryption.phase !== "restart" && !page.encryptionMoving
    readonly property bool canTurnOff: page.encrypted && encryption.known && encryption.blocked === ""
                                       && !page.encryptionMoving

    Component.onCompleted: {
        system.refresh()
        tpm.refresh()
        encryption.refresh()
    }

    EncryptionPasswordDialog {
        id: passwordDialog
        onPasswordGiven: (password) => turningOn ? encryption.turnOn(password) : encryption.turnOff(password)
    }
    RecoveryKeyDialog {
        id: keyDialog
        encryption: encryption
    }
    ContentDialog {
        id: firmwareDialog
        title: i18nc("@title:window", "Restart to firmware settings?")
        text: i18nc("@info", "Your PC restarts and opens its firmware settings. Save your work first.")
        primaryText: i18nc("@action:button", "Restart now")
        onAccepted: recovery.restartToFirmware()
    }

    TpmRebindDialog {
        id: rebindDialog
        onPasswordGiven: (password) => tpm.rebind(password)
    }

    // --- how the last "Use the security chip again" went ----------------
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: tpm.busy
        closable: false
        severity: "informational"
        title: i18nc("@info", "Checking the password…")
        message: i18nc("@info", "This takes a few seconds.")
        ProgressBar { indeterminate: true; width: 120 }
    }
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: tpm.result === "done"
        severity: "success"
        title: i18nc("@info", "Done")
        message: i18nc("@info", "The disk opens by itself again at the next start.")
        onClosed: tpm.clearResult()
    }
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: tpm.result === "failed"
        severity: "error"
        title: i18nc("@info", "The security chip couldn't be set up again")
        message: i18nc("@info", "The disk keeps asking for its password at start-up. Restart the PC and try again.")
        onClosed: tpm.clearResult()
    }
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: encryption.busy
        closable: false
        severity: "informational"
        title: i18nc("@info", "Getting your PC ready…")
        message: i18nc("@info", "This takes about a minute.")
        ProgressBar { indeterminate: true; width: 120 }
    }
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: encryption.result === "failed" || encryption.result === "refused"
        severity: "error"
        title: i18nc("@info", "Encryption couldn't be changed")
        message: i18nc("@info", "Nothing was changed on the disk. Try again in a moment.")
        onClosed: encryption.clearResult()
    }
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: encryption.phase === "restart" && !encryption.busy && !keyDialog.opened
        closable: false
        severity: "warning"
        title: i18nc("@info", "Restart to start encrypting")
        message: i18nc("@info", "Encryption starts after your PC restarts. Save your work first.")
        Button {
            text: i18nc("@action:button", "Restart now")
            accent: true
            onClicked: encryption.restart()
        }
    }
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: firewall.failed
        severity: "error"
        title: i18nc("@info", "The firewall couldn't be changed")
        message: i18nc("@info", "Try again in a moment.")
        onClosed: firewall.refresh()
    }

    SectionHeader {
        text: i18nc("@title:group", "Protection")
        first: true
    }

    Expander {
        Layout.fillWidth: true
        searchId: "secure-boot"
        iconName: "security-high"
        title: i18nc("@label", "Secure Boot")
        description: system.secureBoot === 1 ? i18nc("@info", "On. Only trusted software can start this PC.")
                   : system.secureBoot === 0 ? i18nc("@info", "Off. Turn it on in your PC's firmware settings to stop harmful software from starting before Raye.")
                   : i18nc("@info", "This PC starts in legacy mode, so Secure Boot isn't available.")
        expanded: system.secureBoot === 0
        headerContent: SecurityStatus {
            state: system.secureBoot === 1 ? "success" : system.secureBoot === 0 ? "caution" : "info"
            text: system.secureBoot === 1 ? i18nc("@label a protection that is on", "On")
                : system.secureBoot === 0 ? i18nc("@label a protection that is off", "Off")
                : i18nc("@label", "Not available")
        }

        ExpanderRow {
            visible: system.secureBoot === 1
            title: i18nc("@label", "What it does")
            description: i18nc("@info", "Your PC checks the signature of everything that starts before Raye: the boot menu and the system itself. Nothing else needs to be done.")
        }
        ExpanderRow {
            visible: system.secureBoot === 0
            title: i18nc("@label", "How to turn it on")
            description: encryption.setupMode
                ? i18nc("@info", "1. Restart your PC into its firmware settings.\n2. Find Secure Boot, often under Boot or Security.\n3. Choose to restore the factory keys, then set Secure Boot to Enabled.\n4. Save your changes and exit. Raye starts as usual.")
                : i18nc("@info", "1. Restart your PC into its firmware settings.\n2. Find Secure Boot, often under Boot or Security.\n3. Set it to Enabled and keep the standard keys.\n4. Save your changes and exit. Raye starts as usual.")
        }
        ExpanderRow {
            visible: system.secureBoot === 0
            title: i18nc("@label", "Firmware settings")
            description: recovery.firmwareAvailable ? i18nc("@info", "Your PC can open them for you when it restarts.")
                                                    : i18nc("@info", "Press the key your PC shows when it starts, often F2, F10, Del or Esc.")
            Button {
                visible: recovery.firmwareAvailable
                text: i18nc("@action:button", "Restart to firmware settings")
                onClicked: firmwareDialog.open()
            }
        }
    }

    Expander {
        Layout.fillWidth: true
        searchId: "disk-encryption"
        iconName: "drive-harddisk-encrypted"
        title: i18nc("@label", "Device encryption")
        description: page.encryptionMoving && encryption.mode === "encrypt" ? i18nc("@info", "Encrypting the disk. You can keep using your PC.")
                   : page.encryptionMoving ? i18nc("@info", "Decrypting the disk. You can keep using your PC.")
                   : encryption.phase === "restart" ? i18nc("@info", "Starts when your PC restarts.")
                   : !page.encrypted ? i18nc("@info", "Off. Someone who takes the disk out of this PC can read your files.")
                   : page.chipStale ? i18nc("@info", "On. The disk asks for its password at start-up until the security chip is set up again.")
                   : page.hasChip ? i18nc("@info", "On. The security chip unlocks the disk when the PC starts; its password works too.")
                   : i18nc("@info", "On. The disk asks for its password when the PC starts.")
        expanded: page.encryptionMoving
        headerContent: SecurityStatus {
            state: page.encryptionMoving || encryption.phase === "restart" ? "info"
                 : !page.encrypted ? "info" : page.chipStale ? "caution" : "success"
            text: page.encryptionMoving ? i18nc("@label percent done", "%1%", Math.floor(encryption.percent))
                : page.encrypted ? i18nc("@label a protection that is on", "On") : i18nc("@label a protection that is off", "Off")
        }

        ExpanderRow {
            visible: page.encryptionMoving
            title: encryption.phase === "paused" && encryption.error === "battery" ? i18nc("@label", "Paused: the battery is low")
                 : encryption.phase === "paused" ? i18nc("@label", "Paused")
                 : encryption.mode === "encrypt" ? i18nc("@label", "Encrypting… %1%", Math.floor(encryption.percent))
                 : i18nc("@label", "Decrypting… %1%", Math.floor(encryption.percent))
            description: encryption.phase === "paused" && encryption.error === "battery" ? i18nc("@info", "Plug in your PC, then select Resume.")
                       : encryption.phase === "paused" ? i18nc("@info", "It goes on by itself when your PC restarts, or select Resume.")
                       : i18nc("@info", "If your PC turns off, it picks up where it left off at the next start.")
            ColumnLayout {
                spacing: Theme.s2
                ProgressBar {
                    Layout.preferredWidth: 160
                    from: 0
                    to: 100
                    value: encryption.percent
                    status: encryption.phase === "paused" ? "caution" : ""
                }
                Button {
                    visible: encryption.phase === "paused"
                    Layout.alignment: Qt.AlignRight
                    text: i18nc("@action:button", "Resume")
                    onClicked: encryption.resume()
                }
            }
        }
        ExpanderRow {
            visible: encryption.phase === "failed"
            title: i18nc("@label", "Stopped")
            description: i18nc("@info", "Something went wrong and the work stopped. Your files are safe, and the disk opens as before. Restart your PC and try again.")
        }
        ExpanderRow {
            visible: !page.encrypted && !page.encryptionMoving && encryption.phase !== "restart"
            title: i18nc("@label", "Encrypt this disk")
            description: !encryption.known || encryption.blocked === "" ? i18nc("@info", "Encrypts the whole disk while you keep using your PC. You need one restart.")
                       : encryption.blocked === "battery" ? i18nc("@info", "Plug in your PC first. Its battery is below 50 %.")
                       : encryption.blocked === "space" ? i18nc("@info", "Free up at least 2 GB on the system disk first.")
                       : encryption.blocked === "boot-space" ? i18nc("@info", "There isn't enough room left on the start-up partition.")
                       : encryption.blocked === "no-tpm" ? i18nc("@info", "This PC has no security chip, so encryption can only be chosen when Raye is installed.")
                       : encryption.blocked === "unsupported-disk" ? i18nc("@info", "This disk can't be encrypted while Raye runs. Encryption can be chosen when Raye is installed.")
                       : i18nc("@info", "Raye can't read the state of the disk right now.")
            Button {
                text: i18nc("@action:button", "Turn on")
                enabled: page.canTurnOn && !encryption.busy
                onClicked: {
                    encryption.clearResult()
                    passwordDialog.turningOn = true
                    passwordDialog.wrongPassword = false
                    passwordDialog.open()
                }
            }
        }
        ExpanderRow {
            visible: page.encrypted
            title: i18nc("@label", "Unlocking")
            description: !page.hasChip ? i18nc("@info", "With the disk password, each time the PC starts.")
                       : page.chipStale ? i18nc("@info", "With the disk password, until the security chip is set up again.")
                       : tpm.state === "ok" ? i18nc("@info", "By the security chip, without a password, as long as nothing changed in how the PC starts.")
                       : i18nc("@info", "By the security chip when it can, otherwise with the disk password.")
        }
        ExpanderRow {
            // when the chip is known to fail, the warning below has the button
            visible: page.encrypted && page.hasChip && tpm.state !== "none" && !page.chipStale && !page.encryptionMoving
            searchId: "tpm-rebind"
            title: i18nc("@label", "Disk asks for its password again?")
            description: i18nc("@info", "Let the security chip open the disk by itself again. You need the disk password once.")
            Button {
                text: i18nc("@action:button", "Use the security chip again")
                enabled: !tpm.busy
                onClicked: {
                    tpm.clearResult()
                    rebindDialog.wrongPassword = false
                    rebindDialog.open()
                }
            }
        }
        ExpanderRow {
            visible: page.encrypted
            title: i18nc("@label", "Recovery key")
            description: i18nc("@info", "If you forget the disk password, the recovery key you got when encryption was turned on opens the disk. Keep it somewhere safe, away from this PC.")
        }
        ExpanderRow {
            visible: page.encrypted && !page.encryptionMoving
            title: i18nc("@label", "Turn off encryption")
            description: page.canTurnOff ? i18nc("@info", "Decrypts the disk while you keep using your PC. No restart needed.")
                       : encryption.blocked === "battery" ? i18nc("@info", "Plug in your PC first. Its battery is below 50 %.")
                       : encryption.blocked === "boot-space" ? i18nc("@info", "There isn't enough room left on the start-up partition.")
                       : i18nc("@info", "This disk can't be decrypted while Raye runs.")
            Button {
                text: i18nc("@action:button", "Turn off")
                enabled: page.canTurnOff && !encryption.busy
                onClicked: {
                    encryption.clearResult()
                    passwordDialog.turningOn = false
                    passwordDialog.wrongPassword = false
                    passwordDialog.open()
                }
            }
        }
    }

    // the warning of the spec, right under the card it is about
    InfoBar {
        Layout.fillWidth: true
        open: page.encrypted && page.chipStale && !tpm.busy && tpm.result !== "done"
        closable: false
        severity: "warning"
        title: i18nc("@info", "The disk asks for its password again")
        message: i18nc("@info", "The security chip no longer opens your disk, so it asks for its password at start-up. This happens after a firmware or kernel change.")
        Button {
            text: i18nc("@action:button", "Use the security chip again")
            accent: true
            onClicked: {
                tpm.clearResult()
                rebindDialog.wrongPassword = false
                rebindDialog.open()
            }
        }
    }

    Card {
        Layout.fillWidth: true
        searchId: "security-chip"
        iconName: "cpu"
        title: encryption.tpmVersion >= 2 ? i18nc("@label", "Security chip (TPM 2.0)")
             : encryption.tpmVersion === 1 ? i18nc("@label", "Security chip (TPM 1.2)")
             : i18nc("@label", "Security chip (TPM)")
        description: !page.hasChip ? i18nc("@info", "Not found. Some PCs have one that is turned off in the firmware settings.")
                   : page.encrypted && tpm.state === "ok" ? i18nc("@info", "Keeps the keys of this PC safe, and unlocks the disk when the PC starts.")
                   : page.encrypted ? i18nc("@info", "Keeps the keys of this PC safe. It doesn't unlock the disk right now.")
                   : i18nc("@info", "Keeps the keys of this PC safe. It unlocks the disk once device encryption is on.")
        SecurityStatus {
            state: page.hasChip ? "success" : "info"
            text: page.hasChip ? i18nc("@label the security chip", "Ready") : i18nc("@label", "Not found")
        }
    }

    Card {
        Layout.fillWidth: true
        visible: page.encrypted && system.recoveryKeyFile
        searchId: "recovery-key"
        iconName: "dialog-password"
        title: i18nc("@label", "Recovery key")
        description: i18nc("@info", "Your recovery key is still saved on this PC, in \".local/share/raye/recovery-key\". Write it down or print it, keep it somewhere away from this PC, then delete the file.")
        SecurityStatus {
            state: "caution"
            text: i18nc("@label", "Action needed")
        }
    }

    SectionHeader {
        text: i18nc("@title:group", "Firewall")
    }

    ToggleCard {
        id: firewallCard
        // what the user asked for while the change is running
        property bool wanted: false

        Layout.fillWidth: true
        visible: firewall.installed
        searchId: "firewall"
        iconName: "security-medium"
        title: i18nc("@label", "Firewall")
        description: checked ? i18nc("@info", "Blocks connections that other computers start to this PC. Your apps still reach the internet.")
                             : i18nc("@info", "Off. Other computers on the network can connect to this PC.")
        enabled: !firewall.busy
        checked: firewall.busy ? wanted : firewall.enabled
        onToggled: {
            wanted = checked
            firewall.setEnabled(checked)
            // back to following the firewall, whatever the answer
            checked = Qt.binding(() => firewall.busy ? firewallCard.wanted : firewall.enabled)
        }
    }
    Card {
        Layout.fillWidth: true
        visible: !firewall.installed
        iconName: "security-medium"
        title: i18nc("@label", "Firewall")
        description: i18nc("@info", "The firewall isn't installed. Ask the person who manages this PC to install it again.")
        SecurityStatus { state: "caution"; text: i18nc("@label", "Not installed") }
    }
    LinkCard {
        Layout.fillWidth: true
        visible: firewall.installed && firewall.canOpenRules()
        searchId: "firewall-rules"
        iconName: "configure"
        title: i18nc("@label", "Allow an app through the firewall")
        description: i18nc("@info", "Rules for the connections other computers may start")
        onClicked: firewall.openRules()
    }

    SectionHeader {
        text: i18nc("@title:group", "Related")
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-virus"
        iconFallback: "security-high"
        title: i18nc("@title settings page", "Virus & threat protection")
        description: i18nc("@info", "How Raye keeps your PC safe")
        target: "privacy/virus"
    }
}
