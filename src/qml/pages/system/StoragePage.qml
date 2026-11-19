// SPDX-License-Identifier: GPL-3.0-or-later
// System › Storage: how full the Raye drive is and what fills it, Clean
// up for the files nobody needs, Storage Sense to do it on a schedule, and
// the other drives.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    // the last clean-up: freed bytes and what went wrong ("" when all went well)
    property real freed: -1
    property string problem: ""
    property bool showResult: false

    readonly property var primaryDisks: storage.disks.filter(d => d.primary)
    readonly property var otherDisks: storage.disks.filter(d => !d.primary)

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Storage") }]

    Storage {
        id: storage
        onCleanupFinished: (bytes, what) => {
            page.freed = bytes
            page.problem = what
            page.showResult = true
            storage.refresh()
            storage.estimateCleanup()
        }
    }
    Component.onCompleted: {
        storage.refresh()
        storage.estimateCleanup()
    }

    function openCleanup() {
        cleanupDialog.chosen = { thumbnails: true, apt: true, logs: true }
        if (!storage.estimating)
            storage.estimateCleanup()
        cleanupDialog.open()
    }

    StorageCleanupDialog {
        id: cleanupDialog
        items: storage.cleanupItems
        estimating: storage.estimating
        formatSize: (bytes) => storage.formatSize(bytes)
        onCleanRequested: (items) => {
            page.showResult = false
            storage.clean(items)
        }
    }

    // cleaning, then what it freed
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: storage.cleaning
        closable: false
        severity: "informational"
        title: i18nc("@info:status", "Cleaning up…")
        message: i18nc("@info", "You can keep using your PC.")
    }
    ProgressBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        visible: storage.cleaning
        indeterminate: true
    }
    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.showResult && !storage.cleaning && page.problem !== "cancelled"
        severity: page.problem === "" ? "success" : "warning"
        title: page.problem === "busy" ? i18nc("@info", "Raye Update is busy")
             : page.problem === "denied" ? i18nc("@info", "This needs root access")
             : page.problem !== "" ? i18nc("@info", "Some files couldn't be deleted")
             : i18nc("@info %1 is a size", "%1 freed", storage.formatSize(Math.max(0, page.freed)))
        message: page.problem === "busy" ? i18nc("@info", "Downloaded update files can be cleaned once it's done. Try again in a few minutes.")
               : page.problem === "denied" ? i18nc("@info", "Ask the person who manages this PC to clean up system files.")
               : page.problem !== "" ? i18nc("@info %1 is a size", "%1 was freed. A file in use was left where it is.",
                                             storage.formatSize(Math.max(0, page.freed)))
               : i18nc("@info", "Your files are safe. Only what you chose was deleted.")
        onClosed: page.showResult = false
    }

    // the system drive, and the drive the home is on
    Repeater {
        model: page.primaryDisks
        StorageDriveSection {
            required property var modelData
            required property int index
            disk: modelData
            first: index === 0
            formatSize: (bytes) => storage.formatSize(bytes)
            onCleanupRequested: page.openCleanup()
            onFolderRequested: (path) => storage.openFolder(path)
        }
    }

    SectionHeader {
        text: i18nc("@title", "Storage management")
        first: page.primaryDisks.length === 0
    }

    Expander {
        id: sense
        searchId: "storage-sense"
        iconName: "org.raye.settings-storage"
        iconFallback: "drive-harddisk"
        title: i18nc("@label", "Storage Sense")
        description: i18nc("@info", "Free up space by itself: delete old files from the trash and Downloads, and clean up thumbnails")
        headerContent: [
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: storage.senseEnabled ? i18nc("@label a setting that is on", "On")
                                           : i18nc("@label a setting that is off", "Off")
                font: Theme.body
                color: Theme.text
                Accessible.ignored: true
            },
            ToggleSwitch {
                anchors.verticalCenter: parent.verticalCenter
                checked: storage.senseEnabled
                Accessible.name: i18nc("@label", "Storage Sense")
                onToggled: storage.senseEnabled = checked
            }
        ]

        ExpanderRow {
            searchId: "sense-frequency"
            title: i18nc("@label", "Run Storage Sense")
            ComboBox {
                readonly property var frequencies: ["day", "week", "month", "lowspace"]
                model: [i18nc("@item:inlistbox run Storage Sense", "Every day"),
                        i18nc("@item:inlistbox run Storage Sense", "Every week"),
                        i18nc("@item:inlistbox run Storage Sense", "Every month"),
                        i18nc("@item:inlistbox run Storage Sense", "During low free disk space")]
                currentIndex: Math.max(0, frequencies.indexOf(storage.senseFrequency))
                Accessible.name: i18nc("@label", "Run Storage Sense")
                onActivated: (index) => storage.senseFrequency = frequencies[index]
            }
        }
        ExpanderRow {
            searchId: "trash-days"
            title: i18nc("@label", "Delete files in my trash if they've been there for over")
            ComboBox {
                id: trashDays
                readonly property var days: [0, 1, 14, 30, 60]
                model: [i18nc("@item:inlistbox trash", "Never"),
                        i18ncp("@item:inlistbox", "%1 day", "%1 days", 1),
                        i18ncp("@item:inlistbox", "%1 day", "%1 days", 14),
                        i18ncp("@item:inlistbox", "%1 day", "%1 days", 30),
                        i18ncp("@item:inlistbox", "%1 day", "%1 days", 60)]
                currentIndex: Math.max(0, days.indexOf(storage.senseTrashDays))
                Accessible.name: i18nc("@label", "Delete files in my trash if they've been there for over")
                onActivated: (index) => storage.senseTrashDays = days[index]
            }
        }
        ExpanderRow {
            searchId: "downloads-days"
            title: i18nc("@label", "Delete files in my Downloads folder if they haven't been opened for more than")
            ComboBox {
                readonly property var days: [0, 1, 14, 30, 60]
                model: [i18nc("@item:inlistbox downloads", "Never"),
                        i18ncp("@item:inlistbox", "%1 day", "%1 days", 1),
                        i18ncp("@item:inlistbox", "%1 day", "%1 days", 14),
                        i18ncp("@item:inlistbox", "%1 day", "%1 days", 30),
                        i18ncp("@item:inlistbox", "%1 day", "%1 days", 60)]
                currentIndex: Math.max(0, days.indexOf(storage.senseDownloadsDays))
                Accessible.name: i18nc("@label", "Delete files in my Downloads folder if they haven't been opened for more than")
                onActivated: (index) => storage.senseDownloadsDays = days[index]
            }
        }
        ExpanderRow {
            searchId: "sense-thumbnails"
            title: i18nc("@label", "Clean up thumbnails I haven't looked at for 30 days")
            description: i18nc("@info", "They come back by themselves when a folder is opened")
            ToggleSwitch {
                checked: storage.senseThumbnails
                Accessible.name: i18nc("@label", "Clean up thumbnails I haven't looked at for 30 days")
                onToggled: storage.senseThumbnails = checked
            }
        }
        ExpanderRow {
            searchId: "run-sense"
            title: i18nc("@label", "Run Storage Sense now")
            description: storage.senseRunning ? i18nc("@info:status", "Running…")
                       : storage.senseLastRun.getTime && !isNaN(storage.senseLastRun.getTime())
                         ? i18nc("@info %1 is a date", "Last run: %1",
                                 Qt.formatDate(storage.senseLastRun, Qt.locale(), Locale.LongFormat))
                         : i18nc("@info", "It hasn't run yet")
            Button {
                text: i18nc("@action:button", "Run now")
                enabled: !storage.senseRunning
                Accessible.description: i18nc("@label", "Run Storage Sense now")
                onClicked: storage.runSenseNow()
            }
        }
    }

    Card {
        searchId: "cleanup"
        iconName: "user-trash"
        iconFallback: "edit-clear"
        title: i18nc("@label", "Cleanup recommendations")
        description: storage.estimating && storage.cleanupItems.length === 0
                     ? i18nc("@info:status", "Looking for files you can delete…")
                     : storage.cleanupTotal > 0
                       ? i18nc("@info %1 is a size", "%1 can be freed", storage.formatSize(storage.cleanupTotal))
                       : i18nc("@info", "Nothing to clean up right now")
        clickable: !storage.cleaning
        actionGlyph: "chevron-right"
        onClicked: page.openCleanup()
    }

    Expander {
        visible: page.otherDisks.length > 0
        searchId: "other-drives"
        iconName: "drive-harddisk"
        title: i18nc("@label", "Storage used on other drives")
        description: i18nc("@info", "USB drives, Windows and your other drives")

        Repeater {
            model: page.otherDisks
            ExpanderRow {
                required property var modelData
                title: modelData.name
                description: i18nc("@info %1 free space, %2 total size", "%1 free of %2",
                                   storage.formatSize(modelData.free), storage.formatSize(modelData.total))
                StorageUsageBar {
                    width: 160
                    height: 6
                    total: modelData.total
                    used: modelData.used
                    warn: modelData.total > 0 && modelData.used / modelData.total > 0.9
                }
            }
        }
    }

    LinkCard {
        visible: storage.disksToolAvailable
        searchId: "disks-volumes"
        iconName: "org.raye.settings-hardware"
        iconFallback: "partitionmanager"
        title: i18nc("@label", "Disks & volumes")
        description: i18nc("@info", "Format drives and change their partitions")
        onClicked: storage.openDisksTool()
    }
}
