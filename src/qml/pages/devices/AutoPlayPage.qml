// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > AutoPlay: one switch for everything, then what to do with a
// USB drive and with a memory card. The taskbar's devices applet reads
// the choices when something is plugged in.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Bluetooth & devices"), target: "devices" },
             { text: i18nc("@title settings page", "AutoPlay") }]

    AutoPlaySettings { id: autoPlay }

    readonly property var choices: [
        { text: i18nc("@item:inlistbox AutoPlay", "Ask me every time"), value: "ask" },
        { text: i18nc("@item:inlistbox AutoPlay", "Open folder to view files"), value: "open" },
        { text: i18nc("@item:inlistbox AutoPlay", "Take no action"), value: "nothing" }
    ]
    function indexOf(value) {
        return Math.max(0, choices.findIndex(c => c.value === value))
    }

    ToggleCard {
        searchId: "autoplay"
        iconName: "org.raye.settings-autoplay"
        iconFallback: "media-removable"
        title: i18nc("@title", "Use AutoPlay for all media and devices")
        checked: autoPlay.enabled
        onToggled: autoPlay.enabled = checked
    }

    SectionHeader { text: i18nc("@title:group", "Choose AutoPlay defaults") }

    ComboCard {
        searchId: "removable-drive"
        iconName: "drive-removable-media-usb"
        iconFallback: "media-removable"
        title: i18nc("@title", "Removable drive")
        enabled: autoPlay.enabled
        model: page.choices
        textRole: "text"
        valueRole: "value"
        currentIndex: page.indexOf(autoPlay.removableDrive)
        onActivated: (index) => autoPlay.removableDrive = page.choices[index].value
    }

    ComboCard {
        searchId: "memory-card"
        iconName: "media-flash-sd-mmc"
        iconFallback: "media-flash"
        title: i18nc("@title", "Memory card")
        enabled: autoPlay.enabled
        model: page.choices
        textRole: "text"
        valueRole: "value"
        currentIndex: page.indexOf(autoPlay.memoryCard)
        onActivated: (index) => autoPlay.memoryCard = page.choices[index].value
    }
}
