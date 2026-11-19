// SPDX-License-Identifier: GPL-3.0-or-later
// Accessibility > Keyboard: sticky keys, filter keys, toggle keys and
// the on-screen keyboard, named and grouped as Windows does.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accessibility"), target: "accessibility" },
             { text: i18nc("@title settings page", "Keyboard") }]

    AccessSettings { id: access }

    Connections {
        target: Router
        function onAnchorRequested(anchor) {
            if (["sticky-shortcut", "sticky-lock", "sticky-auto-off", "sticky-sound"].indexOf(anchor) >= 0)
                sticky.expanded = true
            else if (["slow-keys", "bounce-keys"].indexOf(anchor) >= 0)
                filter.expanded = true
        }
    }

    Expander {
        id: sticky
        iconName: "org.raye.settings-accessibility-keyboard"
        iconFallback: "input-keyboard"
        title: i18n("Sticky keys")
        description: i18n("Press one key at a time for keyboard shortcuts")
        searchId: "sticky-keys"

        headerContent: [
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: stickySwitch.checked ? i18nc("@label a setting that is on", "On") : i18nc("@label a setting that is off", "Off")
                font: Theme.body
                color: Theme.text
                Accessible.ignored: true
            },
            ToggleSwitch {
                id: stickySwitch
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Sticky keys")
                checked: access.stickyKeys
                onToggled: access.stickyKeys = checked
            }
        ]

        ExpanderRow {
            title: i18n("Keyboard shortcut for sticky keys")
            description: i18n("Press the Shift key five times to turn sticky keys on or off")
            searchId: "sticky-shortcut"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Keyboard shortcut for sticky keys")
                checked: access.gestures
                onToggled: access.gestures = checked
            }
        }
        ExpanderRow {
            title: i18n("Lock shortcut keys when pressed twice in a row")
            searchId: "sticky-lock"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Lock shortcut keys when pressed twice in a row")
                checked: access.stickyKeysLatch
                onToggled: access.stickyKeysLatch = checked
            }
        }
        ExpanderRow {
            title: i18n("Turn off sticky keys when two keys are pressed at the same time")
            searchId: "sticky-auto-off"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Turn off sticky keys when two keys are pressed at the same time")
                checked: access.stickyKeysAutoOff
                onToggled: access.stickyKeysAutoOff = checked
            }
        }
        ExpanderRow {
            title: i18n("Play a sound when shortcut keys are pressed and released")
            searchId: "sticky-sound"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Play a sound when shortcut keys are pressed and released")
                checked: access.stickyKeysBeep
                onToggled: access.stickyKeysBeep = checked
            }
        }
    }

    Expander {
        id: filter
        iconName: "org.raye.settings-accessibility-keyboard"
        iconFallback: "input-keyboard"
        title: i18n("Filter keys")
        description: i18n("Ignore brief or repeated keystrokes")
        searchId: "filter-keys"

        headerContent: [
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: filterSwitch.checked ? i18nc("@label a setting that is on", "On") : i18nc("@label a setting that is off", "Off")
                font: Theme.body
                color: Theme.text
                Accessible.ignored: true
            },
            ToggleSwitch {
                id: filterSwitch
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Filter keys")
                checked: access.filterKeys
                onToggled: access.filterKeys = checked
            }
        ]

        ExpanderRow {
            title: i18n("Ignore quick keystrokes")
            description: i18n("A key counts only when it is held down for a moment")
            searchId: "slow-keys"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Ignore quick keystrokes")
                checked: access.slowKeys
                onToggled: access.slowKeys = checked
            }
        }
        ExpanderRow {
            title: i18n("Ignore repeated keystrokes")
            description: i18n("A key pressed again right away counts once")
            searchId: "bounce-keys"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Ignore repeated keystrokes")
                checked: access.bounceKeys
                onToggled: access.bounceKeys = checked
            }
        }
    }

    ToggleCard {
        iconName: "org.raye.settings-accessibility-keyboard"
        iconFallback: "input-keyboard"
        title: i18n("Toggle keys")
        description: i18n("Play a sound whenever you press Caps lock, Num lock or Scroll lock")
        searchId: "toggle-keys"
        checked: access.toggleKeys
        onToggled: access.toggleKeys = checked
    }

    ToggleCard {
        iconName: "org.raye.settings-accessibility-keyboard"
        iconFallback: "input-keyboard-virtual"
        title: i18n("On-screen keyboard")
        description: access.onScreenKeyboardAvailable
                     ? i18n("Type with the mouse or a touch screen")
                     : i18n("No on-screen keyboard is set up on this PC")
        searchId: "on-screen-keyboard"
        enabled: access.onScreenKeyboardAvailable
        checked: access.onScreenKeyboard
        onToggled: access.onScreenKeyboard = checked
    }

    ToggleCard {
        iconName: "org.raye.settings-accessibility-keyboard"
        iconFallback: "input-keyboard"
        title: i18n("Underline access keys")
        description: i18n("Access keys are underlined even when you aren't holding Alt")
        searchId: "underline-access-keys"
        checked: access.underlineAccessKeys
        onToggled: access.underlineAccessKeys = checked
    }

    Expander {
        iconName: "org.raye.settings-notifications"
        iconFallback: "preferences-desktop-notification"
        title: i18n("Notification preferences")
        description: i18n("What happens when a keyboard shortcut turns sticky, filter or toggle keys on")
        searchId: "shortcut-notifications"

        ExpanderRow {
            searchId: "shortcut-warning"
            title: i18n("Show a warning message when turning a setting on with a shortcut")
            NotificationsCheckBox {
                text: i18n("Show a warning message when turning a setting on with a shortcut")
                contentItem: Item {}
                checked: access.gestureConfirmation
                onToggled: access.gestureConfirmation = checked
            }
        }
        ExpanderRow {
            searchId: "shortcut-sound"
            title: i18n("Make a sound when turning a setting on or off with a shortcut")
            NotificationsCheckBox {
                text: i18n("Make a sound when turning a setting on or off with a shortcut")
                contentItem: Item {}
                checked: access.gestureBeep
                onToggled: access.gestureBeep = checked
            }
        }
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-typing"
        iconFallback: "input-keyboard"
        title: i18n("Typing")
        description: i18n("Keyboard layouts and key repeat")
        target: "time/typing"
    }
}
