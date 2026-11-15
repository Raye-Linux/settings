// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Update › Update history: what was installed and when, by kind
// (kernel, drivers, firmware, the rest of the system), newest first.
// Going back to how the PC was before an update is in Recovery.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import "updatetext.js" as U

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings page", "Raye Update"), target: "update" },
             { text: i18nc("@title settings page", "Update history") }]

    readonly property var status: typeof updates !== "undefined" ? updates : null
    readonly property var groups: U.historyGroups(status ? status.history : [])

    function kindTitle(kind, count) {
        switch (kind) {
        case "kernel": return i18ncp("@title", "Kernel updates (%1)", "Kernel updates (%1)", count)
        case "driver": return i18ncp("@title", "Driver updates (%1)", "Driver updates (%1)", count)
        case "firmware": return i18ncp("@title", "Firmware updates (%1)", "Firmware updates (%1)", count)
        default: return i18ncp("@title", "System updates (%1)", "System updates (%1)", count)
        }
    }

    function installedOn(time) {
        return i18nc("@info %1 is a date", "Installed on %1",
                     new Date(time * 1000).toLocaleDateString(Qt.locale(), Locale.ShortFormat))
    }

    SectionHeader { text: i18nc("@title:group", "Update history"); first: true }

    EmptyState {
        Layout.fillWidth: true
        visible: page.groups.length === 0
        title: i18nc("@info", "No updates installed yet")
        message: i18nc("@info", "Updates Raye Update installs show up here.")
    }

    Repeater {
        model: page.groups
        delegate: Expander {
            id: group
            required property var modelData
            required property int index
            expanded: index === 0
            iconName: modelData.kind === "kernel" || modelData.kind === "firmware" ? "org.raye.settings-firmware"
                    : modelData.kind === "driver" ? "org.raye.settings-drivers"
                    : "org.raye.settings-update-history"
            iconFallback: "view-history"
            title: page.kindTitle(modelData.kind, modelData.rows.length)

            Repeater {
                model: group.modelData.rows.slice(0, 100)
                delegate: ExpanderRow {
                    required property var modelData
                    title: modelData.devices > 0
                           ? i18ncp("@info", "Firmware for one device", "Firmware for %1 devices", modelData.devices)
                           : i18nc("@info %1 package, %2 version", "%1 %2", modelData.name, modelData.version)
                    description: page.installedOn(modelData.time)
                }
            }
            ExpanderRow {
                visible: group.modelData.rows.length > 100
                title: i18nc("@info", "And %1 more", group.modelData.rows.length - 100)
            }
        }
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }

    NavCard {
        iconName: "org.raye.settings-recovery"
        iconFallback: "edit-undo"
        title: i18nc("@title settings page", "Recovery")
        description: i18nc("@info", "Go back to how your PC was before an update")
        target: "system/recovery"
    }
}
