// SPDX-License-Identifier: GPL-3.0-or-later
// One app permission (camera, microphone, location, screenshots,
// background, files), the way Windows draws each of them: "Let apps use
// your camera" on top, then one switch per Store app that asked for it.
// Apps installed outside the Store run without a sandbox: they are not
// listed, and the page says so.
//
//   AppPermissionPage { kind: "camera"; pageTitle: i18n("Camera"); ... }
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    // what AppPermissions knows: camera, microphone, location, screenshot,
    // background, files
    property alias kind: permissions.kind
    property string pageTitle
    property string masterTitle
    property string masterDescription
    property string listTitle
    property string emptyTitle
    property string emptyMessage
    property string iconName
    property string iconFallback
    // a line under the list (files: when a change applies)
    property string footnote
    // the anchor of "Let apps…", the row search results land on
    property string searchId

    crumbs: [{ text: i18nc("@title settings category", "Privacy & security"), target: "privacy" },
             { text: page.pageTitle }]

    // the store answered once: until then nothing is known to be wrong
    property bool loadedOnce: false

    AppPermissions {
        id: permissions
        onLoaded: page.loadedOnce = true
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: open ? Theme.s3 : 0
        open: page.loadedOnce && !permissions.available
        closable: false
        severity: "warning"
        title: i18nc("@info", "The list of apps isn't available right now")
        message: i18nc("@info", "Sign out and back in, then open this page again.")
    }

    ToggleCard {
        Layout.fillWidth: true
        searchId: page.searchId
        iconName: page.iconName
        iconFallback: page.iconFallback
        title: page.masterTitle
        description: page.masterDescription
        checked: permissions.masterEnabled
        onToggled: {
            permissions.masterEnabled = checked
            checked = Qt.binding(() => permissions.masterEnabled)
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s3
        text: i18nc("@info", "This applies to apps from Raye Store. Apps you installed outside the Store are not limited.")
        font: Theme.caption
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignLeft
    }

    SectionHeader {
        text: page.listTitle
        visible: permissions.count > 0
    }

    Repeater {
        model: permissions
        delegate: ToggleCard {
            id: appCard
            required property string appId
            required property string name
            required property string appIcon
            required property bool allowed

            Layout.fillWidth: true
            iconName: appCard.appIcon
            iconFallback: "application-x-executable"
            title: appCard.name
            enabled: permissions.masterEnabled
            checked: appCard.allowed
            onToggled: {
                permissions.setAllowed(appCard.appId, checked)
                checked = Qt.binding(() => appCard.allowed)
            }
        }
    }

    EmptyState {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s8
        visible: page.loadedOnce && permissions.available && permissions.count === 0
        iconName: page.iconName
        title: page.emptyTitle
        message: page.emptyMessage
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s3
        visible: page.footnote !== "" && permissions.count > 0
        text: page.footnote
        font: Theme.caption
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignLeft
    }

    SectionHeader {
        text: i18nc("@title:group", "Related settings")
        visible: permissions.canOpenAppSettings()
    }
    LinkCard {
        Layout.fillWidth: true
        visible: permissions.canOpenAppSettings()
        iconName: "configure"
        title: i18nc("@label", "All permissions of an app")
        description: i18nc("@info", "Files, devices and network an app from Raye Store may use")
        onClicked: permissions.openAppSettings()
    }
}
