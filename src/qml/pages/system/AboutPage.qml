// SPDX-License-Identifier: GPL-3.0-or-later
// System › About: the PC with its name and "Rename this PC", the device
// specifications and the Raye Linux ones, each with Copy, then the
// related links (Hardware details, Performance options, Device
// security, the licence).
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "About") }]

    DeviceInfo { id: info }
    Hostname {
        id: pcName
        onRenamed: (ok, error) => {
            page.renameResult = ok ? "done" : error
            resultTimer.restart()
        }
    }

    // "" at rest, "done", "denied" or "failed" for a moment after a rename
    property string renameResult: ""
    Timer { id: resultTimer; interval: 6000; onTriggered: page.renameResult = "" }

    banner: Component {
        AboutBanner {
            device: info
            name: pcName.name
            busy: pcName.busy
            onRenameClicked: renameDialog.start()
        }
    }

    InfoBar {
        Layout.fillWidth: true
        open: page.renameResult !== ""
        severity: page.renameResult === "done" ? "success" : "error"
        title: page.renameResult === "done" ? i18nc("@info", "Your PC has a new name")
                                            : i18nc("@info", "Your PC wasn't renamed")
        message: page.renameResult === "done" ? i18nc("@info %1 the new name", "It's now called %1. Other PCs on the network may show the old name for a little while.", pcName.name)
               : page.renameResult === "denied" ? i18nc("@info", "Renaming the PC needs root access.")
               : i18nc("@info", "Something went wrong. Try again.")
        onClosed: page.renameResult = ""
    }

    // --- Device specifications -------------------------------------------
    Expander {
        id: specs
        searchId: "device-specifications"
        iconName: "org.raye.settings-about"
        iconFallback: "computer"
        title: i18nc("@title", "Device specifications")
        description: info.model !== "" ? info.model : ""
        expanded: true
        headerContent: Button {
            text: copiedSpecs.running ? i18nc("@action:button the text is on the clipboard", "Copied")
                                      : i18nc("@action:button", "Copy")
            enabled: info.ready
            Accessible.description: i18nc("@info", "Copy the device specifications")
            onClicked: {
                info.copyToClipboard(info.specificationsText(pcName.name))
                copiedSpecs.restart()
            }
            Timer { id: copiedSpecs; interval: 2000 }
        }

        AboutSpecRow { label: i18nc("@label device specification", "Device name"); value: pcName.name }
        AboutSpecRow { label: i18nc("@label device specification", "Processor"); value: info.processor
                       detail: info.cores > 0 ? i18ncp("@info processor cores and threads", "%1 core", "%1 cores", info.cores)
                                                  + (info.threads > info.cores ? ", " + i18ncp("@info", "%1 thread", "%1 threads", info.threads) : "")
                                                : "" }
        AboutSpecRow { label: i18nc("@label device specification", "Installed RAM"); value: info.memory }
        AboutSpecRow { label: i18nc("@label device specification", "Graphics"); value: info.graphics.join("\n") }
        AboutSpecRow { label: i18nc("@label device specification", "Storage"); value: info.disks.join("\n") }
        AboutSpecRow { label: i18nc("@label device specification", "Device ID"); value: info.deviceId }
        AboutSpecRow { label: i18nc("@label device specification", "System type"); value: info.systemType }
        AboutSpecRow { label: i18nc("@label device specification", "Pen and touch"); value: info.ready ? info.penAndTouch : "" }
        AboutSpecRow { label: i18nc("@label device specification", "Device model"); value: info.model; last: true }
    }

    // --- Raye Linux specifications ----------------------------------------
    Expander {
        searchId: "raye-specifications"
        iconName: "org.raye.settings"
        iconFallback: "raye-eye"
        title: i18nc("@title", "Raye Linux specifications")
        expanded: true
        headerContent: Button {
            text: copiedRaye.running ? i18nc("@action:button the text is on the clipboard", "Copied")
                                     : i18nc("@action:button", "Copy")
            enabled: info.ready
            Accessible.description: i18nc("@info", "Copy the Raye Linux specifications")
            onClicked: {
                info.copyToClipboard(info.rayeSpecificationsText())
                copiedRaye.restart()
            }
            Timer { id: copiedRaye; interval: 2000 }
        }

        AboutSpecRow { label: i18nc("@label Raye specification", "Edition"); value: info.edition }
        AboutSpecRow { label: i18nc("@label Raye specification", "Version"); value: info.version }
        AboutSpecRow {
            label: i18nc("@label Raye specification", "Installed on")
            value: !isNaN(info.installedOn) && info.installedOn.getFullYear() > 1971
                   ? info.installedOn.toLocaleDateString(Qt.locale(), Locale.LongFormat) : ""
        }
        AboutSpecRow {
            label: i18nc("@label Raye specification", "Kernel")
            value: info.ready ? (info.kernelKind === "raye" ? i18nc("@info kernel kind", "Raye")
                                                                : i18nc("@info kernel kind", "Standard")) : ""
            detail: info.kernelRelease
            last: true
        }
    }

    SectionHeader { text: i18nc("@title", "Related") }

    NavCard {
        searchId: "hardware-details"
        iconName: "org.raye.settings-hardware"
        iconFallback: "preferences-system-hardware"
        title: i18nc("@title settings page", "Hardware details")
        description: i18nc("@info", "Processor, memory, graphics, disks and devices in depth")
        target: "system/hardware-details"
    }
    NavCard {
        iconName: "org.raye.settings-performance"
        iconFallback: "speedometer"
        title: i18nc("@title settings page", "Performance options")
        description: i18nc("@info", "Visual effects, or the best performance this PC can give")
        target: "system/performance"
    }
    NavCard {
        iconName: "org.raye.settings-device-security"
        iconFallback: "security-high"
        title: i18nc("@title settings page", "Device security")
        description: i18nc("@info", "Secure Boot, disk encryption and the security chip")
        target: "privacy/device-security"
    }
    Card {
        searchId: "licence"
        iconName: "org.raye.settings"
        iconFallback: "raye-eye"
        title: i18nc("@title", "Raye Linux is free software")
        description: i18nc("@info", "No activation, no license key. You can use it, share it and change it.")
        clickable: true
        actionGlyph: "chevron-right"
        onClicked: licence.open()
    }

    AboutRenameDialog {
        id: renameDialog
        objectName: "renameDialog"
        hostname: pcName
    }

    ContentDialog {
        id: licence
        objectName: "licence"
        title: i18nc("@title:window", "Raye Linux is free software")
        closeText: i18nc("@action:button", "Close")
        Repeater {
            model: [
                i18nc("@info license", "Raye Linux is free. It needs no activation and no product key, on as many PCs as you like."),
                i18nc("@info license", "It is made of free and open source software, based on Ubuntu. Each program keeps its own license, most of them the GNU General Public License: you may use, study, share and change them."),
                i18nc("@info license", "The license texts are in the folder /usr/share/common-licenses and next to each program in /usr/share/doc."),
            ]
            delegate: Text {
                required property string modelData
                Layout.fillWidth: true
                Layout.preferredWidth: 480
                Layout.bottomMargin: Theme.s3
                font: Theme.body
                color: Theme.text
                wrapMode: Text.Wrap
                text: modelData
            }
        }
    }
}
