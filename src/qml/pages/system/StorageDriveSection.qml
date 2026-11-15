// SPDX-License-Identifier: GPL-3.0-or-later
// One drive on the Storage page, the way Windows shows drive C: its
// name and size, the space used and free over a bar coloured by
// category, then the largest categories, the others behind "Show more
// categories".
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ColumnLayout {
    id: section

    // a drive of Storage.disks
    property var disk: ({})
    property var formatSize: (bytes) => ""
    property bool first: false
    property bool showAll: false
    readonly property int shownAtFirst: 4

    signal cleanupRequested()
    signal folderRequested(string path)

    readonly property var _categories: disk.categories || []
    readonly property real _used: disk.used || 0
    readonly property real _total: disk.total || 0
    readonly property bool _almostFull: _total > 0 && _used / _total > 0.9
    readonly property var _shown: showAll ? _categories : _categories.slice(0, shownAtFirst)

    spacing: Theme.cardSpacing
    Layout.fillWidth: true

    function nameOf(id) {
        switch (id) {
        case "apps": return i18nc("@label storage category", "Apps")
        case "system": return i18nc("@label storage category", "System & reserved")
        case "temporary": return i18nc("@label storage category", "Temporary files")
        case "documents": return i18nc("@label storage category", "Documents")
        case "pictures": return i18nc("@label storage category", "Pictures")
        case "music": return i18nc("@label storage category", "Music")
        case "videos": return i18nc("@label storage category", "Videos")
        case "downloads": return i18nc("@label storage category", "Downloads")
        case "desktop": return i18nc("@label storage category", "Desktop")
        }
        return i18nc("@label storage category", "Other")
    }

    function iconOf(id) {
        switch (id) {
        case "apps": return "org.raye.settings-installed-apps"
        case "system": return "org.raye.settings-hardware"
        case "temporary": return "user-trash"
        case "documents": return "folder-documents"
        case "pictures": return "folder-pictures"
        case "music": return "folder-music"
        case "videos": return "folder-videos"
        case "downloads": return "folder-download"
        case "desktop": return "user-desktop"
        }
        return "folder"
    }

    // the colours of the bar, one family of hues around the accent
    function colorOf(id) {
        switch (id) {
        case "system": return Theme.night ? "#8d7fa8" : "#7d6f98"
        case "apps": return Theme.accent
        case "temporary": return Theme.caution
        case "documents": return Theme.night ? "#6f9bff" : "#2f5fc9"
        case "pictures": return Theme.success
        case "music": return Theme.brandMagenta
        case "videos": return Theme.night ? "#ff6fae" : "#c72c74"
        case "downloads": return Theme.night ? "#4fd1e0" : "#0f7f8c"
        case "desktop": return Theme.night ? "#b48cff" : "#8a5cf0"
        }
        return Theme.night ? "#5f5476" : "#b9b0c8"
    }

    SectionHeader {
        first: section.first
        text: i18nc("@title %1 drive name, %2 its size", "%1 · %2", section.disk.name || "",
                    section.formatSize(section._total))
    }

    // used and free over the bar
    Card {
        id: usage
        Layout.fillWidth: true
        implicitHeight: usageColumn.implicitHeight + 2 * Theme.cardPadding
        Accessible.name: i18nc("@info %1 used space, %2 free space", "%1 used, %2 free",
                               section.formatSize(section._used), section.formatSize(disk.free || 0))

        ColumnLayout {
            id: usageColumn
            parent: usage.contentItem
            width: parent.width
            spacing: Theme.s4
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: i18nc("@info %1 is a size", "%1 used", section.formatSize(section._used))
                    font: Theme.bodyStrong
                    color: Theme.text
                    horizontalAlignment: Text.AlignLeft
                }
                Text {
                    text: i18nc("@info %1 is a size", "%1 free", section.formatSize(section.disk.free || 0))
                    font: Theme.body
                    color: section._almostFull ? Theme.caution : Theme.textSecondary
                }
            }
            StorageUsageBar {
                Layout.fillWidth: true
                total: section._total
                used: section._used
                categories: section._categories
                colorOf: section.colorOf
                warn: section._almostFull
            }
            Text {
                Layout.fillWidth: true
                visible: section._almostFull
                text: i18nc("@info", "This drive is almost full. Clean up to make room for updates and new files.")
                font: Theme.caption
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignLeft
            }
        }
    }

    // the count is not done and there is no earlier one: the cards
    // wait as quiet placeholders
    Repeater {
        model: section._categories.length === 0 && !section.disk.counted ? 4 : 0
        Card {
            id: placeholder
            required property int index
            Layout.fillWidth: true
            iconName: "folder"
            enabled: false
            Accessible.name: i18nc("@info:status something is loading", "Loading")
            Shimmer {
                parent: placeholder.contentItem
                x: placeholder.mirrored ? parent.width - width - (Theme.cardTextLeft - Theme.cardPadding)
                                        : Theme.cardTextLeft - Theme.cardPadding
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(260 - placeholder.index * 40, parent.width / 2)
                height: 12
                color: Theme.night ? Theme.control : Theme.divider
            }
        }
    }

    Repeater {
        model: section._shown
        StorageCategoryCard {
            required property var modelData
            Layout.fillWidth: true
            categoryId: modelData.id
            title: section.nameOf(modelData.id)
            iconName: section.iconOf(modelData.id)
            iconFallback: "folder"
            bytes: modelData.bytes
            usedBytes: section._used
            sizeText: section.formatSize(modelData.bytes)
            barColor: section.colorOf(modelData.id)
            readonly property bool opensFolder: (modelData.path || "") !== ""
            clickable: opensFolder || modelData.id === "apps" || modelData.id === "temporary"
            actionGlyph: opensFolder ? "open-external" : clickable ? "chevron-right" : ""
            onClicked: {
                if (opensFolder)
                    section.folderRequested(modelData.path)
                else if (modelData.id === "apps")
                    Router.navigate("apps/installed")
                else if (modelData.id === "temporary")
                    section.cleanupRequested()
            }
        }
    }

    HyperlinkButton {
        visible: section._categories.length > section.shownAtFirst
        Layout.topMargin: Theme.s1
        Layout.leftMargin: Theme.s1
        text: section.showAll ? i18nc("@action:button", "Show fewer categories")
                              : i18nc("@action:button", "Show more categories")
        onClicked: section.showAll = !section.showAll
    }
}
