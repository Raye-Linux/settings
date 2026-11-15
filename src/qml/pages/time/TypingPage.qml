// SPDX-License-Identifier: GPL-3.0-or-later
// Time & language > Typing: the keyboard layouts in use, how fast a held
// key repeats, and where to add a layout.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Time & language"), target: "time" },
             { text: i18nc("@title settings page", "Typing") }]

    KeyboardLayouts { id: keyboard }

    Connections {
        target: Router
        function onAnchorRequested(anchor) {
            if (anchor === "layout-options")
                layouts.expanded = true
        }
    }

    // a value is written once the slider rests, not at every step
    Timer {
        id: writeDelay
        interval: 300
        onTriggered: {
            keyboard.repeatDelay = delayCard.value
            keyboard.repeatRate = rateCard.value
        }
    }

    Expander {
        id: layouts
        iconName: "org.raye.settings-typing"
        iconFallback: "input-keyboard"
        title: i18n("Keyboard layouts")
        description: keyboard.layouts.map((l) => l.name).join(", ")
        searchId: "keyboard-layouts"
        expanded: true

        headerContent: [
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@action:button add a keyboard layout", "Add a keyboard")
                onClicked: keyboard.openLayouts()
            }
        ]

        Repeater {
            model: keyboard.layouts
            ExpanderRow {
                required property var modelData
                title: modelData.name
                Text {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    text: modelData.layout.toUpperCase()
                    font: Theme.captionStrong
                    color: Theme.textSecondary
                }
            }
        }
        ExpanderRow {
            title: i18n("Change the shortcut to switch layouts, and more keyboard options")
            searchId: "layout-options"
            clickable: true
            actionGlyph: "open-external"
            onClicked: keyboard.openLayouts()
        }
    }

    SectionHeader { text: i18n("Key repeat") }

    SliderCard {
        id: delayCard
        iconName: "org.raye.settings-typing"
        iconFallback: "input-keyboard"
        title: i18n("Repeat delay")
        description: i18n("How long to hold a key before it starts repeating")
        searchId: "repeat-delay"
        from: 100
        to: 1000
        stepSize: 50
        value: keyboard.repeatDelay
        textFromValue: (v) => i18nc("@label milliseconds", "%1 ms", Math.round(v))
        onMoved: writeDelay.restart()
    }

    SliderCard {
        id: rateCard
        iconName: "org.raye.settings-typing"
        iconFallback: "input-keyboard"
        title: i18n("Repeat rate")
        description: i18n("How fast a held key repeats")
        searchId: "repeat-rate"
        from: 2
        to: 50
        stepSize: 1
        value: keyboard.repeatRate
        textFromValue: (v) => i18nc("@label key repeats per second", "%1 per second", Math.round(v))
        onMoved: writeDelay.restart()
    }

    Card {
        iconName: "org.raye.settings-text-input"
        iconFallback: "edit-rename"
        title: i18n("Try it here")
        TextField {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            width: 240
            placeholderText: i18n("Hold a key down")
            Accessible.name: i18n("Try the key repeat here")
        }
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-accessibility-keyboard"
        iconFallback: "preferences-desktop-accessibility"
        title: i18n("Accessibility keyboard settings")
        description: i18n("Sticky keys, filter keys and the on-screen keyboard")
        target: "accessibility/keyboard"
    }
    NavCard {
        iconName: "org.raye.settings-language"
        iconFallback: "preferences-desktop-locale"
        title: i18n("Language & region")
        description: i18n("Display language, date and number formats")
        target: "time/language"
    }
}
