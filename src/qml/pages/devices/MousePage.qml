// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > Mouse: the settings Windows has, on every mouse plugged in.
// KWin applies each one at once and remembers it.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Bluetooth & devices"), target: "devices" },
             { text: i18nc("@title settings page", "Mouse") }]

    InputDeviceSettings {
        id: input
        kind: "mouse"
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
    readonly property bool hasMouse: input.deviceNames.length > 0

    InfoBar {
        Layout.fillWidth: true
        open: !page.hasMouse
        closable: false
        severity: "warning"
        title: input.available ? i18nc("@title", "No mouse found")
                               : i18nc("@title", "Mouse settings are not available here")
        message: input.available ? i18nc("@info", "Plug in a mouse or connect one over Bluetooth, and its settings show up here.")
                                 : i18nc("@info", "They can only be changed from a desktop session on this PC.")
    }

    ComboCard {
        searchId: "primary-button"
        iconName: "org.raye.settings-mouse"
        iconFallback: "input-mouse"
        title: i18nc("@title", "Primary mouse button")
        enabled: page.can("supportsLeftHanded")
        model: [i18nc("@item:inlistbox mouse button", "Left"), i18nc("@item:inlistbox mouse button", "Right")]
        currentIndex: page.read("leftHanded", false) ? 1 : 0
        onActivated: (index) => input.setValue("leftHanded", index === 1)
    }

    SliderCard {
        searchId: "pointer-speed"
        iconName: "org.raye.settings-mouse-pointer"
        iconFallback: "input-mouse"
        title: i18nc("@title", "Mouse pointer speed")
        description: page.hasMouse && !enabled ? i18nc("@info", "This mouse does not let its speed be changed.") : ""
        enabled: page.can("supportsPointerAcceleration")
        from: -1
        to: 1
        stepSize: 0.1
        value: page.read("pointerAcceleration", 0)
        textFromValue: (v) => Math.round((v + 1) * 5 + 1).toString()
        onMoved: input.setValue("pointerAcceleration", value)
    }

    ToggleCard {
        searchId: "pointer-precision"
        iconName: "org.raye.settings-mouse-pointer"
        iconFallback: "input-mouse"
        title: i18nc("@title", "Enhance pointer precision")
        description: i18nc("@info", "The pointer moves further the faster you move the mouse")
        enabled: page.can("supportsPointerAccelerationProfileAdaptive")
        checked: page.read("pointerAccelerationProfileAdaptive", false)
        onToggled: {
            input.setValue(checked ? "pointerAccelerationProfileAdaptive" : "pointerAccelerationProfileFlat", true)
            input.reload()
        }
    }

    SectionHeader { text: i18nc("@title:group", "Scrolling") }

    SliderCard {
        searchId: "scroll-speed"
        iconName: "org.raye.settings-mouse"
        iconFallback: "input-mouse"
        title: i18nc("@title", "Scroll speed")
        description: i18nc("@info", "How far one notch of the wheel scrolls")
        enabled: page.hasMouse
        from: 0.25
        to: 4
        stepSize: 0.25
        value: page.read("scrollFactor", 1)
        textFromValue: (v) => i18nc("@label %1 is a multiplier, like 1.5", "×%1", v.toLocaleString(Qt.locale(), "f", 2))
        onMoved: input.setValue("scrollFactor", value)
    }

    ComboCard {
        searchId: "scroll-direction"
        iconName: "org.raye.settings-mouse"
        iconFallback: "input-mouse"
        title: i18nc("@title", "Scrolling direction")
        enabled: page.can("supportsNaturalScroll")
        model: [i18nc("@item:inlistbox", "Down motion scrolls down"), i18nc("@item:inlistbox", "Down motion scrolls up")]
        currentIndex: page.read("naturalScroll", false) ? 1 : 0
        onActivated: (index) => input.setValue("naturalScroll", index === 1)
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }

    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-mouse-pointer"
        iconFallback: "input-mouse"
        title: i18nc("@title settings page", "Mouse pointer")
        description: i18nc("@info", "Pointer size and color")
        target: "accessibility/mouse-pointer"
    }
}
