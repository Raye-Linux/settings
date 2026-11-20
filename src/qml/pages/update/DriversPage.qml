// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Update › Optional updates, as in Windows: drivers for the
// devices of this PC that Raye doesn't install on its own, with the one
// it recommends and the one in use, and under "Other updates" the
// Raye kernel. The administrator password is asked by the system when
// a driver is installed or removed.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings page", "Raye Update"), target: "update" },
             { text: i18nc("@title settings page", "Optional updates") }]

    readonly property var system_: typeof system !== "undefined" ? system : null
    property string result: ""          // "installed", "removed", "failed"
    property string resultPackage: ""

    DriverUpdates {
        id: drivers
        onFinished: (pkg, removed, ok) => {
            page.resultPackage = pkg
            page.result = !ok ? "failed" : removed ? "removed" : "installed"
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s4
        text: i18nc("@info", "The drivers Raye installs keep your PC working. If a device has a specific problem, one of these may help.")
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.result !== ""
        severity: page.result === "failed" ? "error" : "success"
        title: page.result === "failed" ? i18nc("@info", "That didn't work")
             : page.result === "removed" ? i18nc("@info %1 is a driver", "%1 was removed", page.resultPackage)
             : i18nc("@info %1 is a driver", "%1 was installed", page.resultPackage)
        message: page.result === "failed" ? i18nc("@info", "Check your internet connection and try again.")
                                          : i18nc("@info", "Restart your PC to start using it.")
        onClosed: page.result = ""
    }

    Expander {
        searchId: "driver-updates"
        expanded: true
        iconName: "org.raye.settings-drivers"
        iconFallback: "preferences-system-hardware"
        title: i18nc("@title", "Driver updates")
        description: !drivers.supported ? i18nc("@info", "Raye can't look for drivers on this PC")
                   : drivers.checking && drivers.count === 0 ? i18nc("@info", "Looking for drivers…")
                   : drivers.count === 0 ? i18nc("@info", "No extra drivers for your devices")
                   : i18ncp("@info", "%1 driver for your devices", "%1 drivers for your devices", drivers.count)
        headerContent: Button {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            visible: drivers.supported
            enabled: !drivers.checking && drivers.busyPackage === ""
            text: i18nc("@action:button", "Check again")
            onClicked: drivers.refresh()
        }

        Repeater {
            model: drivers
            delegate: ExpanderRow {
                id: row
                required property var model
                readonly property bool busy: drivers.busyPackage === model.package
                title: model.device
                description: {
                    const parts = [model.package]
                    if (model.recommended)
                        parts.push(i18nc("@info driver", "Recommended"))
                    if (busy)
                        parts.push(drivers.busyRemoving ? i18nc("@info", "Removing…") : i18nc("@info", "Installing…"))
                    else if (model.state === "installed")
                        parts.push(i18nc("@info driver", "Installed"))
                    else if (model.state === "builtin")
                        parts.push(i18nc("@info driver", "Built into Raye"))
                    else if (model.state === "unsigned")
                        parts.push(i18nc("@info driver", "Not available with Secure Boot yet"))
                    else if (model.state === "kernel")
                        parts.push(i18nc("@info driver", "Not available with the Raye kernel yet"))
                    else if (model.state === "other")
                        parts.push(i18nc("@info driver", "Remove the NVIDIA driver in use to choose this one"))
                    return parts.join(" · ")
                }

                RowLayout {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    spacing: Theme.s3
                    ProgressBar {
                        visible: row.busy
                        Layout.preferredWidth: 120
                        indeterminate: drivers.busyPercent <= 0
                        value: drivers.busyPercent / 100
                    }
                    Button {
                        visible: !row.busy && row.model.state === "installable"
                        accent: row.model.recommended
                        enabled: drivers.busyPackage === "" && !drivers.checking
                        text: i18nc("@action:button", "Install")
                        Accessible.description: row.model.package
                        onClicked: drivers.install(row.model.package)
                    }
                    Button {
                        visible: !row.busy && row.model.state === "installed" && !row.model.builtin
                        enabled: drivers.busyPackage === "" && !drivers.checking
                        text: i18nc("@action:button", "Remove")
                        Accessible.description: row.model.package
                        onClicked: {
                            removeDialog.packageName = row.model.package
                            removeDialog.open()
                        }
                    }
                }
            }
        }
    }

    Expander {
        searchId: "other-updates"
        visible: page.system_ !== null && page.system_.rayeKernel !== "unavailable"
        iconName: "org.raye.settings-update-advanced"
        iconFallback: "preferences-system"
        title: i18nc("@title", "Other updates")
        description: i18nc("@info", "The Raye kernel")

        ExpanderRow {
            readonly property string kernel: page.system_ ? page.system_.rayeKernel : ""
            readonly property bool encrypted: page.system_ ? page.system_.encrypted : false
            title: i18n("Raye kernel")
            description: page.system_ && page.system_.kernelBusy ? i18n("Working…")
                       : kernel === "running" ? i18n("In use. The standard kernel stays installed as a fallback.")
                       : kernel === "enrolled" ? i18n("Ready. Restart to start using it.")
                       : kernel === "pending" ? i18n("Restart to finish. A blue screen asks to enroll the Raye key: press a key, choose Enroll MOK, Continue, Yes, then type the code below.")
                       : kernel === "trial" ? (encrypted
                             ? i18n("Installed. Restart to try it: this one time the disk asks for its password, then it opens by itself again.")
                             : i18n("Installed. Restart to try it. If it does not start, the standard kernel comes back by itself."))
                       : kernel === "failed" ? i18n("The Raye kernel did not start on this computer, so the standard kernel stayed in use. Nothing was lost.")
                       : kernel === "nvidia" ? i18n("Not available yet with NVIDIA graphics: the NVIDIA driver only works with the standard kernel for now.")
                       : i18n("Optional. The same kernel with the Raye crash screen. Using it with Secure Boot takes one restart to enroll the Raye key.")
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                readonly property string kernel: page.system_ ? page.system_.rayeKernel : ""
                visible: page.system_ !== null && !page.system_.kernelBusy && kernel !== "pending" && kernel !== "nvidia"
                text: kernel === "off" ? i18n("Use the Raye kernel")
                    : kernel === "failed" ? i18n("Try again")
                    : i18n("Go back to the standard kernel")
                onClicked: kernel === "off" || kernel === "failed" ? page.system_.enableRayeKernel()
                                                                    : page.system_.disableRayeKernel()
            }
        }

        // the one-time code MokManager asks for, big enough to read from
        // across the room
        Text {
            visible: page.system_ !== null && page.system_.enrollCode.length > 0 && page.system_.rayeKernel === "pending"
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Theme.s4
            Layout.bottomMargin: Theme.s4
            text: page.system_ ? page.system_.enrollCode.split("").join(" ") : ""
            font.family: "Cascadia Mono"
            font.pixelSize: 32
            color: Theme.text
        }
    }

    ContentDialog {
        id: removeDialog
        property string packageName
        title: i18nc("@title:window %1 is a driver", "Remove %1?", packageName)
        text: i18nc("@info", "Your device goes back to the driver built into Raye after a restart. You can install this one again from here.")
        primaryText: i18nc("@action:button", "Remove")
        onAccepted: drivers.remove(packageName)
    }
}
