// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts › Files from Windows: pick the drive Windows is on (or a USB
// drive, or a folder), tick the folders of each account, copy. Windows
// keeps everything: the files are copied, never moved, and the Windows
// drive is only ever opened read-only.
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accounts"), target: "accounts" },
             { text: i18nc("@title settings page", "Files from Windows") }]

    readonly property string stage: transfer.stage
    readonly property var report: transfer.report
    readonly property bool roomShort: transfer.selectedBytes + 1073741824 > transfer.freeBytes
    property bool wallpaperSet: false

    WindowsImport {
        id: transfer
        onStageChanged: {
            if (stage === "done" && report.wallpaper && !page.wallpaperSet) {
                const settings = backgroundComponent.createObject(page)
                settings.setPicture("file://" + report.wallpaper)
                settings.destroy()
                page.wallpaperSet = true
            }
        }
    }
    Component {
        id: backgroundComponent
        BackgroundSettings { }
    }
    Component.onCompleted: transfer.refresh()

    FolderDialog {
        id: folderDialog
        title: i18nc("@title:window", "Choose the folder with your Windows files")
        onAccepted: transfer.openFolder(selectedFolder)
    }

    function folderName(kind) {
        switch (kind) {
        case "desktop": return i18nc("@label a folder", "Desktop")
        case "documents": return i18nc("@label a folder", "Documents")
        case "downloads": return i18nc("@label a folder", "Downloads")
        case "music": return i18nc("@label a folder", "Music")
        case "pictures": return i18nc("@label a folder", "Pictures")
        case "videos": return i18nc("@label a folder", "Videos")
        }
        return kind
    }
    function folderIcon(kind) {
        switch (kind) {
        case "desktop": return "user-desktop"
        case "documents": return "folder-documents"
        case "downloads": return "folder-download"
        case "music": return "folder-music"
        case "pictures": return "folder-pictures"
        case "videos": return "folder-videos"
        }
        return "folder"
    }
    function filesAndSize(files, bytes) {
        return i18ncp("@info %2 is a size", "%1 file, %2", "%1 files, %2", files, transfer.formatSize(bytes))
    }
    function driveTitle(volume) {
        if (volume.label !== "")
            return volume.label
        return volume.external ? i18nc("@label a drive", "USB drive") : i18nc("@label a drive", "Windows")
    }
    function driveText(volume) {
        const where = volume.external ? i18nc("@info a drive", "External drive") : i18nc("@info a drive", "On this PC")
        if (volume.state === "bitlocker")
            return i18nc("@info %1 is where the drive is, %2 its size", "%1, %2, locked with BitLocker",
                         where, transfer.formatSize(volume.size))
        return i18nc("@info %1 is where the drive is, %2 its size", "%1, %2", where, transfer.formatSize(volume.size))
    }
    function timeLeft(seconds) {
        if (seconds < 0)
            return i18nc("@info", "Working out how long it will take…")
        if (seconds < 60)
            return i18nc("@info", "Less than a minute left")
        const minutes = Math.round(seconds / 60)
        if (minutes < 90)
            return i18ncp("@info", "About %1 minute left", "About %1 minutes left", minutes)
        return i18ncp("@info", "About %1 hour left", "About %1 hours left", Math.round(minutes / 60))
    }
    function refusalTitle(reason) {
        switch (reason) {
        case "bitlocker": return i18nc("@info", "This drive is locked with BitLocker")
        case "hibernated": return i18nc("@info", "Windows didn't fully shut down")
        case "busy": return i18nc("@info", "This drive is in use")
        case "nothing": return i18nc("@info", "No Windows files here")
        case "room": return i18nc("@info", "Not enough space on this PC")
        case "gone": return i18nc("@info", "The drive with your files isn't here")
        case "denied": return i18nc("@info", "The drive wasn't opened")
        case "failed": return i18nc("@info", "The copy couldn't start")
        }
        return i18nc("@info", "This drive can't be read")
    }
    function refusalText(reason) {
        switch (reason) {
        case "bitlocker": return i18nc("@info", "Start Windows and turn BitLocker off for this drive: Settings › Privacy & security › Device encryption, or Control Panel › BitLocker Drive Encryption › Turn off BitLocker. When it's done, come back here.")
        case "hibernated": return i18nc("@info", "Windows is hibernated or uses Fast startup, so some of your files may only be in its memory. Start Windows, open Control Panel › Power Options › Choose what the power buttons do, turn off Fast startup, then choose Shut down. Then come back here.")
        case "busy": return i18nc("@info", "Close the windows that show this drive in Explorer, then try again.")
        case "nothing": return i18nc("@info", "No Windows account with Documents, Pictures, Music, Videos, Downloads or a Desktop was found there.")
        case "room": return i18nc("@info %1 and %2 are sizes", "You chose %1, and %2 is free. Untick some folders, or free up space in System › Storage.",
                                  transfer.formatSize(transfer.neededBytes || transfer.selectedBytes), transfer.formatSize(transfer.freeBytes))
        case "gone": return i18nc("@info", "Plug in the drive your files were on, then choose Continue again.")
        case "denied": return i18nc("@info", "Opening the Windows drive needs root access.")
        case "failed": return i18nc("@info", "Something went wrong before any file was copied. Try again.")
        }
        return i18nc("@info", "Start Windows and check the drive for errors (right-click it in File Explorer › Properties › Tools › Check), then try again.")
    }

    // ---- what this page is for ----
    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s5
        visible: page.stage === "pick" || page.stage === "choose"
        text: i18nc("@info", "Copy your documents, pictures, music, videos, downloads and desktop from Windows. Your files are copied, never moved: Windows keeps everything as it is.")
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        lineHeightMode: Text.FixedHeight
        lineHeight: Theme.bodyLineHeight
    }

    // ---- a copy that stopped ----
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: transfer.canResume && (page.stage === "pick" || page.stage === "stopped")
        closable: false
        severity: "warning"
        title: i18nc("@info", "The copy from Windows stopped")
        message: transfer.totalBytes > 0
                 ? i18nc("@info %1 and %2 are sizes", "%1 of %2 is already here. Continue to copy the rest.",
                         transfer.formatSize(transfer.doneBytes), transfer.formatSize(transfer.totalBytes))
                 : i18nc("@info", "Continue to copy the rest.")
        Button {
            text: i18nc("@action:button", "Continue")
            accent: true
            onClicked: transfer.resume()
        }
    }

    // ---- refused ----
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.stage === "refused" || (page.stage === "pick" && transfer.refusal !== "")
              || (page.stage === "choose" && transfer.refusal === "room")
        closable: false
        // something the person can fix is a warning; a drive that can't be read is an error
        severity: ["unreadable", "not-windows", "failed"].indexOf(transfer.refusal) >= 0 ? "error" : "warning"
        title: page.refusalTitle(transfer.refusal)
        message: page.refusalText(transfer.refusal)
    }
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: transfer.dirty && (page.stage === "choose" || page.stage === "copying")
        closable: false
        severity: "informational"
        title: i18nc("@info", "Windows wasn't shut down properly")
        message: i18nc("@info", "What Windows was saving last may be missing. To be sure, start Windows and choose Shut down, then copy again.")
    }
    Button {
        visible: page.stage === "refused"
        text: i18nc("@action:button", "Back")
        onClicked: transfer.back()
    }

    // ---- 1. where the files are ----
    SectionHeader {
        // what the search scrolls to for the drives
        property string searchId: "drive"
        visible: page.stage === "pick"
        first: true
        text: i18nc("@title", "Where are your files?")
    }
    Card {
        Layout.fillWidth: true
        visible: page.stage === "pick" && transfer.listing && transfer.volumes.length === 0
        iconName: "drive-harddisk"
        title: i18nc("@info", "Looking for drives…")
        ProgressBar { implicitWidth: 120; indeterminate: true }
    }
    Repeater {
        model: page.stage === "pick" ? transfer.volumes : []
        delegate: Card {
            required property var modelData
            required property int index
            Layout.fillWidth: true
            iconName: modelData.external ? "drive-removable-media" : "drive-harddisk"
            title: page.driveTitle(modelData)
            description: page.driveText(modelData)
            clickable: true
            onClicked: transfer.openVolume(modelData.device)
            Button {
                text: modelData.state === "bitlocker" ? i18nc("@action:button", "What to do")
                                                      : i18nc("@action:button", "Look for files")
                accent: modelData.windows && modelData.state === "ok"
                onClicked: transfer.openVolume(modelData.device)
            }
        }
    }
    Card {
        Layout.fillWidth: true
        visible: page.stage === "pick" && !transfer.listing && transfer.volumes.length === 0
        iconName: "drive-harddisk"
        title: i18nc("@info", "No Windows drive found")
        description: i18nc("@info", "Plug in the USB drive or disk with your files, or choose a folder.")
        Button {
            text: i18nc("@action:button", "Look again")
            onClicked: transfer.refresh()
        }
    }
    Card {
        Layout.fillWidth: true
        visible: page.stage === "pick"
        searchId: "folder"
        iconName: "folder-open"
        title: i18nc("@label", "Choose a folder")
        description: i18nc("@info", "A copy of your Windows folders on a USB drive or another disk")
        clickable: true
        onClicked: folderDialog.open()
        Button {
            text: i18nc("@action:button", "Browse")
            onClicked: folderDialog.open()
        }
    }

    // ---- opening, counting ----
    Card {
        Layout.fillWidth: true
        visible: page.stage === "opening" || page.stage === "scanning"
        iconName: "drive-harddisk"
        title: page.stage === "opening" ? i18nc("@info", "Opening the drive…")
                                        : i18nc("@info", "Looking for your files…")
        description: page.stage === "opening" ? i18nc("@info", "It's opened read-only: nothing on it can change.")
                                              : i18nc("@info", "Counting what's in each folder. A big drive can take a minute.")
        ProgressBar { implicitWidth: 120; indeterminate: true }
    }

    // ---- 2. what to bring ----
    Repeater {
        model: page.stage === "choose" ? transfer.accounts : []
        delegate: ColumnLayout {
            id: accountColumn
            required property var modelData
            required property int index
            Layout.fillWidth: true
            spacing: Theme.s1

            SectionHeader {
                first: accountColumn.index === 0
                text: transfer.accounts.length > 1
                      ? i18nc("@title %1 is the name of a Windows account", "Files of %1", accountColumn.modelData.name)
                      : i18nc("@title", "Choose what to copy")
            }
            Repeater {
                model: accountColumn.modelData.folders
                delegate: Card {
                    id: folderCard
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    iconName: page.folderIcon(modelData.kind)
                    iconFallback: "folder"
                    title: modelData.onedrive ? i18nc("@label %1 is a folder", "%1 (OneDrive)", page.folderName(modelData.kind))
                                              : page.folderName(modelData.kind)
                    description: page.filesAndSize(modelData.files, modelData.bytes)
                    clickable: true
                    Accessible.role: Accessible.CheckBox
                    Accessible.checked: modelData.checked
                    onClicked: transfer.setFolderChecked(accountColumn.index, index, !modelData.checked)
                    RecoveryCheckBox {
                        checked: modelData.checked
                        Accessible.name: folderCard.title
                        onClicked: transfer.setFolderChecked(accountColumn.index, index, checked)
                    }
                }
            }
            Card {
                Layout.fillWidth: true
                visible: accountColumn.modelData.wallpaper > 0
                iconName: "preferences-desktop-wallpaper"
                title: i18nc("@label", "Windows wallpaper")
                description: i18nc("@info", "Use the picture you had on Windows on Raye too")
                clickable: true
                onClicked: transfer.setWallpaperChecked(accountColumn.index, !accountColumn.modelData.wallpaperChecked)
                RecoveryCheckBox {
                    checked: accountColumn.modelData.wallpaperChecked
                    Accessible.name: i18nc("@label", "Windows wallpaper")
                    onClicked: transfer.setWallpaperChecked(accountColumn.index, checked)
                }
            }
            Card {
                Layout.fillWidth: true
                visible: accountColumn.modelData.browsers.length > 0
                iconName: "bookmarks"
                iconFallback: "bookmark-new"
                title: i18nc("@label", "Browser favorites")
                description: accountColumn.modelData.browsers.map(b => b.name)
                                 .filter((name, i, all) => all.indexOf(name) === i).join(", ")
                clickable: true
                onClicked: transfer.setBookmarksChecked(accountColumn.index, !accountColumn.modelData.bookmarksChecked)
                RecoveryCheckBox {
                    checked: accountColumn.modelData.bookmarksChecked
                    Accessible.name: i18nc("@label", "Browser favorites")
                    onClicked: transfer.setBookmarksChecked(accountColumn.index, checked)
                }
            }
        }
    }
    Card {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s5
        visible: page.stage === "choose"
        iconName: "edit-copy"
        title: transfer.selectedFiles > 0
               ? i18ncp("@info %2 is a size", "%1 file to copy, %2", "%1 files to copy, %2",
                        transfer.selectedFiles, transfer.formatSize(transfer.selectedBytes))
               : i18nc("@info", "Nothing chosen yet")
        description: i18nc("@info %1 is a size", "%1 free on this PC", transfer.formatSize(transfer.freeBytes))
        Row {
            spacing: Theme.s3
            Button {
                text: i18nc("@action:button", "Back")
                onClicked: transfer.back()
            }
            Button {
                text: i18nc("@action:button", "Copy files")
                accent: true
                enabled: transfer.selectedFiles > 0 && !page.roomShort
                onClicked: transfer.start()
            }
        }
    }

    // ---- 3. copying ----
    Card {
        Layout.fillWidth: true
        visible: page.stage === "copying"
        iconName: "edit-copy"
        title: transfer.totalFiles > 0
               ? i18nc("@info %1 and %2 are numbers of files", "Copying %1 of %2 files", transfer.doneFiles, transfer.totalFiles)
               : i18nc("@info", "Getting ready to copy…")
        description: transfer.totalBytes > 0
                     ? i18nc("@info %1 and %2 are sizes, %3 how long is left", "%1 of %2 · %3",
                             transfer.formatSize(transfer.doneBytes), transfer.formatSize(transfer.totalBytes),
                             page.timeLeft(transfer.secondsLeft))
                     : ""
        Button {
            text: i18nc("@action:button", "Stop")
            onClicked: transfer.stop()
        }
    }
    ProgressBar {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s3
        visible: page.stage === "copying"
        indeterminate: transfer.totalBytes <= 0
        value: transfer.totalBytes > 0 ? transfer.doneBytes / transfer.totalBytes : 0
    }
    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s3
        visible: page.stage === "copying"
        text: (transfer.currentFolder !== "" ? i18nc("@info %1 is a folder", "Now copying: %1", page.folderName(transfer.currentFolder)) + "  ·  " : "")
              + i18nc("@info", "Each file is checked once copied. You can keep using your PC; leave Settings open until it's done.")
        font: Theme.caption
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    // ---- 4. done ----
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.stage === "done"
        closable: false
        severity: (page.report.failed || 0) > 0 ? "warning" : "success"
        title: (page.report.failed || 0) > 0 ? i18nc("@info", "Most of your files are here")
                                             : i18nc("@info", "Your files are here")
        message: i18nc("@info", "Windows still has its own copy of everything.")
    }
    SectionHeader {
        visible: page.stage === "done" || page.stage === "stopped"
        first: true
        text: i18nc("@title", "What was copied")
    }
    Card {
        Layout.fillWidth: true
        visible: page.stage === "done" || page.stage === "stopped"
        iconName: "dialog-ok"
        title: i18ncp("@info", "%1 file copied", "%1 files copied", page.report.files || 0)
        description: i18nc("@info %1 is a size", "%1, each one read back to check it", transfer.formatSize(page.report.bytes || 0))
        Button {
            visible: page.stage === "done"
            text: i18nc("@action:button", "Open Documents")
            onClicked: transfer.showInFiles(page.report.folders && page.report.folders.documents
                                            ? page.report.folders.documents
                                            : Object.values(page.report.folders || {})[0] || "")
        }
    }
    Card {
        Layout.fillWidth: true
        visible: (page.stage === "done" || page.stage === "stopped") && (page.report.already || 0) > 0
        iconName: "folder-sync"
        title: i18ncp("@info", "%1 file was already here", "%1 files were already here", page.report.already || 0)
        description: i18nc("@info", "Copied before, so they were left as they are.")
    }
    Card {
        Layout.fillWidth: true
        visible: page.stage === "done" && (page.report.failed || 0) > 0
        iconName: "dialog-warning"
        title: i18ncp("@info", "%1 file couldn't be copied", "%1 files couldn't be copied", page.report.failed || 0)
        description: i18nc("@info", "They could not be read. Often these are OneDrive files kept only online: in Windows, right-click them and choose Always keep on this device, then copy again.")
                     + "\n" + (page.report.examples || []).slice(0, 5).join("\n")
    }
    Card {
        Layout.fillWidth: true
        visible: page.stage === "done" && (page.report.links || 0) + (page.report.windows || 0) + (page.report.special || 0) > 0
        iconName: "dialog-information"
        title: i18ncp("@info", "%1 item left out", "%1 items left out",
                      (page.report.links || 0) + (page.report.windows || 0) + (page.report.special || 0))
        description: i18nc("@info", "Windows shortcuts and links to other places (they don't work on Raye), and files Windows keeps for itself, like desktop.ini and Thumbs.db.")
    }
    Card {
        Layout.fillWidth: true
        visible: page.stage === "done" && (page.report.wallpaper || "") !== ""
        iconName: "preferences-desktop-wallpaper"
        title: i18nc("@info", "Your Windows wallpaper is on your desktop")
        description: i18nc("@info", "It's also in your Pictures folder.")
    }
    Card {
        Layout.fillWidth: true
        visible: page.stage === "done" && (page.report.bookmarks || "") !== ""
        iconName: "bookmarks"
        iconFallback: "bookmark-new"
        title: i18nc("@info", "Your browser favorites are in Documents")
        description: i18nc("@info", "To add them to Firefox: Bookmarks › Manage bookmarks › Import and Backup › Import Bookmarks from HTML, then pick this file.")
        Button {
            text: i18nc("@action:button", "Show")
            onClicked: transfer.showInFiles(page.report.bookmarks.substring(0, page.report.bookmarks.lastIndexOf("/")))
        }
    }
    Button {
        Layout.topMargin: Theme.s5
        visible: page.stage === "done" || page.stage === "stopped"
        text: page.stage === "done" ? i18nc("@action:button", "Done") : i18nc("@action:button", "Later")
        onClicked: transfer.back()
    }
}
