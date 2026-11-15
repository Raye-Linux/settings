// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Start > Folders: the folders next to the power button.
//
// The switches are saved in rayeshellrc. While the menu does not draw these
// folders, the page says so: a switch that seems to do nothing is worse
// than one that admits it.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
             { text: i18nc("@title settings page", "Start"), target: "personalization/start" },
             { text: i18nc("@title settings page", "Folders") }]

    StartSettings { id: start }

    readonly property var rows: [
        { id: "settings", icon: "org.raye.settings-system", fallback: "preferences-system", title: i18nc("@item folder on Start", "Settings") },
        { id: "files", icon: "system-file-manager", fallback: "folder", title: i18nc("@item folder on Start", "Files") },
        { id: "documents", icon: "folder-documents", fallback: "folder", title: i18nc("@item folder on Start", "Documents") },
        { id: "downloads", icon: "folder-download", fallback: "folder", title: i18nc("@item folder on Start", "Downloads") },
        { id: "music", icon: "folder-music", fallback: "folder", title: i18nc("@item folder on Start", "Music") },
        { id: "pictures", icon: "folder-pictures", fallback: "folder", title: i18nc("@item folder on Start", "Pictures") },
        { id: "videos", icon: "folder-videos", fallback: "folder", title: i18nc("@item folder on Start", "Videos") },
        { id: "network", icon: "folder-network", fallback: "folder", title: i18nc("@item folder on Start", "Network") },
        { id: "personal", icon: "user-home", fallback: "folder", title: i18nc("@item folder on Start", "Personal folder") }
    ]

    Card {
        objectName: "notShownYet"
        Layout.fillWidth: true
        visible: !start.foldersFollowed
        iconName: "org.raye.settings-status-info"
        iconFallback: "dialog-information"
        title: i18nc("@title", "Not on Start yet")
        description: i18nc("@info", "Start doesn't show these folders yet. Your choices are kept for when it does.")
    }

    SectionHeader {
        property string searchId: "folders"
        text: i18nc("@title:group", "Choose which folders appear on Start next to the power button")
    }
    Repeater {
        model: page.rows
        delegate: ToggleCard {
            required property var modelData
            Layout.fillWidth: true
            iconName: modelData.icon
            iconFallback: modelData.fallback
            title: modelData.title
            checked: start.folders.indexOf(modelData.id) >= 0
            onCheckedChanged: if (checked !== (start.folders.indexOf(modelData.id) >= 0)) start.setFolder(modelData.id, checked)
        }
    }
}
