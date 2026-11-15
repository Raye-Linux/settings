// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts › Your devices: every PC signed in to the Raye account, with
// its model, Raye version, when it was last seen and how many apps it
// has. Removing a PC signs it out of the account.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import org.raye.account

PageScaffold {
    id: page

    property string failure: ""
    property var pending: null

    crumbs: [{ text: i18nc("@title settings category", "Accounts"), target: "accounts" },
             { text: i18nc("@title settings page", "Raye account"), target: "accounts/raye-account" },
             { text: i18nc("@title settings page", "Your devices") }]

    Component.onCompleted: RayeAccount.loadDevices()

    Connections {
        target: RayeAccount
        function onActionFailed(action, code) { page.failure = code }
    }

    function seenText(iso) {
        const minutes = Math.round((Date.now() - new Date(iso).getTime()) / 60000)
        if (minutes < 2)
            return i18nc("@info last seen", "Active now")
        if (minutes < 60)
            return i18ncp("@info last seen", "Last seen %1 minute ago", "Last seen %1 minutes ago", minutes)
        if (minutes < 48 * 60)
            return i18ncp("@info last seen", "Last seen %1 hour ago", "Last seen %1 hours ago", Math.round(minutes / 60))
        return i18nc("@info %1 is a date", "Last seen %1", new Date(iso).toLocaleDateString(Qt.locale(), Locale.ShortFormat))
    }

    function details(d) {
        const parts = [d.model, i18nc("@info %1 is a version", "Raye %1", d.raye_version), seenText(d.last_seen_at)]
        if (d.app_count > 0)
            parts.push(i18ncp("@info", "%1 app", "%1 apps", d.app_count))
        return parts.join(" · ")
    }

    AccountErrorBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        code: page.failure
        onClosed: page.failure = ""
    }

    Repeater {
        // this PC first, then the most recently seen
        model: RayeAccount.devices.slice().sort((a, b) => (b.this_device - a.this_device)
                                                || b.last_seen_at.localeCompare(a.last_seen_at))
        delegate: Card {
            required property var modelData
            Layout.fillWidth: true
            iconName: "computer"
            title: modelData.this_device
                ? i18nc("@label %1 is the PC's name", "%1 (this PC)", modelData.name) : modelData.name
            description: page.details(modelData)

            Button {
                text: i18nc("@action:button", "Remove")
                onClicked: {
                    page.pending = modelData
                    removeDialog.open()
                }
            }
        }
    }

    EmptyState {
        Layout.fillWidth: true
        visible: !RayeAccount.devicesLoading && RayeAccount.devices.length === 0 && page.failure === ""
        iconName: "computer"
        title: i18nc("@info", "No devices")
        message: i18nc("@info", "Sign in on a PC and it shows up here.")
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s4
        visible: RayeAccount.devices.length > 0
        text: i18nc("@info", "A PC you remove is signed out the next time it goes online. Its settings stay on it.")
        font: Theme.caption
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    ContentDialog {
        id: removeDialog
        title: page.pending && page.pending.this_device
            ? i18nc("@title:window", "Sign out this PC?")
            : i18nc("@title:window %1 is a PC's name", "Remove %1?", page.pending ? page.pending.name : "")
        text: page.pending && page.pending.this_device
            ? i18nc("@info", "This PC stops syncing with your Raye account. Its settings stay here.")
            : i18nc("@info", "It's signed out of your Raye account and stops syncing.")
        primaryText: i18nc("@action:button", "Remove")
        onAccepted: if (page.pending) RayeAccount.removeDevice(page.pending.id)
    }
}
