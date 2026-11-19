// SPDX-License-Identifier: GPL-3.0-or-later
// System › Notifications: the master switch (with sounds and the lock
// screen inside it), do not disturb and when it turns on by itself, a
// link to Focus, then every app that sends notifications with its own
// switch. Clicking an app opens its settings over the list.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

Item {
    id: root

    NotificationSettings { id: notifications }
    DndSchedule { id: dndSchedule }

    // the app whose page is open, "" for the list
    property string appId: ""

    function scrollToAnchor(anchor) {
        root.appId = ""
        return list.scrollToAnchor(anchor)
    }

    // the time do not disturb ends, in the user's words
    function untilText(date) {
        const now = new Date()
        const time = date.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
        if (date.toDateString() === now.toDateString())
            return time
        return date.toLocaleDateString(Qt.locale(), Locale.ShortFormat) + " " + time
    }

    PageScaffold {
        id: list
        anchors.fill: parent
        visible: root.appId === ""
        crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
                 { text: i18nc("@title settings page", "Notifications") }]

        Expander {
            id: master
            searchId: "notifications"
            iconName: "org.raye.settings-notifications"
            iconFallback: "preferences-desktop-notification-bell"
            title: i18nc("@title", "Notifications")
            description: i18nc("@info", "Get notifications from apps and other senders")
            headerContent: Row {
                spacing: Theme.s4
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    font: Theme.body
                    color: Theme.text
                    text: notifications.enabled ? i18nc("@label a setting that is on", "On")
                                                : i18nc("@label a setting that is off", "Off")
                }
                ToggleSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    text: i18nc("@title", "Notifications")
                    checked: notifications.enabled
                    onToggled: notifications.enabled = checked
                }
            }

            ExpanderRow {
                searchId: "notification-sounds"
                title: i18nc("@option:check", "Allow notifications to play sounds")
                enabled: notifications.enabled
                NotificationsCheckBox {
                    text: i18nc("@option:check", "Allow notifications to play sounds")
                    Accessible.name: text
                    // the text is the row's title: the box shows only its square
                    contentItem: Item {}
                    checked: notifications.sounds
                    onToggled: notifications.sounds = checked
                }
            }
            ExpanderRow {
                searchId: "lock-screen-notifications"
                visible: notifications.lockScreenAvailable
                title: i18nc("@option:check", "Show notifications on the lock screen")
                description: i18nc("@info", "Only the app icons and how many there are, never what they say")
                enabled: notifications.enabled
                NotificationsCheckBox {
                    text: i18nc("@option:check", "Show notifications on the lock screen")
                    contentItem: Item {}
                    checked: notifications.lockScreen
                    onToggled: notifications.lockScreen = checked
                }
            }
        }

        ToggleCard {
            searchId: "do-not-disturb"
            iconName: "org.raye.settings-focus"
            iconFallback: "notifications-disabled"
            title: i18nc("@title", "Do not disturb")
            description: !notifications.enabled
                         ? i18nc("@info", "Notifications are off")
                         : notifications.doNotDisturb && !isNaN(notifications.doNotDisturbUntil)
                           ? i18nc("@info %1 a time", "On until %1. Notifications go straight to the notification center.",
                                   root.untilText(notifications.doNotDisturbUntil))
                           : i18nc("@info", "Notifications go straight to the notification center")
            enabled: notifications.enabled
            checked: notifications.doNotDisturb
            onToggled: notifications.doNotDisturb = checked
        }

        Expander {
            searchId: "dnd-automatically"
            iconName: "org.raye.settings-focus"
            iconFallback: "clock"
            title: i18nc("@title", "Turn on do not disturb automatically")
            enabled: notifications.enabled

            ExpanderRow {
                searchId: "dnd-schedule"
                title: i18nc("@option:check", "During these times")
                NotificationsCheckBox {
                    text: i18nc("@option:check", "During these times")
                    contentItem: Item {}
                    checked: dndSchedule.enabled
                    onToggled: dndSchedule.enabled = checked
                }
            }
            ExpanderRow {
                searchId: "dnd-schedule-from"
                title: i18nc("@label do not disturb starts at", "From")
                enabled: dndSchedule.enabled
                ComboBox {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    readonly property var values: dndSchedule.times()
                    model: values.map((t) => dndSchedule.displayTime(t))
                    currentIndex: Math.max(0, values.indexOf(dndSchedule.start))
                    Accessible.name: i18nc("@label do not disturb starts at", "From") + ", " + displayText
                    onActivated: (index) => dndSchedule.start = values[index]
                }
            }
            ExpanderRow {
                searchId: "dnd-schedule-to"
                title: i18nc("@label do not disturb ends at", "To")
                enabled: dndSchedule.enabled
                ComboBox {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    readonly property var values: dndSchedule.times()
                    model: values.map((t) => dndSchedule.displayTime(t))
                    currentIndex: Math.max(0, values.indexOf(dndSchedule.end))
                    Accessible.name: i18nc("@label do not disturb ends at", "To") + ", " + displayText
                    onActivated: (index) => dndSchedule.end = values[index]
                }
            }
            ExpanderRow {
                searchId: "dnd-schedule-days"
                title: i18nc("@label do not disturb on these days", "Repeats")
                enabled: dndSchedule.enabled
                ComboBox {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    readonly property var values: ["everyday", "weekdays", "weekends"]
                    model: [i18nc("@item:inlistbox do not disturb repeats", "Every day"),
                            i18nc("@item:inlistbox do not disturb repeats", "Weekdays"),
                            i18nc("@item:inlistbox do not disturb repeats", "Weekends")]
                    currentIndex: Math.max(0, values.indexOf(dndSchedule.days))
                    Accessible.name: i18nc("@label do not disturb on these days", "Repeats") + ", " + displayText
                    onActivated: (index) => dndSchedule.days = values[index]
                }
            }
            ExpanderRow {
                searchId: "dnd-duplicating"
                title: i18nc("@option:check", "When duplicating your display")
                NotificationsCheckBox {
                    text: i18nc("@option:check", "When duplicating your display")
                    contentItem: Item {}
                    checked: notifications.whenScreensMirrored
                    onToggled: notifications.whenScreensMirrored = checked
                }
            }
            ExpanderRow {
                searchId: "dnd-sharing"
                title: i18nc("@option:check", "When sharing your screen")
                NotificationsCheckBox {
                    text: i18nc("@option:check", "When sharing your screen")
                    contentItem: Item {}
                    checked: notifications.whenScreenSharing
                    onToggled: notifications.whenScreenSharing = checked
                }
            }
            ExpanderRow {
                searchId: "dnd-full-screen"
                title: i18nc("@option:check", "When playing a game or using an app in full screen")
                NotificationsCheckBox {
                    text: i18nc("@option:check", "When playing a game or using an app in full screen")
                    contentItem: Item {}
                    checked: notifications.whenFullscreen
                    onToggled: notifications.whenFullscreen = checked
                }
            }
        }

        NavCard {
            iconName: "org.raye.settings-focus"
            iconFallback: "clock"
            title: i18nc("@title settings page", "Focus")
            description: notifications.focusActive
                         ? i18nc("@info %1 a time", "A focus session is on until %1", root.untilText(notifications.focusUntil))
                         : i18nc("@info", "Fewer distractions: a timer, and do not disturb while it runs")
            target: "system/focus"
        }

        SectionHeader {
            text: i18nc("@title", "Notifications from apps and other senders")
        }

        Repeater {
            model: notifications.apps
            delegate: Card {
                id: appCard
                required property var modelData
                Layout.fillWidth: true
                iconName: modelData.icon || "application-x-executable"
                iconFallback: "application-x-executable"
                title: modelData.name
                description: modelData.enabled ? i18nc("@info notifications of an app", "Banners, notification center")
                                               : i18nc("@info notifications of an app", "Off")
                clickable: true
                actionGlyph: "chevron-right"
                enabled: notifications.enabled
                onClicked: root.appId = modelData.id

                Text {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    font: Theme.body
                    color: appCard.enabled ? Theme.text : Theme.textDisabled
                    text: appCard.modelData.enabled ? i18nc("@label a setting that is on", "On")
                                                    : i18nc("@label a setting that is off", "Off")
                }
                ToggleSwitch {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    text: appCard.modelData.name
                    checked: appCard.modelData.enabled
                    onToggled: notifications.setAppOption(appCard.modelData.id, "enabled", checked)
                }
            }
        }

        EmptyState {
            Layout.fillWidth: true
            visible: notifications.apps.length === 0
            title: i18nc("@info", "No apps yet")
            message: i18nc("@info", "Apps show up here after they send their first notification.")
        }
    }

    Loader {
        anchors.fill: parent
        active: root.appId !== ""
        sourceComponent: NotificationsAppPage {
            settings: notifications
            appId: root.appId
            onClosed: root.appId = ""
        }
    }
}
