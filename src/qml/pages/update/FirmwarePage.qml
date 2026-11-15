// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Update › Firmware updates: the parts of the PC that fwupd can
// update, the version each runs, and the newer one when there is one.
// Raye Update downloads the cabinet with the other updates and installs
// it at the next start-up, so the button here either asks it to look
// and download, or restarts into the install.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.raye.settings
import "updatetext.js" as U

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings page", "Raye Update"), target: "update" },
             { text: i18nc("@title settings page", "Firmware updates") }]

    readonly property var status: typeof updates !== "undefined" ? updates : null
    // a device already at the offered version or newer is up to date, even
    // when fwupd still offers it (the dbx it could not read back)
    readonly property var withUpdate: firmware.devices.filter(d => d.to !== "" && !U.firmwareCurrent(d.version, d.to))
    readonly property var upToDate: firmware.devices.filter(d => d.to === "" || U.firmwareCurrent(d.version, d.to))
    // what Raye Update downloaded and fwupd will only install on mains power
    readonly property bool wantsCharger: status !== null && status.firmware.some(f => f.needsPower)

    FirmwareDevices { id: firmware }

    // when Raye Update has fetched something new, read the devices again
    Connections {
        target: page.status
        function onChanged() {
            if (page.status.phase === "ready" || page.status.phase === "idle")
                firmware.refresh()
        }
        function onRestartFailed(reason) {
            restartError.text = reason === "cancelled" ? i18n("The update wasn't set up because the password wasn't entered. Try again.")
                              : reason === "failed" ? i18n("The update couldn't be set up to install. Your PC wasn't restarted. Try again later.")
                              : i18n("Your PC couldn't be restarted. Save your work and restart it from Start.")
            restartError.open()
        }
    }

    function sizeText(bytes) {
        return bytes >= 1048576 ? i18nc("@info size", "%1 MB", Math.max(1, Math.round(bytes / 1048576)))
             : i18nc("@info size", "%1 KB", Math.max(1, Math.round(bytes / 1024)))
    }

    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s4
        text: i18nc("@info", "Firmware is the software built into the parts of your PC, like its motherboard or its drive. Makers fix problems and close security holes with it.")
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.status !== null && page.status.errorCode === "firmware-failed"
        severity: "error"
        title: i18nc("@info", "A firmware update couldn't be installed")
        message: i18nc("@info", "Your PC still runs the version it had. Raye Update tries again later.")
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.withUpdate.length > 0
        closable: false
        severity: "informational"
        title: page.wantsCharger ? i18nc("@info", "Plug in the charger") : i18nc("@info", "Your PC restarts to install firmware")
        message: i18nc("@info", "Keep it plugged in and don't turn it off while it updates. If your drive is encrypted, Raye gets it ready first, so it still unlocks on its own afterwards.")
    }

    EmptyState {
        visible: !firmware.supported || (!firmware.checking && firmware.devices.length === 0)
        Layout.topMargin: Theme.s6
        iconName: "org.raye.settings-firmware"
        title: i18nc("@info", "No firmware to update")
        message: firmware.supported
                 ? i18nc("@info", "None of the parts of this PC get firmware updates from Raye.")
                 : i18nc("@info", "The firmware update service isn't installed.")
    }

    ProgressBar {
        visible: firmware.checking && firmware.devices.length === 0
        Layout.fillWidth: true
        Layout.topMargin: Theme.s4
        indeterminate: true
    }

    SectionHeader {
        visible: page.withUpdate.length > 0
        text: i18nc("@title:group", "Available updates")
        first: true
    }

    Repeater {
        model: page.withUpdate
        delegate: Card {
            id: row
            required property var modelData
            readonly property string step: U.firmwareAction(page.status, modelData.deviceId, Date.now())
            Layout.fillWidth: true
            iconName: "org.raye.settings-firmware"
            iconFallback: "cpu"
            title: modelData.name
            description: {
                const parts = [i18nc("@info %1 and %2 are versions", "Version %1 to %2", modelData.version, modelData.to)]
                if (modelData.size > 0)
                    parts.push(page.sizeText(modelData.size))
                if (modelData.summary !== "")
                    parts.push(modelData.summary)
                return parts.join(" · ")
            }

            RowLayout {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                spacing: Theme.s3
                ProgressBar {
                    visible: row.step === "working"
                    Layout.preferredWidth: 120
                    indeterminate: U.downloadProgress(page.status) < 0
                    value: Math.max(0, U.downloadProgress(page.status))
                }
                Text {
                    visible: row.step === "armed"
                    text: i18nc("@info", "Installs when your PC restarts")
                    font: Theme.body
                    color: Theme.textSecondary
                }
                Button {
                    visible: row.step === "download"
                    accent: true
                    enabled: page.status !== null
                    text: i18nc("@action:button", "Download")
                    Accessible.description: row.modelData.name
                    onClicked: page.status.checkNow()
                }
                Button {
                    visible: row.step === "restart"
                    accent: true
                    enabled: page.status !== null && !page.status.restarting
                    text: i18nc("@action:button", "Restart now")
                    Accessible.description: row.modelData.name
                    onClicked: restartDialog.open()
                }
            }
        }
    }

    SectionHeader {
        visible: page.upToDate.length > 0
        text: i18nc("@title:group", "Up to date")
        first: page.withUpdate.length === 0
    }

    Repeater {
        model: page.upToDate
        delegate: Card {
            required property var modelData
            Layout.fillWidth: true
            iconName: "org.raye.settings-firmware"
            iconFallback: "cpu"
            title: modelData.name
            description: modelData.vendor
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@info %1 is a version", "Version %1", modelData.version)
                font: Theme.body
                color: Theme.textSecondary
            }
        }
    }

    ContentDialog {
        id: restartDialog
        title: i18nc("@title:window", "Restart now?")
        text: i18n("Save your work first. Raye installs the firmware while it restarts, which can take a few minutes. Keep your PC plugged in.")
        primaryText: i18nc("@action:button", "Restart now")
        closeText: i18nc("@action:button", "Not now")
        onAccepted: page.status.restartAndInstall()
    }

    ContentDialog {
        id: restartError
        title: i18nc("@title:window", "Couldn't restart")
        closeText: i18nc("@action:button", "Close")
    }
}
