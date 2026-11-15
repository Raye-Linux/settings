// SPDX-License-Identifier: GPL-3.0-or-later
// Accessibility > Audio: see alerts as well as hear them.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accessibility"), target: "accessibility" },
             { text: i18nc("@title settings page", "Audio") }]

    AccessSettings { id: access }

    ToggleCard {
        iconName: "org.raye.settings-accessibility-audio"
        iconFallback: "audio-volume-high"
        title: i18n("Flash my screen during audio notifications")
        description: i18n("The screen flashes when an alert sound plays, so you see it too")
        searchId: "flash-screen"
        checked: access.visibleBell
        onToggled: access.visibleBell = checked
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-sound"
        iconFallback: "audio-volume-high"
        title: i18n("Sound")
        description: i18n("Volume, output and input devices")
        target: "system/sound"
    }
}
