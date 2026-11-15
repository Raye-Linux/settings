// SPDX-License-Identifier: GPL-3.0-or-later
// System > Multitasking: snap windows, desktops and Alt+Tab, in the
// order Windows shows them. Every change applies at once.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Multitasking") }]

    MultitaskingSettings { id: settings }

    // a search result on a row inside a closed expander opens it first
    Connections {
        target: Router
        function onAnchorRequested(anchor) {
            if (["snap-layouts-hover", "snap-bar", "snap-assist"].indexOf(anchor) >= 0)
                snap.expanded = true
            else if (["taskbar-desktops", "alt-tab-desktops", "virtual-desktops"].indexOf(anchor) >= 0)
                desktops.expanded = true
        }
    }

    Expander {
        id: snap
        iconName: "org.raye.settings-multitasking"
        iconFallback: "preferences-system-windows-effect-presentwindows"
        title: i18n("Snap windows")
        description: i18n("Put windows side by side by dragging them to an edge or a corner of the screen")
        searchId: "snap-windows"

        headerContent: [
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: snapSwitch.checked ? i18nc("@label a setting that is on", "On") : i18nc("@label a setting that is off", "Off")
                font: Theme.body
                color: Theme.text
                Accessible.ignored: true
            },
            ToggleSwitch {
                id: snapSwitch
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Snap windows")
                checked: settings.snapWindows
                onToggled: settings.snapWindows = checked
            }
        ]

        ExpanderRow {
            title: i18n("Show snap layouts when I hover over a window's maximize button")
            searchId: "snap-layouts-hover"
            enabled: settings.snapWindows
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Show snap layouts when I hover over a window's maximize button")
                checked: settings.snapLayoutsOnHover
                onToggled: settings.snapLayoutsOnHover = checked
            }
        }
        ExpanderRow {
            title: i18n("Show snap layouts when I drag a window to the top of my screen")
            searchId: "snap-bar"
            enabled: settings.snapWindows
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Show snap layouts when I drag a window to the top of my screen")
                checked: settings.snapBar
                onToggled: settings.snapBar = checked
            }
        }
        ExpanderRow {
            title: i18n("When I snap a window, suggest what I can snap next to it")
            searchId: "snap-assist"
            enabled: settings.snapWindows
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("When I snap a window, suggest what I can snap next to it")
                checked: settings.snapAssist
                onToggled: settings.snapAssist = checked
            }
        }
    }

    Expander {
        id: desktops
        iconName: "org.raye.settings-multitasking"
        iconFallback: "preferences-desktop-virtual"
        title: i18n("Desktops")
        description: i18n("Choose what the taskbar and Alt+Tab show when you use more than one desktop")
        searchId: "desktops"

        ExpanderRow {
            title: i18n("On the taskbar, show all the open windows")
            searchId: "taskbar-desktops"
            // until the taskbar reads this setting, the row would do nothing
            visible: settings.taskbarAllDesktopsAvailable
            ComboBox {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                model: [i18n("Only on the desktop I'm using"), i18n("On all desktops")]
                currentIndex: settings.taskbarAllDesktops ? 1 : 0
                Accessible.name: i18n("On the taskbar, show all the open windows") + ", " + displayText
                onActivated: (index) => settings.taskbarAllDesktops = index === 1
            }
        }
        ExpanderRow {
            title: i18n("Show all open windows when I press Alt+Tab")
            searchId: "alt-tab-desktops"
            ComboBox {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                model: [i18n("Only on the desktop I'm using"), i18n("On all desktops")]
                currentIndex: settings.altTabAllDesktops ? 1 : 0
                Accessible.name: i18n("Show all open windows when I press Alt+Tab") + ", " + displayText
                onActivated: (index) => settings.altTabAllDesktops = index === 1
            }
        }
        ExpanderRow {
            title: i18n("Add, rename and order your desktops")
            searchId: "virtual-desktops"
            clickable: true
            actionGlyph: "open-external"
            onClicked: settings.openDesktops()
        }
    }

    ComboCard {
        iconName: "org.raye.settings-multitasking"
        iconFallback: "preferences-system-windows-effect-presentwindows"
        title: i18n("Alt+Tab")
        description: i18n("Choose what you see when you press Alt+Tab")
        searchId: "alt-tab"
        model: [i18n("Open windows only"), i18n("Open windows and the desktop")]
        currentIndex: settings.altTabShowsDesktop ? 1 : 0
        onActivated: (index) => settings.altTabShowsDesktop = index === 1
    }
}
