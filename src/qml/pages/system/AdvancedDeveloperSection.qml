// SPDX-License-Identifier: GPL-3.0-or-later
// System > Advanced > For developers: Developer Mode, Device Portal (a
// page in a browser on the same network to manage this PC), device
// discovery, debug messages of Windows programs and the OpenSSH server.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ColumnLayout {
    id: section

    Layout.fillWidth: true
    spacing: Theme.cardSpacing

    DeveloperSettings { id: dev }
    WindowsDebug { id: windowsDebug }

    // the portal can be turned off from a terminal, and its log grows on its own
    Timer {
        interval: 5000
        repeat: true
        running: section.visible && dev.developerMode
        onTriggered: dev.refresh()
    }

    function copy(value) {
        clipboard.text = value
        clipboard.selectAll()
        clipboard.copy()
        clipboard.text = ""
    }
    TextEdit {
        id: clipboard
        visible: false
        textFormat: TextEdit.PlainText
    }

    SectionHeader { text: i18nc("@title", "For developers") }

    InfoBar {
        implicitWidth: Theme.contentMaxWidth
        severity: "error"
        closable: false
        open: dev.error !== ""
        title: i18nc("@title", "Something went wrong")
        message: dev.error
    }

    ToggleCard {
        iconName: "org.raye.settings-advanced"
        iconFallback: "applications-development"
        title: i18nc("@title", "Developer Mode")
        description: i18nc("@info", "Use Device Portal, and get detailed logs from Raye apps from your next sign-in")
        searchId: "developer-mode"
        checked: dev.developerMode
        onToggled: dev.developerMode = checked
    }

    Expander {
        id: portal
        iconName: "org.raye.settings-advanced"
        iconFallback: "internet-web-browser"
        title: i18nc("@title", "Device Portal")
        description: !dev.developerMode ? i18nc("@info", "Turn on Developer Mode to use it")
                   : i18nc("@info", "Manage this PC from a browser on the same network")
        searchId: "device-portal"
        enabled: dev.developerMode
        headerContent: [
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: dev.portalEnabled ? i18nc("@label a setting that is on", "On")
                                        : i18nc("@label a setting that is off", "Off")
                font: Theme.body
                color: portal.enabled ? Theme.text : Theme.textDisabled
                Accessible.ignored: true
            },
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@title", "Device Portal")
                enabled: dev.developerMode && !dev.portalBusy
                checked: dev.portalEnabled
                onToggled: {
                    dev.setPortalEnabled(checked)
                    if (checked)
                        portal.expanded = true
                }
            }
        ]

        ExpanderRow {
            title: i18nc("@label", "Address")
            description: dev.portalEnabled ? dev.portalAddresses.join("\n")
                                           : i18nc("@info", "Shown once Device Portal is on")
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                visible: dev.portalEnabled && dev.portalAddresses.length > 0
                text: i18nc("@action:button", "Copy")
                onClicked: section.copy(dev.portalAddresses[0])
            }
        }
        ExpanderRow {
            title: i18nc("@label", "Certificate fingerprint")
            description: dev.portalFingerprint !== ""
                         ? i18nc("@info", "Your browser warns about this page the first time. Go on only if it shows this fingerprint (SHA-256):") + "\n" + dev.portalFingerprint
                         : i18nc("@info", "Made when Device Portal is turned on")
        }
        ExpanderRow {
            title: i18nc("@label", "User name and password")
            description: dev.portalUser !== "" ? i18nc("@info", "Sign in as %1", dev.portalUser)
                                               : i18nc("@info", "Not set yet. Set one, or pair a browser with a code.")
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: dev.portalUser !== "" ? i18nc("@action:button", "Change") : i18nc("@action:button", "Set")
                onClicked: loginDialog.open()
            }
        }
        ExpanderRow {
            title: i18nc("@label", "Pair a device")
            description: i18nc("@info", "Type a one-time code in the browser instead of a password. The browser stays paired for 30 days.")
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                enabled: dev.portalEnabled
                text: i18nc("@action:button", "Pair")
                onClicked: {
                    dev.newPairCode()
                    pairDialog.open()
                }
            }
        }
        ExpanderRow {
            title: i18nc("@label", "Paired browsers")
            description: dev.pairedDevices === 0 ? i18nc("@info", "None")
                       : i18ncp("@info", "%1 browser", "%1 browsers", dev.pairedDevices)
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                enabled: dev.pairedDevices > 0
                text: i18nc("@action:button", "Forget all")
                onClicked: dev.forgetPairedDevices()
            }
        }
        ExpanderRow {
            title: i18nc("@label", "Allow screenshots")
            description: i18nc("@info", "Let the portal take a picture of this screen. A notification shows each time.")
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: dev.allowScreenshot ? i18nc("@label a setting that is on", "On")
                                          : i18nc("@label a setting that is off", "Off")
                font: Theme.body
                color: Theme.text
                Accessible.ignored: true
            }
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@label", "Allow screenshots")
                checked: dev.allowScreenshot
                onToggled: dev.allowScreenshot = checked
            }
        }
        ExpanderRow {
            title: i18nc("@label", "Connection log")
            description: dev.portalLog.length > 0 ? DevicePortalLog.line(dev.portalLog[0])
                                                  : i18nc("@info", "No connections yet")
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                enabled: dev.portalLog.length > 0
                text: i18nc("@action:button", "View")
                onClicked: logDialog.open()
            }
        }
    }

    ToggleCard {
        iconName: "org.raye.settings-advanced"
        iconFallback: "network-workgroup"
        title: i18nc("@title", "Device discovery")
        description: dev.discoveryAvailable ? i18nc("@info", "Let devices on the network find Device Portal by this PC's name")
                   : i18nc("@info", "Needs the avahi-utils package")
        searchId: "device-discovery"
        enabled: dev.developerMode && dev.discoveryAvailable
        checked: dev.discovery && dev.discoveryAvailable
        onToggled: dev.discovery = checked
    }

    ToggleCard {
        iconName: "org.raye.settings-advanced"
        iconFallback: "run-build"
        title: i18nc("@title", "Debug messages from Windows programs")
        description: i18nc("@info", "Show what Windows programs report when you run them from a terminal. Takes effect the next time a program opens.")
        searchId: "windows-debug"
        checked: windowsDebug.on
        onToggled: {
            windowsDebug.on = checked
            checked = Qt.binding(() => windowsDebug.on)
        }
    }

    Expander {
        iconName: "org.raye.settings-advanced"
        iconFallback: "utilities-terminal"
        title: i18nc("@title", "OpenSSH server")
        description: !dev.sshInstalled ? i18nc("@info", "Sign in to this PC from another computer with ssh. Not installed.")
                   : i18nc("@info", "Sign in to this PC from another computer with ssh")
        searchId: "ssh-server"
        headerContent: [
            ProgressBar {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                visible: dev.sshBusy && !dev.sshInstalled
                width: 120
                indeterminate: dev.sshInstallPercent < 0
                value: dev.sshInstallPercent / 100
            },
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                visible: !dev.sshInstalled && !dev.sshBusy
                text: i18nc("@action:button", "Install")
                onClicked: dev.installSsh()
            },
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                visible: dev.sshInstalled
                text: dev.sshRunning ? i18nc("@label a setting that is on", "On")
                                     : i18nc("@label a setting that is off", "Off")
                font: Theme.body
                color: Theme.text
                Accessible.ignored: true
            },
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                visible: dev.sshInstalled
                text: i18nc("@title", "OpenSSH server")
                enabled: !dev.sshBusy
                checked: dev.sshRunning
                onToggled: dev.setSshEnabled(checked)
            }
        ]

        ExpanderRow {
            title: i18nc("@label", "Sign in with a key only")
            description: i18nc("@info", "Passwords are refused. Put your public key in ~/.ssh/authorized_keys first.")
            enabled: dev.sshInstalled
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@label", "Sign in with a key only")
                enabled: !dev.sshBusy && !dev.sshKeysOnlyFixed
                checked: dev.sshKeysOnly
                onToggled: dev.setSshKeysOnly(checked)
            }
        }
        ExpanderRow {
            title: i18nc("@label", "Connect from another computer")
            description: dev.sshCommand
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@action:button", "Copy")
                onClicked: section.copy(dev.sshCommand)
            }
        }
    }

    DevicePortalLoginDialog {
        id: loginDialog
        settings: dev
    }

    ContentDialog {
        id: pairDialog
        title: i18nc("@title:window", "Pair a device")
        text: i18nc("@info", "On the other device, open the portal's address, select \"Have a pairing code?\" and type this code. It works once, for 10 minutes.")
        primaryText: ""
        closeText: i18nc("@action:button", "Done")
        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s3
            text: dev.pairCode.length === 6 ? dev.pairCode.slice(0, 3) + " " + dev.pairCode.slice(3) : "…"
            font: Theme.titleLarge
            color: Theme.text
            horizontalAlignment: Text.AlignHCenter
            Accessible.name: dev.pairCode
        }
    }

    ContentDialog {
        id: logDialog
        title: i18nc("@title:window", "Connection log")
        primaryText: ""
        closeText: i18nc("@action:button", "Close")
        Repeater {
            model: dev.portalLog.slice(0, 12)
            delegate: Text {
                required property var modelData
                Layout.fillWidth: true
                text: DevicePortalLog.line(modelData)
                textFormat: Text.PlainText
                font: Theme.body
                color: Theme.text
                wrapMode: Text.Wrap
            }
        }
    }
}
