// SPDX-License-Identifier: GPL-3.0-or-later
// Accessibility > Mouse: move the pointer with the number pad.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accessibility"), target: "accessibility" },
             { text: i18nc("@title settings page", "Mouse") }]

    AccessSettings { id: access }

    Connections {
        target: Router
        function onAnchorRequested(anchor) {
            if (["mouse-keys-speed", "mouse-keys-acceleration"].indexOf(anchor) >= 0)
                keys.expanded = true
        }
    }

    Expander {
        id: keys
        iconName: "org.raye.settings-accessibility-mouse"
        iconFallback: "input-mouse"
        title: i18n("Mouse keys")
        description: i18n("Use the number pad to move the mouse pointer")
        searchId: "mouse-keys"
        expanded: access.mouseKeys

        headerContent: [
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: keysSwitch.checked ? i18nc("@label a setting that is on", "On") : i18nc("@label a setting that is off", "Off")
                font: Theme.body
                color: Theme.text
                Accessible.ignored: true
            },
            ToggleSwitch {
                id: keysSwitch
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Mouse keys")
                checked: access.mouseKeys
                onToggled: access.mouseKeys = checked
            }
        ]

        ExpanderRow {
            title: i18n("Mouse keys speed")
            searchId: "mouse-keys-speed"
            Slider {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                width: Theme.sliderWidth
                from: 1
                to: 10
                stepSize: 1
                snapMode: Slider.SnapAlways
                value: access.mouseKeysSpeed
                Accessible.name: i18n("Mouse keys speed")
                onMoved: access.mouseKeysSpeed = Math.round(value)
            }
        }
        ExpanderRow {
            title: i18n("Mouse keys acceleration")
            description: i18n("How quickly the pointer reaches its top speed")
            searchId: "mouse-keys-acceleration"
            Slider {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                width: Theme.sliderWidth
                from: 1
                to: 10
                stepSize: 1
                snapMode: Slider.SnapAlways
                value: access.mouseKeysAcceleration
                Accessible.name: i18n("Mouse keys acceleration")
                onMoved: access.mouseKeysAcceleration = Math.round(value)
            }
        }
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-mouse-pointer"
        iconFallback: "cursor-arrow"
        title: i18n("Mouse pointer and touch")
        description: i18n("Pointer style and size")
        target: "accessibility/mouse-pointer"
    }
    NavCard {
        iconName: "org.raye.settings-mouse"
        iconFallback: "input-mouse"
        title: i18n("Mouse")
        description: i18n("Buttons, pointer speed, scrolling")
        target: "devices/mouse"
    }
}
