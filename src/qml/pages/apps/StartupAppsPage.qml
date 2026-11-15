// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Startup: the apps that start when you sign in, each with its
// switch; sort by name or by status; "Add an app" picks an installed
// app to start too. Scripts and services that start are in "More
// startup options".
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Apps"), target: "apps" },
             { text: i18nc("@title settings page", "Startup") }]

    StartupApps { id: startup }

    property bool writeFailed: false

    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s4
        text: i18nc("@info", "Apps can be set to start when you sign in. Apps that start are often already there when you need them, but they can make signing in slower.")
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.writeFailed
        severity: "error"
        title: i18nc("@info", "That change could not be saved")
        message: i18nc("@info", "Check that your settings folder is not full or read-only.")
        onClosed: page.writeFailed = false
    }

    ComboCard {
        Layout.fillWidth: true
        title: i18nc("@label:listbox", "Sort by")
        searchId: "sort-startup"
        textRole: "text"
        valueRole: "value"
        model: [{ text: i18nc("@item:inlistbox sort startup apps", "Name"), value: "name" },
                { text: i18nc("@item:inlistbox sort startup apps", "Status"), value: "status" }]
        onActivated: startup.sortOrder = currentValue
    }

    ButtonCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-startup"
        iconFallback: "list-add"
        title: i18nc("@label", "Add an app")
        description: i18nc("@info", "Choose an app to start when you sign in")
        buttonText: i18nc("@action:button", "Browse apps")
        searchId: "add-startup-app"
        onButtonClicked: addDialog.open()
    }

    SectionHeader {
        property string searchId: "startup-apps"
        text: i18nc("@title:group", "Startup apps")
    }

    EmptyState {
        Layout.fillWidth: true
        visible: startup.count === 0
        iconName: "org.raye.settings-startup"
        title: i18nc("@info", "No apps start when you sign in")
    }

    Repeater {
        model: startup
        delegate: ToggleCard {
            required property var model
            Layout.fillWidth: true
            iconName: model.iconName
            iconFallback: "application-x-executable"
            title: model.name
            description: (model.comment !== "" ? model.comment + " · " : "")
                         + (model.starts ? i18nc("@info", "Starts when you sign in") : i18nc("@info", "Doesn't start"))
            checked: model.starts
            onToggled: {
                if (!startup.setEnabled(model.fileName, checked))
                    page.writeFailed = true
                checked = Qt.binding(() => model.starts)
            }
        }
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    LinkCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-startup"
        iconFallback: "system-run"
        title: i18nc("@label", "More startup options")
        description: i18nc("@info", "Scripts and services that start when you sign in")
        searchId: "more-startup-options"
        enabled: typeof moduleHost !== "undefined"
        onClicked: moduleHost.openExternal("kcm_autostart")
    }

    StartupAddDialog {
        id: addDialog
        onPicked: (desktopFile) => {
            if (!startup.addApp(desktopFile))
                page.writeFailed = true
        }
    }
}
