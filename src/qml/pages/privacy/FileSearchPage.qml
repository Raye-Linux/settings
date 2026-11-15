// SPDX-License-Identifier: GPL-3.0-or-later
// Privacy & security › Searching your files: the file index that makes
// search in Start and in Files instant, and the folders it leaves out.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Privacy & security"), target: "privacy" },
             { text: i18nc("@title settings page", "Searching your files") }]

    FileIndexing { id: indexing }

    ToggleCard {
        Layout.fillWidth: true
        searchId: "index-files"
        iconName: "org.raye.settings-file-search"
        iconFallback: "search"
        title: i18nc("@label", "Index my files for search")
        description: i18nc("@info", "Search in Start and in Files finds your files at once. Raye builds the index when your PC isn't busy, and it stays on this PC.")
        checked: indexing.enabled
        onToggled: {
            indexing.enabled = checked
            checked = Qt.binding(() => indexing.enabled)
        }
    }

    SectionHeader {
        text: i18nc("@title:group", "Exclude folders from search")
    }

    ButtonCard {
        Layout.fillWidth: true
        searchId: "exclude-folder"
        iconName: "folder-new"
        title: i18nc("@label", "Add a folder to exclude")
        description: i18nc("@info", "Files in it won't show up in search.")
        buttonText: i18nc("@action:button", "Choose a folder")
        onButtonClicked: indexing.chooseFolderToExclude()
    }

    Repeater {
        model: indexing.excludedFolders
        delegate: Card {
            id: folderCard
            required property string modelData

            Layout.fillWidth: true
            iconName: "folder"
            title: indexing.displayPath(folderCard.modelData)
            Button {
                text: i18nc("@action:button stop leaving this folder out of search", "Remove")
                onClicked: indexing.removeExcludedFolder(folderCard.modelData)
            }
        }
    }

    SectionHeader {
        text: i18nc("@title:group", "Related settings")
        visible: indexing.canOpenAdvanced()
    }
    LinkCard {
        Layout.fillWidth: true
        visible: indexing.canOpenAdvanced()
        searchId: "indexing-options"
        iconName: "configure"
        title: i18nc("@label", "More indexing options")
        description: i18nc("@info", "Hidden folders, file contents, and which folders are indexed")
        onClicked: indexing.openAdvanced()
    }
}
