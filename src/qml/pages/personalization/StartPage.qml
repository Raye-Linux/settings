// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Start: how the menu shares its room between pins and
// recommended items, what it recommends, and the folders next to the power
// button. The menu follows every change at once.
//
// A row for a key the menu does not read yet is not shown: see
// StartSettings.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
             { text: i18nc("@title settings page", "Start") }]

    StartSettings { id: start }

    SectionHeader {
        property string searchId: "start-layout"
        text: i18nc("@title:group", "Layout")
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s2
        RadioCard {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            title: i18nc("@item Start layout", "More pins")
            description: i18nc("@info Start layout", "Room for more apps")
            checked: start.layout === "pins"
            onClicked: start.layout = "pins"
            StartLayoutPreview { pinRows: 4; recommendedRows: 1 }
        }
        RadioCard {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            title: i18nc("@item Start layout", "Default")
            description: i18nc("@info Start layout", "Pins and recommendations")
            checked: start.layout === "default"
            onClicked: start.layout = "default"
            StartLayoutPreview { pinRows: 3; recommendedRows: 2 }
        }
        RadioCard {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            title: i18nc("@item Start layout", "More recommendations")
            description: i18nc("@info Start layout", "Room for more recent items")
            checked: start.layout === "recommendations"
            onClicked: start.layout = "recommendations"
            StartLayoutPreview { pinRows: 2; recommendedRows: 3 }
        }
    }

    SectionHeader { text: i18nc("@title:group", "What Start shows") }
    ToggleCard {
        Layout.fillWidth: true
        searchId: "start-recommended"
        iconName: "org.raye.settings-start"
        iconFallback: "start-here"
        title: i18nc("@title", "Show new apps and recent files in Start")
        description: i18nc("@info", "The Recommended part of the menu")
        checked: start.recommended
        onCheckedChanged: if (checked !== start.recommended) start.recommended = checked
    }
    ToggleCard {
        Layout.fillWidth: true
        visible: start.recentAppsFollowed
        iconName: "org.raye.settings-start"
        iconFallback: "start-here"
        title: i18nc("@title", "Show recently added apps")
        checked: start.recentApps
        onCheckedChanged: if (checked !== start.recentApps) start.recentApps = checked
    }
    ToggleCard {
        Layout.fillWidth: true
        visible: start.mostUsedFollowed
        iconName: "org.raye.settings-start"
        iconFallback: "start-here"
        title: i18nc("@title", "Show most used apps")
        checked: start.mostUsed
        onCheckedChanged: if (checked !== start.mostUsed) start.mostUsed = checked
    }
    NavCard {
        Layout.fillWidth: true
        searchId: "start-folders"
        iconName: "org.raye.settings-start-folders"
        iconFallback: "folder"
        title: i18nc("@title settings page", "Folders")
        description: i18nc("@info", "Choose which folders appear next to the power button")
        target: "personalization/start-folders"
    }
}
