// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Taskbar: the buttons on the taskbar, the tray icons
// and how the taskbar behaves, in the order Windows shows them. Every
// change is made at once on the running taskbars.
//
// Switches and boxes follow `checked` rather than `toggled`: a screen
// reader's toggle action changes `checked` without a `toggled` signal.
// When the setting already says the same, nothing is written.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
             { text: i18nc("@title settings page", "Taskbar") }]

    TaskbarSettings { id: taskbar }

    // a search result on a row inside a closed expander opens it first
    Connections {
        target: Router
        function onAnchorRequested(anchor) {
            if (["search", "task-view", "widgets"].indexOf(anchor) >= 0)
                items.expanded = true
            else if (["taskbar-alignment", "auto-hide", "badges", "flashing", "all-displays",
                      "multi-display-apps", "show-desktop-corner", "combine"].indexOf(anchor) >= 0)
                behaviours.expanded = true
        }
    }

    InfoBar {
        Layout.fillWidth: true
        visible: taskbar.ready && !taskbar.available
        severity: "warning"
        title: i18n("Taskbar")
        message: i18n("Raye can't reach the taskbar right now. Your changes will apply once it's back.")
        closable: false
    }

    Expander {
        id: items
        expanded: true
        searchId: "taskbar-items"
        iconName: "org.raye.settings-taskbar"
        iconFallback: "preferences-desktop"
        title: i18n("Taskbar items")
        description: i18n("Show or hide buttons that appear on the taskbar")

        ExpanderRow {
            searchId: "search"
            title: i18n("Search")
            enabled: taskbar.available
            // Raye's Start draws all four styles, the stock one only the icon
            ComboBox {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                visible: taskbar.rayeStart
                readonly property var modes: ["hidden", "icon", "iconlabel", "box"]
                model: [i18nc("@item search on the taskbar", "Hide"), i18nc("@item search on the taskbar", "Search icon only"),
                        i18nc("@item search on the taskbar", "Search icon and label"), i18nc("@item search on the taskbar", "Search box")]
                currentIndex: Math.max(0, modes.indexOf(taskbar.search))
                Accessible.name: i18n("Search") + ", " + displayText
                onActivated: (index) => taskbar.search = modes[index]
            }
            ComboBox {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                visible: !taskbar.rayeStart
                readonly property var modes: ["hidden", "icon"]
                model: [i18nc("@item search on the taskbar", "Hide"), i18nc("@item search on the taskbar", "Search icon only")]
                currentIndex: Math.max(0, modes.indexOf(taskbar.search))
                Accessible.name: i18n("Search") + ", " + displayText
                onActivated: (index) => taskbar.search = modes[index]
            }
        }
        ExpanderRow {
            searchId: "task-view"
            title: i18n("Task view")
            enabled: taskbar.available
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: taskbar.taskView ? i18nc("@label a setting that is on", "On") : i18nc("@label a setting that is off", "Off")
                font: Theme.body
                color: parent && parent.enabled ? Theme.text : Theme.textDisabled
                Accessible.ignored: true
            }
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Task view")
                checked: taskbar.taskView
                onCheckedChanged: if (checked !== taskbar.taskView) taskbar.taskView = checked
            }
        }
    }

    Expander {
        id: trayIcons
        searchId: "system-tray-icons"
        iconName: "org.raye.settings-taskbar"
        iconFallback: "preferences-desktop"
        title: i18n("System tray icons")
        description: i18n("Show or hide system tray icons")

        Repeater {
            model: taskbar.systemIcons
            delegate: ExpanderRow {
                required property var modelData
                title: modelData.name
                enabled: taskbar.available
                ToggleSwitch {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    text: modelData.name
                    checked: modelData.on
                    onCheckedChanged: if (checked !== modelData.on) taskbar.setSystemIcon(modelData.id, checked)
                }
            }
        }
    }

    Expander {
        id: otherIcons
        searchId: "other-tray-icons"
        iconName: "org.raye.settings-taskbar"
        iconFallback: "preferences-desktop"
        title: i18n("Other system tray icons")
        description: i18n("Show or hide additional system tray icons")

        ExpanderRow {
            visible: taskbar.otherIcons.length === 0
            title: i18n("Apps that put an icon in the system tray show up here.")
        }
        Repeater {
            model: taskbar.otherIcons
            delegate: ExpanderRow {
                required property var modelData
                title: modelData.name
                description: modelData.on ? i18n("Always on the taskbar") : i18n("In the hidden icons menu")
                enabled: taskbar.available
                ToggleSwitch {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    text: modelData.name
                    checked: modelData.on
                    onCheckedChanged: if (checked !== modelData.on) taskbar.setOtherIcon(modelData.id, checked)
                }
            }
        }
    }

    Expander {
        id: behaviours
        searchId: "taskbar-behaviors"
        iconName: "org.raye.settings-taskbar"
        iconFallback: "preferences-desktop"
        title: i18n("Taskbar behaviors")
        description: i18n("Taskbar alignment, badges, automatically hide, and multiple displays")

        ExpanderRow {
            searchId: "taskbar-alignment"
            title: i18n("Taskbar alignment")
            description: taskbar.alignmentFixed ? i18nc("@info", "Always on the left in the Windows 10 style") : ""
            enabled: taskbar.available && !taskbar.alignmentFixed
            ComboBox {
                objectName: "alignmentChoice"
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                readonly property var modes: ["center", "left"]
                model: [i18nc("@item taskbar alignment", "Center"), i18nc("@item taskbar alignment", "Left")]
                currentIndex: Math.max(0, modes.indexOf(taskbar.alignment))
                Accessible.name: i18n("Taskbar alignment") + ", " + displayText
                onActivated: (index) => taskbar.alignment = modes[index]
            }
        }
        ExpanderRow {
            searchId: "auto-hide"
            title: i18nc("@option:check", "Automatically hide the taskbar")
            enabled: taskbar.available
            NotificationsCheckBox {
                text: i18nc("@option:check", "Automatically hide the taskbar")
                contentItem: Item {}
                checked: taskbar.autoHide
                onCheckedChanged: if (checked !== taskbar.autoHide) taskbar.autoHide = checked
            }
        }
        ExpanderRow {
            searchId: "badges"
            title: i18nc("@option:check", "Show badges on taskbar apps")
            description: taskbar.rayeTasks ? "" : i18n("Needs the Raye taskbar. Apps that count unread items show the count.")
            enabled: taskbar.available && taskbar.rayeTasks
            NotificationsCheckBox {
                text: i18nc("@option:check", "Show badges on taskbar apps")
                contentItem: Item {}
                checked: !taskbar.rayeTasks || taskbar.badges
                onCheckedChanged: if (taskbar.rayeTasks && checked !== taskbar.badges) taskbar.badges = checked
            }
        }
        ExpanderRow {
            searchId: "flashing"
            title: i18nc("@option:check", "Show flashing on taskbar apps")
            description: taskbar.rayeTasks ? "" : i18n("Needs the Raye taskbar. An app that needs you lights up its button.")
            enabled: taskbar.available && taskbar.rayeTasks
            NotificationsCheckBox {
                text: i18nc("@option:check", "Show flashing on taskbar apps")
                contentItem: Item {}
                checked: !taskbar.rayeTasks || taskbar.flashing
                onCheckedChanged: if (taskbar.rayeTasks && checked !== taskbar.flashing) taskbar.flashing = checked
            }
        }
        ExpanderRow {
            searchId: "all-displays"
            title: i18nc("@option:check", "Show my taskbar on all displays")
            description: taskbar.severalDisplays ? "" : i18n("Connect another display to use this.")
            enabled: taskbar.severalDisplays
            NotificationsCheckBox {
                text: i18nc("@option:check", "Show my taskbar on all displays")
                contentItem: Item {}
                checked: taskbar.allDisplays
                onCheckedChanged: if (checked !== taskbar.allDisplays) taskbar.allDisplays = checked
            }
        }
        ExpanderRow {
            searchId: "multi-display-apps"
            title: i18n("When using multiple displays, show my taskbar apps on")
            enabled: taskbar.severalDisplays && taskbar.allDisplays
            ComboBox {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                readonly property var modes: ["all", "main-and-window", "window-only"]
                model: [i18nc("@item where taskbar buttons go", "All taskbars"),
                        i18nc("@item where taskbar buttons go", "Main taskbar and taskbar where window is open"),
                        i18nc("@item where taskbar buttons go", "Taskbar where window is open")]
                currentIndex: Math.max(0, modes.indexOf(taskbar.multiDisplayButtons))
                Accessible.name: i18n("When using multiple displays, show my taskbar apps on") + ", " + displayText
                onActivated: (index) => taskbar.multiDisplayButtons = modes[index]
            }
        }
        ExpanderRow {
            searchId: "show-desktop-corner"
            title: i18nc("@option:check", "Select the far corner of the taskbar to show the desktop")
            enabled: taskbar.available
            NotificationsCheckBox {
                text: i18nc("@option:check", "Select the far corner of the taskbar to show the desktop")
                contentItem: Item {}
                checked: taskbar.showDesktopCorner
                onCheckedChanged: if (checked !== taskbar.showDesktopCorner) taskbar.showDesktopCorner = checked
            }
        }
        ExpanderRow {
            searchId: "combine"
            title: i18n("Combine taskbar buttons and hide labels")
            enabled: taskbar.available
            ComboBox {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                readonly property var modes: ["always", "full", "never"]
                model: [i18nc("@item combine taskbar buttons", "Always"),
                        i18nc("@item combine taskbar buttons", "When taskbar is full"),
                        i18nc("@item combine taskbar buttons", "Never")]
                currentIndex: Math.max(0, modes.indexOf(taskbar.combine))
                Accessible.name: i18n("Combine taskbar buttons and hide labels") + ", " + displayText
                onActivated: (index) => taskbar.combine = modes[index]
            }
        }
        ExpanderRow {
            searchId: "small-buttons"
            title: i18n("Show smaller taskbar buttons")
            description: taskbar.rayeTasks ? "" : i18n("Needs the Raye taskbar.")
            enabled: taskbar.available && taskbar.rayeTasks
            ComboBox {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                readonly property var modes: ["always", "full", "never"]
                model: [i18nc("@item smaller taskbar buttons", "Always"),
                        i18nc("@item smaller taskbar buttons", "When taskbar is full"),
                        i18nc("@item smaller taskbar buttons", "Never")]
                currentIndex: Math.max(0, modes.indexOf(taskbar.smallButtons))
                Accessible.name: i18n("Show smaller taskbar buttons") + ", " + displayText
                onActivated: (index) => taskbar.smallButtons = modes[index]
            }
        }
        ExpanderRow {
            searchId: "recent-searches"
            title: i18nc("@option:check", "Show recent searches when I open search")
            description: taskbar.rayeStart ? "" : i18n("Needs the Raye Start menu.")
            enabled: taskbar.available && taskbar.rayeStart
            NotificationsCheckBox {
                text: i18nc("@option:check", "Show recent searches when I open search")
                contentItem: Item {}
                checked: !taskbar.rayeStart || taskbar.recentSearches
                onCheckedChanged: if (taskbar.rayeStart && checked !== taskbar.recentSearches) taskbar.recentSearches = checked
            }
        }
    }
}
