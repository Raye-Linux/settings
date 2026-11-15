// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Sounds: which sound theme the desktop uses, a button
// to hear it, and one switch for all the event sounds of apps and Raye.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
             { text: i18nc("@title settings page", "Sounds") }]

    SoundTheme { id: sounds }

    ToggleCard {
        Layout.fillWidth: true
        searchId: "event-sounds"
        iconName: "org.raye.settings-sounds"
        iconFallback: "preferences-desktop-notification"
        title: i18nc("@label", "Play sounds for events")
        description: i18nc("@info", "Notifications, alerts and the sounds apps play when something happens")
        checked: sounds.enabled
        onToggled: {
            sounds.enabled = checked
            checked = Qt.binding(() => sounds.enabled)
        }
    }

    ComboCard {
        id: scheme
        Layout.fillWidth: true
        searchId: "sound-scheme"
        iconName: "org.raye.settings-sounds"
        iconFallback: "audio-x-generic"
        title: i18nc("@label", "Sound scheme")
        description: {
            const list = sounds.themes
            for (let i = 0; i < list.length; ++i) {
                if (list[i].id === sounds.theme)
                    return list[i].comment
            }
            return ""
        }
        enabled: sounds.enabled
        model: sounds.themes
        textRole: "name"
        valueRole: "id"
        currentIndex: {
            const list = sounds.themes
            for (let i = 0; i < list.length; ++i) {
                if (list[i].id === sounds.theme)
                    return i
            }
            return -1
        }
        onActivated: index => {
            sounds.theme = sounds.themes[index].id
            sounds.play(sounds.theme)
        }
    }

    ButtonCard {
        Layout.fillWidth: true
        searchId: "test-sound"
        iconName: "org.raye.settings-sound"
        iconFallback: "audio-volume-high"
        title: i18nc("@label", "Hear the sound scheme")
        description: i18nc("@info", "Plays a short sound from the scheme you picked")
        enabled: sounds.enabled
        buttonText: i18nc("@action:button", "Test")
        onButtonClicked: sounds.play(sounds.theme)
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }

    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-sound"
        iconFallback: "audio-volume-high"
        title: i18nc("@title settings page", "Sound")
        description: i18nc("@info", "Volume, output and input devices")
        target: "system/sound"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-notifications"
        iconFallback: "preferences-desktop-notification"
        title: i18nc("@title settings page", "Notifications")
        description: i18nc("@info", "Which apps can show notifications and play sounds")
        target: "system/notifications"
    }
}
