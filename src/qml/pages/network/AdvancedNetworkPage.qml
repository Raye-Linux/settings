// SPDX-License-Identifier: GPL-3.0-or-later
// Network & internet › Advanced network settings: every adapter with
// Disable / Enable, KDE's window for what Raye does not draw, and the
// network reset.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import "networktext.js" as Net

PageScaffold {
    id: page

    // "", "done", "failed"
    property string resetResult: ""

    crumbs: [{ text: i18nc("@title settings category", "Network & internet"), target: "network" },
             { text: i18nc("@title settings page", "Advanced network settings") }]

    NetworkStatus {
        id: netStatus
        onResetFinished: (ok) => page.resetResult = ok ? "done" : "failed"
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.resetResult !== ""
        severity: page.resetResult === "done" ? "success" : "error"
        title: page.resetResult === "done" ? i18nc("@info", "Your network settings were reset")
                                           : i18nc("@info", "Your network settings weren't reset")
        message: page.resetResult === "done" ? netStatus.wording.resetDone
                                             : i18nc("@info", "This needs root access. Nothing was changed.")
        onClosed: page.resetResult = ""
    }

    SectionHeader { first: true; text: i18nc("@title:group", "Network adapters") }

    Repeater {
        model: netStatus.adapters
        Expander {
            id: adapter
            required property var modelData
            Layout.fillWidth: true
            iconName: modelData.kind === "wifi" ? "org.raye.settings-wifi" : modelData.kind === "ethernet" ? "org.raye.settings-ethernet" : "network-mobile"
            iconFallback: modelData.kind === "wifi" ? "network-wireless" : "network-wired"
            title: modelData.description
            description: Net.styled(modelData.statusText)
            headerContent: [
                Button {
                    text: adapter.modelData.enabled ? i18nc("@action:button network adapter", "Disable")
                                                    : i18nc("@action:button network adapter", "Enable")
                    Accessible.description: adapter.modelData.description + " " + adapter.modelData.interface
                    onClicked: netStatus.setAdapterEnabled(adapter.modelData.uni, !adapter.modelData.enabled)
                }
            ]
            Item {
                Layout.fillWidth: true
                implicitHeight: list.implicitHeight
                NetworkPropertyList {
                    id: list
                    width: parent.width
                    color: "transparent"
                    border.width: 0
                    entries: {
                        const m = adapter.modelData
                        const out = [{ label: i18nc("@label", "Adapter"), value: m.interface }]
                        if (m.speed) out.push({ label: i18nc("@label", "Link speed"), value: m.speed })
                        if (m.driver) out.push({ label: i18nc("@label", "Driver"), value: m.driver })
                        if (m.mac) out.push({ label: i18nc("@label", "Physical address (MAC)"), value: m.mac })
                        return out
                    }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        visible: netStatus.adapters.length === 0
        text: i18nc("@info", "No network adapters found.")
        font: Theme.body
        color: Theme.textSecondary
    }

    SectionHeader { text: i18nc("@title:group", "More settings") }

    Card {
        Layout.fillWidth: true
        searchId: "more-adapter-options"
        iconName: "org.raye.settings-network-advanced"
        iconFallback: "preferences-system-network"
        title: i18nc("@label", "More network adapter options")
        description: i18nc("@info", "Bridges, VLANs, 802.1X and every setting of a connection, in a separate window")
        clickable: true
        actionGlyph: "open-external"
        onClicked: netStatus.openMoreOptions("")
    }

    ButtonCard {
        Layout.fillWidth: true
        searchId: "network-reset"
        iconName: "org.raye.settings-recovery"
        iconFallback: "view-refresh"
        title: i18nc("@label", "Network reset")
        description: netStatus.resetting ? i18nc("@info", "Resetting…")
                                       : netStatus.wording.resetSummary
        buttonText: i18nc("@action:button", "Reset now")
        enabled: !netStatus.resetting
        onButtonClicked: resetDialog.open()
    }

    NetworkResetDialog {
        id: resetDialog
        warning: netStatus.wording.resetWarning
        onAccepted: {
            page.resetResult = ""
            netStatus.resetNetwork()
        }
    }
}
