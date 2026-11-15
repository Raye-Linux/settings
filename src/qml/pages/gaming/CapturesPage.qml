// SPDX-License-Identifier: GPL-3.0-or-later
// Gaming > Captures: where screenshots and screen recordings go.
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Gaming"), target: "gaming" },
             { text: i18nc("@title settings page", "Captures") }]

    CapturesSettings { id: captures }

    Card {
        Layout.fillWidth: true
        searchId: "screenshots-folder"
        iconName: "folder-pictures"
        iconFallback: "folder"
        title: i18nc("@title", "Screenshots")
        description: captures.pathOf(captures.screenshots)
        Button {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: i18nc("@action:button", "Open folder")
            onClicked: captures.open(captures.screenshots)
        }
        Button {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: i18nc("@action:button", "Change")
            onClicked: { folderDialog.forRecordings = false; folderDialog.currentFolder = captures.screenshots; folderDialog.open() }
        }
    }
    Card {
        Layout.fillWidth: true
        searchId: "recordings-folder"
        iconName: "folder-videos"
        iconFallback: "folder"
        title: i18nc("@title", "Screen recordings")
        description: captures.pathOf(captures.recordings)
        Button {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: i18nc("@action:button", "Open folder")
            onClicked: captures.open(captures.recordings)
        }
        Button {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: i18nc("@action:button", "Change")
            onClicked: { folderDialog.forRecordings = true; folderDialog.currentFolder = captures.recordings; folderDialog.open() }
        }
    }

    FolderDialog {
        id: folderDialog
        property bool forRecordings: false
        title: i18nc("@title:window", "Choose a folder")
        onAccepted: forRecordings ? captures.setRecordings(selectedFolder) : captures.setScreenshots(selectedFolder)
    }
}
