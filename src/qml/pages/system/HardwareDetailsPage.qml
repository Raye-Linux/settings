// SPDX-License-Identifier: GPL-3.0-or-later
// System › About › Hardware details: everything the system information
// tool knows, one card per part of the PC. Each opens the tool's own
// view of that part; a part whose view is not installed is not listed.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "About"), target: "system/about" },
             { text: i18nc("@title settings page", "Hardware details") }]

    DeviceInfo { id: device }

    // a part is listed only when its view is installed (and the battery
    // only on a PC that has one)
    function shown(module) {
        return device.hasDetails(module) && (module !== "kcm_energyinfo" || Router.has("battery"))
    }
    function anyShown(modules) {
        return modules.some(m => page.shown(m))
    }
    readonly property var allModules: ["kcm_cpu", "kcm_memory", "kcm_vulkan", "kcm_egl", "kcm_edid", "kcm_disks",
                                       "kcm_block_devices", "kcm_usb", "kcm_pci", "kcm_audio_information",
                                       "kcm_energyinfo", "kcm_firmware_security"]

    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        text: i18nc("@info", "Every detail about the parts of your PC, for when you need to look closer. Each one opens in a window of its own.")
    }

    SectionHeader {
        visible: page.anyShown(["kcm_cpu", "kcm_memory"])
        text: i18nc("@title", "Processor and memory")
    }

    Card {
        visible: page.shown("kcm_cpu")
        iconName: "org.raye.settings-hardware"
        iconFallback: "cpu"
        title: i18nc("@title", "Processor")
        description: device.processor !== "" ? device.processor : i18nc("@info", "Model, cores, speed and features")
        searchId: "hardware-processor"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_cpu")
    }

    Card {
        visible: page.shown("kcm_memory")
        iconName: "org.raye.settings-hardware"
        iconFallback: "memory"
        title: i18nc("@title", "Memory")
        description: device.memory !== "" ? device.memory : i18nc("@info", "How the memory is used")
        searchId: "hardware-memory"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_memory")
    }

    SectionHeader {
        visible: page.anyShown(["kcm_vulkan", "kcm_egl", "kcm_edid"])
        text: i18nc("@title", "Graphics and displays")
    }

    Card {
        visible: page.shown("kcm_vulkan")
        iconName: "org.raye.settings-graphics"
        iconFallback: "video-display"
        title: i18nc("@title", "Graphics (Vulkan)")
        description: device.graphics.length > 0 ? device.graphics.join(", ") : i18nc("@info", "What the graphics card can do for games")
        searchId: "hardware-vulkan"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_vulkan")
    }

    Card {
        visible: page.shown("kcm_egl")
        iconName: "org.raye.settings-graphics"
        iconFallback: "video-display"
        title: i18nc("@title", "Graphics (OpenGL)")
        description: i18nc("@info", "The drivers and versions apps draw with")
        searchId: "hardware-opengl"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_egl")
    }

    Card {
        visible: page.shown("kcm_edid")
        iconName: "org.raye.settings-display"
        iconFallback: "video-display"
        title: i18nc("@title", "Monitors")
        description: i18nc("@info", "What each monitor says about itself")
        searchId: "hardware-monitors"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_edid")
    }

    SectionHeader {
        visible: page.anyShown(["kcm_disks", "kcm_block_devices"])
        text: i18nc("@title", "Drives")
    }

    Card {
        visible: page.shown("kcm_disks")
        iconName: "org.raye.settings-storage"
        iconFallback: "drive-harddisk"
        title: i18nc("@title", "Drive health")
        description: i18nc("@info", "Warnings from your drives before they fail")
        searchId: "hardware-drive-health"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_disks")
    }

    Card {
        visible: page.shown("kcm_block_devices")
        iconName: "org.raye.settings-storage"
        iconFallback: "drive-harddisk"
        title: i18nc("@title", "Drives and partitions")
        description: device.disks.length > 0 ? device.disks.join(", ") : i18nc("@info", "Every drive and how it is divided")
        searchId: "hardware-drives"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_block_devices")
    }

    SectionHeader {
        visible: page.anyShown(["kcm_usb", "kcm_pci", "kcm_audio_information", "kcm_energyinfo"])
        text: i18nc("@title", "Devices")
    }

    Card {
        visible: page.shown("kcm_usb")
        iconName: "org.raye.settings-devices"
        iconFallback: "drive-removable-media-usb"
        title: i18nc("@title", "USB devices")
        description: i18nc("@info", "Everything plugged into a USB port")
        searchId: "hardware-usb"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_usb")
    }

    Card {
        visible: page.shown("kcm_pci")
        iconName: "org.raye.settings-devices"
        iconFallback: "preferences-system-hardware"
        title: i18nc("@title", "Internal devices")
        description: i18nc("@info", "Cards and chips inside the PC")
        searchId: "hardware-pci"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_pci")
    }

    Card {
        visible: page.shown("kcm_audio_information")
        iconName: "org.raye.settings-sound-devices"
        iconFallback: "audio-card"
        title: i18nc("@title", "Sound devices")
        description: i18nc("@info", "Sound cards, outputs and inputs")
        searchId: "hardware-sound"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_audio_information")
    }

    Card {
        visible: page.shown("kcm_energyinfo")
        iconName: "org.raye.settings-power"
        iconFallback: "battery"
        title: i18nc("@title", "Battery")
        description: i18nc("@info", "Charge, health and use of the battery")
        searchId: "hardware-battery"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_energyinfo")
    }

    SectionHeader {
        visible: page.anyShown(["kcm_firmware_security"])
        text: i18nc("@title", "Security")
    }

    Card {
        visible: page.shown("kcm_firmware_security")
        iconName: "org.raye.settings-device-security"
        iconFallback: "security-high"
        title: i18nc("@title", "Firmware security")
        description: i18nc("@info", "How well the firmware of this PC protects it")
        searchId: "hardware-firmware"
        clickable: true
        actionGlyph: "open-external"
        onClicked: device.openDetails("kcm_firmware_security")
    }

    EmptyState {
        Layout.fillWidth: true
        visible: !page.anyShown(page.allModules)
        title: i18nc("@info", "No hardware details here")
        message: i18nc("@info", "The details of the parts of this PC can't be shown right now.")
    }
}
