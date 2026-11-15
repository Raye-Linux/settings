// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > Touchpad. KWin applies each setting at once and remembers it.
// A touchpad that cannot do something has its row turned off rather than
// taken away, so the page looks the same on every laptop.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Bluetooth & devices"), target: "devices" },
             { text: i18nc("@title settings page", "Touchpad") }]

    InputDeviceSettings {
        id: input
        kind: "touchpad"
        onChanged: page.revision++
    }
    property int revision: 0
    function read(property, fallback) {
        page.revision
        return input.value(property, fallback)
    }
    function can(capability) {
        page.revision
        return input.supports(capability)
    }
    readonly property bool hasTouchpad: input.deviceNames.length > 0

    InfoBar {
        Layout.fillWidth: true
        open: !page.hasTouchpad
        closable: false
        severity: "warning"
        title: i18nc("@title", "No touchpad found")
        message: i18nc("@info", "Its settings show up here when this PC has a touchpad and you are on its desktop.")
    }

    ToggleCard {
        searchId: "touchpad"
        iconName: "org.raye.settings-touchpad"
        iconFallback: "input-touchpad"
        title: i18nc("@title", "Touchpad")
        enabled: page.hasTouchpad
        checked: page.read("enabled", true)
        onToggled: input.setValue("enabled", checked)
    }

    ToggleCard {
        searchId: "keep-on-with-mouse"
        iconName: "org.raye.settings-mouse"
        iconFallback: "input-mouse"
        title: i18nc("@title", "Leave touchpad on when a mouse is connected")
        enabled: page.can("supportsDisableEventsOnExternalMouse")
        checked: !page.read("disableEventsOnExternalMouse", false)
        onToggled: input.setValue("disableEventsOnExternalMouse", !checked)
    }

    SliderCard {
        searchId: "cursor-speed"
        iconName: "org.raye.settings-mouse-pointer"
        iconFallback: "input-touchpad"
        title: i18nc("@title", "Cursor speed")
        enabled: page.can("supportsPointerAcceleration")
        from: -1
        to: 1
        stepSize: 0.1
        value: page.read("pointerAcceleration", 0)
        textFromValue: (v) => Math.round((v + 1) * 5 + 1).toString()
        onMoved: input.setValue("pointerAcceleration", value)
    }

    ToggleCard {
        searchId: "disable-while-typing"
        iconName: "org.raye.settings-touchpad"
        iconFallback: "input-touchpad"
        title: i18nc("@title", "Turn the touchpad off while I type")
        enabled: page.can("supportsDisableWhileTyping")
        checked: page.read("disableWhileTyping", false)
        onToggled: input.setValue("disableWhileTyping", checked)
    }

    SectionHeader { text: i18nc("@title:group", "Taps") }

    ToggleCard {
        searchId: "tap-to-click"
        iconName: "org.raye.settings-touchpad"
        iconFallback: "input-touchpad"
        title: i18nc("@title", "Tap with a single finger to click")
        enabled: page.revision >= 0 && page.read("tapFingerCount", 0) > 0
        checked: page.read("tapToClick", false)
        onToggled: input.setValue("tapToClick", checked)
    }

    ComboCard {
        searchId: "two-finger-tap"
        iconName: "org.raye.settings-touchpad"
        iconFallback: "input-touchpad"
        title: i18nc("@title", "Tap with two fingers to")
        enabled: page.can("supportsLmrTapButtonMap")
        model: [i18nc("@item:inlistbox", "Right-click"), i18nc("@item:inlistbox", "Middle-click")]
        currentIndex: page.read("lmrTapButtonMap", false) ? 1 : 0
        onActivated: (index) => input.setValue("lmrTapButtonMap", index === 1)
    }

    ToggleCard {
        searchId: "tap-and-drag"
        iconName: "org.raye.settings-touchpad"
        iconFallback: "input-touchpad"
        title: i18nc("@title", "Tap twice and drag to select several items")
        enabled: page.read("tapFingerCount", 0) > 0
        checked: page.read("tapAndDrag", false)
        onToggled: input.setValue("tapAndDrag", checked)
    }

    SectionHeader { text: i18nc("@title:group", "Scroll") }

    ComboCard {
        searchId: "scroll-method"
        iconName: "org.raye.settings-touchpad"
        iconFallback: "input-touchpad"
        title: i18nc("@title", "Scroll with")
        enabled: page.can("supportsScrollTwoFinger") && page.can("supportsScrollEdge")
        model: [i18nc("@item:inlistbox", "Two fingers"), i18nc("@item:inlistbox", "The edge of the touchpad")]
        currentIndex: page.read("scrollEdge", false) ? 1 : 0
        onActivated: (index) => {
            input.setValue(index === 1 ? "scrollEdge" : "scrollTwoFinger", true)
            input.reload()
        }
    }

    ComboCard {
        searchId: "scroll-direction"
        iconName: "org.raye.settings-touchpad"
        iconFallback: "input-touchpad"
        title: i18nc("@title", "Scrolling direction")
        enabled: page.can("supportsNaturalScroll")
        model: [i18nc("@item:inlistbox", "Down motion scrolls down"), i18nc("@item:inlistbox", "Down motion scrolls up")]
        currentIndex: page.read("naturalScroll", false) ? 1 : 0
        onActivated: (index) => input.setValue("naturalScroll", index === 1)
    }
}
