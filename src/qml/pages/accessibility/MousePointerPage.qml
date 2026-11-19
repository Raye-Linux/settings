// SPDX-License-Identifier: GPL-3.0-or-later
// Accessibility > Mouse pointer and touch: the pointer's style and size,
// and shaking the mouse to make the pointer big for a moment.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accessibility"), target: "accessibility" },
             { text: i18nc("@title settings page", "Mouse pointer and touch") }]

    PointerSettings { id: pointer }
    InputDeviceSettings { id: touchScreens; kind: "touchscreen" }

    // the size is applied once the slider rests: the desktop redraws
    // every pointer at each change
    Timer {
        id: sizeDelay
        interval: 400
        onTriggered: pointer.size = sizeCard.value
    }

    Card {
        iconName: "org.raye.settings-mouse-pointer"
        iconFallback: "cursor-arrow"
        title: i18n("Mouse pointer style")
        description: pointer.themes.length > 4 ? i18n("More styles are in the list") : ""
        searchId: "pointer-style"

        Row {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            spacing: Theme.s2

            Repeater {
                model: pointer.themes.slice(0, 4)
                PointerStyleTile {
                    required property var modelData
                    name: modelData.name
                    preview: modelData.preview
                    chosen: modelData.id === pointer.theme
                    onClicked: pointer.theme = modelData.id
                }
            }
            ComboBox {
                anchors.verticalCenter: parent.verticalCenter
                visible: pointer.themes.length > 4
                model: pointer.themes
                textRole: "name"
                valueRole: "id"
                currentIndex: indexOfValue(pointer.theme)
                Accessible.name: i18n("Mouse pointer style") + ", " + displayText
                // the list is not there yet at the first binding: look again
                Component.onCompleted: currentIndex = indexOfValue(pointer.theme)
                onActivated: (index) => pointer.theme = currentValue
            }
        }
    }

    SliderCard {
        id: sizeCard
        iconName: "org.raye.settings-mouse-pointer"
        iconFallback: "zoom-in"
        title: i18n("Size")
        searchId: "pointer-size"
        from: 24
        to: 96
        stepSize: 8
        value: pointer.size
        textFromValue: (v) => i18nc("@label pointer size in pixels", "%1 px", Math.round(v))
        onMoved: sizeDelay.restart()
    }

    ToggleCard {
        iconName: "org.raye.settings-mouse-pointer"
        iconFallback: "cursor-arrow"
        title: i18n("Shake the mouse pointer to find it")
        description: i18n("The pointer grows for a moment when you move the mouse quickly back and forth")
        searchId: "shake-to-find"
        checked: pointer.shakeToFind
        onToggled: pointer.shakeToFind = checked
    }

    ToggleCard {
        iconName: "org.raye.settings-mouse-pointer"
        iconFallback: "cursor-arrow"
        title: i18n("Show the location of the pointer when I press the Ctrl key")
        description: i18n("Rings circle the pointer while you hold Ctrl")
        searchId: "ctrl-to-find"
        checked: pointer.ctrlToFind
        onToggled: pointer.ctrlToFind = checked
    }

    ToggleCard {
        iconName: "org.raye.settings-mouse-pointer"
        iconFallback: "input-touchscreen"
        title: i18n("Touch indicator")
        description: i18n("Show a circle on the screen where I touch it")
        searchId: "touch-indicator"
        visible: touchScreens.deviceNames.length > 0 || pointer.touchIndicator
        checked: pointer.touchIndicator
        onToggled: pointer.touchIndicator = checked
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-mouse"
        iconFallback: "input-mouse"
        title: i18n("Mouse")
        description: i18n("Buttons, pointer speed, scrolling")
        target: "devices/mouse"
    }
    NavCard {
        iconName: "org.raye.settings-touchpad"
        iconFallback: "input-touchpad"
        title: i18n("Touchpad")
        description: i18n("Taps, gestures, scrolling")
        target: "devices/touchpad"
        visible: Router.isAvailable("devices/touchpad")
    }
}
