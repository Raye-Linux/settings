// SPDX-License-Identifier: GPL-3.0-or-later
// System > Performance options: the visual effects a slow PC feels, and
// one choice that turns them all off (or back on) at once. Every change
// applies at once and stays after a restart.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Performance options") }]

    PerformanceSettings { id: perf }

    // the order of the list below
    readonly property var modes: ["auto", "appearance", "performance", "custom"]

    ComboCard {
        objectName: "visualEffectsMode"
        Layout.fillWidth: true
        iconName: "org.raye.settings-performance"
        iconFallback: "speedometer"
        title: i18nc("@label:listbox", "Visual effects")
        description: i18nc("@info", "Choose how Raye balances looks and speed on this PC")
        searchId: "visual-effects-mode"
        textRole: "text"
        valueRole: "value"
        model: [{ text: i18nc("@item:inlistbox visual effects", "Let Raye choose what's best for this PC"), value: "auto" },
                { text: i18nc("@item:inlistbox visual effects", "Adjust for best appearance"), value: "appearance" },
                { text: i18nc("@item:inlistbox visual effects", "Adjust for best performance"), value: "performance" },
                { text: i18nc("@item:inlistbox visual effects", "Custom"), value: "custom" }]
        currentIndex: Math.max(0, page.modes.indexOf(perf.mode))
        onActivated: {
            perf.setMode(currentValue)
            currentIndex = Qt.binding(() => Math.max(0, page.modes.indexOf(perf.mode)))
        }
    }

    InfoBar {
        Layout.fillWidth: true
        visible: perf.mode === "auto" && perf.lowEnd
        closable: false
        severity: "informational"
        title: i18nc("@info", "Effects are off to keep this PC fast")
        message: i18nc("@info", "This PC draws the screen without much help from its graphics. Turn effects back on below if you like.")
    }

    SectionHeader { text: i18nc("@title:group", "Effects") }

    Repeater {
        model: [
            { id: "animations", prop: "animations", child: false,
              title: i18nc("@label", "Animate controls and elements inside windows"),
              description: i18nc("@info", "Turn this off to stop every animation at once") },
            { id: "window-open", prop: "windowOpen", child: true,
              title: i18nc("@label", "Animate windows when they open and close"),
              description: "" },
            { id: "minimize", prop: "minimize", child: true,
              title: i18nc("@label", "Animate windows when minimizing and maximizing"),
              description: "" },
            { id: "menus", prop: "menus", child: true,
              title: i18nc("@label", "Fade or slide menus and flyouts into view"),
              description: "" },
            { id: "desktops", prop: "desktops", child: true,
              title: i18nc("@label", "Slide when switching desktops"),
              description: "" },
            { id: "transparency", prop: "transparency", child: false,
              title: i18nc("@label", "Transparency effects"),
              description: i18nc("@info", "Blur and see-through surfaces on the taskbar, Start, menus and windows") },
            { id: "shadows", prop: "shadows", child: false,
              title: i18nc("@label", "Show shadows under windows"),
              description: i18nc("@info", "Also the shadows under menus") }
        ]
        delegate: ToggleCard {
            required property var modelData
            Layout.fillWidth: true
            title: modelData.title
            // with every animation off, the ones below have nothing to show
            enabled: !modelData.child || perf.animations
            description: modelData.child && !perf.animations
                         ? i18nc("@info", "Turn on animating controls and elements first") : modelData.description
            searchId: modelData.id
            checked: perf[modelData.prop]
            onToggled: {
                perf.setEffect(modelData.id, checked)
                checked = Qt.binding(() => perf[modelData.prop])
            }
        }
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-colors"
        iconFallback: "preferences-desktop-color"
        title: i18nc("@title settings page", "Colors")
        description: i18nc("@info", "Light or dark mode, accent color")
        target: "personalization/colors"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-power"
        iconFallback: "battery"
        title: i18nc("@title settings page", "Power & battery")
        description: i18nc("@info", "Sleep, battery usage, power mode")
        target: "system/power"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-game-mode"
        iconFallback: "applications-games"
        title: i18nc("@title settings page", "Game Mode")
        description: i18nc("@info", "Performance while you play")
        target: "gaming/game-mode"
    }
}
