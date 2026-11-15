// SPDX-License-Identifier: GPL-3.0-or-later
// Privacy & security > Virus & threat protection: what keeps this PC
// safe, each with how it stands now and a way to it. Raye has no
// scanner running in the background; these are the protections it has.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import "../update/updatetext.js" as U

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Privacy & security"), target: "privacy" },
             { text: i18nc("@title settings page", "Virus & threat protection") }]

    Firewall { id: firewall }
    readonly property var status: typeof updates !== "undefined" ? updates : null
    readonly property string updateState: U.pageState(page.status, Date.now())
    readonly property bool updatesOk: page.updateState === "uptodate"
    readonly property bool updating: page.updateState === "checking" || page.updateState === "downloading"
                                     || page.updateState === "installing"
    readonly property bool firewallOk: firewall.installed && firewall.enabled

    Component.onCompleted: firewall.refresh()

    SectionHeader {
        text: i18nc("@title:group", "Protection areas")
        first: true
    }

    NavCard {
        Layout.fillWidth: true
        searchId: "virus-firewall"
        iconName: "org.raye.settings-device-security"
        iconFallback: "security-high"
        title: i18nc("@label", "Firewall")
        description: !firewall.installed ? i18nc("@info", "No firewall is installed")
                   : firewall.enabled ? i18nc("@info", "Connections other computers start to this PC are blocked")
                   : i18nc("@info", "Other computers on the network can connect to this PC")
        target: "privacy/device-security#firewall"
        SecurityStatus {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            state: page.firewallOk ? "success" : "caution"
            text: page.firewallOk ? i18nc("@label a setting that is on", "On")
                                  : i18nc("@label a setting that is off", "Off")
        }
    }

    NavCard {
        Layout.fillWidth: true
        searchId: "virus-updates"
        iconName: "org.raye.settings-update"
        iconFallback: "system-software-update"
        title: i18nc("@label", "Security updates")
        description: page.updatesOk || page.updating
                     ? i18nc("@info", "Raye Update installs security fixes as soon as they come out")
                     : i18nc("@info", "Some updates are waiting. Install them to stay protected.")
        target: "update"
        SecurityStatus {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            state: page.updatesOk ? "success" : page.updating ? "info" : "caution"
            text: page.updatesOk ? i18nc("@info", "Up to date")
                : page.updating ? i18nc("@info", "Updating")
                : i18nc("@info", "Action needed")
        }
    }

    NavCard {
        Layout.fillWidth: true
        searchId: "virus-apps"
        iconName: "org.raye.settings-privacy-files"
        iconFallback: "preferences-security"
        title: i18nc("@label", "Apps from Raye Store")
        description: i18nc("@info", "Each one runs apart from the system and only reaches the files and devices you allow")
        target: "privacy/files"
    }

    NavCard {
        Layout.fillWidth: true
        searchId: "virus-windows"
        iconName: "org.raye.settings-windows-programs"
        iconFallback: "application-x-ms-dos-executable"
        title: i18nc("@label", "Windows programs")
        description: i18nc("@info", "Each one gets its own space and can't change Raye's system files")
        target: "gaming/windows-programs"
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s3
        text: i18nc("@info", "Raye doesn't run a virus scanner in the background. Only open files and programs you trust, and keep your PC up to date.")
        font: Theme.caption
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }
}
