// SPDX-License-Identifier: GPL-3.0-or-later
// Gaming > Windows programs: each Windows program someone has run has its
// own space. Removing one deletes the program and its settings, so it asks.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Gaming"), target: "gaming" },
             { text: i18nc("@title settings page", "Windows programs") }]

    readonly property var sys: typeof system !== "undefined" ? system : null
    readonly property var programs: sys ? sys.windowsApps : []
    property string removing: ""

    EmptyState {
        objectName: "noPrograms"
        Layout.topMargin: Theme.s7
        visible: page.programs.length === 0
        iconName: "org.raye.settings-windows-programs"
        title: i18nc("@title", "No Windows programs yet")
        message: i18nc("@info", "Double-click a .exe or .msi file to open it. Each program gets its own space, listed here.")
    }

    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s2
        visible: page.programs.length > 0
        wrapMode: Text.WordWrap
        font: Theme.body
        color: Theme.textSecondary
        text: i18nc("@info", "Each Windows program you have run has its own space. Removing one deletes that program and its settings.")
    }

    Repeater {
        model: page.programs
        delegate: ButtonCard {
            required property string modelData
            Layout.fillWidth: true
            iconName: "org.raye.settings-windows-programs"
            iconFallback: "application-x-ms-dos-executable"
            title: modelData
            description: i18nc("@info", "Windows program")
            buttonText: i18nc("@action:button", "Remove")
            onButtonClicked: {
                page.removing = modelData
                confirm.open()
            }
        }
    }

    InfoBar {
        id: failure
        Layout.fillWidth: true
        open: false
        severity: "error"
        title: i18nc("@title", "Couldn't remove it")
        message: i18nc("@info", "Close the program and try again.")
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-installed-apps"
        iconFallback: "preferences-system"
        title: i18nc("@title settings page", "Installed apps")
        target: "apps/installed"
    }

    ContentDialog {
        id: confirm
        title: i18nc("@title:window %1 is a program", "Remove %1?", page.removing)
        text: i18n("This deletes the program and its settings. Files you saved in your own folders stay.")
        primaryText: i18nc("@action:button", "Remove")
        onAccepted: failure.open = !page.sys.forgetWindowsApp(page.removing)
    }
}
