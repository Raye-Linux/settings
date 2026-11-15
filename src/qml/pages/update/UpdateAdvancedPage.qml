// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Update › Advanced options: the hours the PC never restarts on its
// own, and whether it may restart outside them to finish updating.
// Pausing is on the main page, as in Windows.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import "updatetext.js" as U

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings page", "Raye Update"), target: "update" },
             { text: i18nc("@title settings page", "Advanced options") }]

    readonly property var status: typeof updates !== "undefined" ? updates : null
    readonly property var hours: {
        const list = []
        for (let h = 0; h < 24; ++h)
            list.push({ text: new Date(2000, 0, 1, h).toLocaleTimeString(Qt.locale(), Locale.ShortFormat), value: h })
        return list
    }
    readonly property int start: status ? status.activeHoursStart : 8
    readonly property int end: status ? status.activeHoursEnd : 17
    // a pick that makes more than 18 hours is refused, as in Windows
    property bool tooLong: false

    function pick(start, end) {
        page.tooLong = !U.validActiveHours(start, end)
        if (!page.tooLong)
            page.status.setActiveHours(start, end)
    }

    SectionHeader { text: i18nc("@title:group", "Restarts"); first: true }

    Expander {
        searchId: "active-hours"
        expanded: true
        iconName: "org.raye.settings-time"
        iconFallback: "clock"
        title: i18nc("@title", "Active hours")
        description: i18nc("@info", "Your PC won't restart on its own during these hours")
        headerContent: Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            font: Theme.body
            color: Theme.textSecondary
            text: i18nc("@info %1 and %2 are times", "%1 to %2", page.hours[page.start].text, page.hours[page.end].text)
        }

        ExpanderRow {
            title: i18nc("@label", "Start time")
            ComboBox {
                model: page.hours
                textRole: "text"
                valueRole: "value"
                currentIndex: page.start
                enabled: page.status !== null
                Accessible.name: i18nc("@label", "Start time")
                onActivated: (index) => page.pick(index, page.end)
            }
        }
        ExpanderRow {
            title: i18nc("@label", "End time")
            description: page.tooLong ? i18nc("@info", "Active hours can be 18 hours at most.") : ""
            ComboBox {
                model: page.hours
                textRole: "text"
                valueRole: "value"
                currentIndex: page.end
                enabled: page.status !== null
                Accessible.name: i18nc("@label", "End time")
                onActivated: (index) => page.pick(page.start, index)
            }
        }
    }

    ToggleCard {
        searchId: "auto-restart"
        iconName: "org.raye.settings-status-restart"
        iconFallback: "system-reboot"
        title: i18nc("@title", "Restart outside active hours to finish updating")
        description: i18nc("@info", "Raye restarts on its own when updates are ready, never while you're playing or on battery.")
        enabled: page.status !== null
        checked: page.status ? page.status.autoRestart : false
        onToggled: page.status.setAutoRestart(checked)
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
