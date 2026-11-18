// SPDX-License-Identifier: GPL-3.0-or-later
// Accessibility > Visual effects: see-through surfaces, animations, and
// how long notifications stay on screen.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accessibility"), target: "accessibility" },
             { text: i18nc("@title settings page", "Visual effects") }]

    VisualEffectsSettings { id: effects }

    ToggleCard {
        iconName: "org.raye.settings-visual-effects"
        iconFallback: "preferences-desktop-effects"
        title: i18n("Transparency effects")
        description: i18n("Make some windows and surfaces slightly see-through")
        searchId: "transparency-effects"
        checked: effects.transparency
        onToggled: effects.transparency = checked
    }

    ToggleCard {
        iconName: "org.raye.settings-visual-effects"
        iconFallback: "preferences-desktop-effects"
        title: i18n("Animation effects")
        description: i18n("Windows, menus and controls move smoothly when they open and close")
        searchId: "animation-effects"
        checked: effects.animations
        onToggled: effects.animations = checked
    }

    ComboCard {
        iconName: "org.raye.settings-notifications"
        iconFallback: "preferences-desktop-notification"
        title: i18n("Dismiss notifications after this amount of time")
        searchId: "dismiss-notifications"
        // until the shell reads this setting, the row would do nothing
        visible: effects.toastAvailable
        // the same times as VisualEffectsSettings::toastChoices
        model: [i18np("1 second", "%1 seconds", 5), i18np("1 second", "%1 seconds", 7),
                i18np("1 second", "%1 seconds", 15), i18np("1 second", "%1 seconds", 30),
                i18np("1 minute", "%1 minutes", 1), i18np("1 minute", "%1 minutes", 5)]
        currentIndex: effects.toastIndex(effects.toastSeconds)
        onActivated: (index) => effects.toastSeconds = [5, 7, 15, 30, 60, 300][index]
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-colors"
        iconFallback: "preferences-desktop-color"
        title: i18n("Colors")
        description: i18n("Light or dark mode, accent color")
        target: "personalization/colors"
    }

    NavCard {
        iconName: "org.raye.settings-performance"
        iconFallback: "speedometer"
        title: i18n("Performance options")
        description: i18n("Turn every effect off at once on a slow PC")
        target: "system/performance"
    }
}
