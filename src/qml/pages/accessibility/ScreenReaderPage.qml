// SPDX-License-Identifier: GPL-3.0-or-later
// Accessibility > Screen reader: have what is on the screen read out.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accessibility"), target: "accessibility" },
             { text: i18nc("@title settings page", "Screen reader") }]

    AccessSettings { id: access }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: !access.screenReaderInstalled
        severity: "warning"
        closable: false
        title: i18n("The screen reader isn't installed")
        message: i18n("Raye needs the screen reader to read out what is on the screen. Install it with Raye Update or from the Store, then come back to this page.")
    }

    ToggleCard {
        iconName: "org.raye.settings-screen-reader"
        iconFallback: "preferences-desktop-accessibility"
        title: i18n("Screen reader")
        description: i18n("Reads out what is on the screen. Press the Windows key + Alt + S to turn it on or off.")
        searchId: "screen-reader"
        enabled: access.screenReaderInstalled
        checked: access.screenReader
        onToggled: access.screenReader = checked
    }

    Card {
        iconName: "org.raye.settings-screen-reader"
        iconFallback: "preferences-desktop-accessibility"
        title: i18n("Screen reader settings")
        description: i18n("Voice, speed, and how much it says")
        searchId: "screen-reader-settings"
        enabled: access.screenReaderInstalled
        clickable: true
        actionGlyph: "open-external"
        onClicked: access.openScreenReaderSettings()
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-accessibility-audio"
        iconFallback: "audio-volume-high"
        title: i18n("Audio")
        description: i18n("Flash the screen for alerts")
        target: "accessibility/audio"
    }
}
