// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization: the desktop as it is now, drawn small, next to
// "Select a theme to apply" (Raye Night, Raye Day); then a card per
// page.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    title: i18nc("@title settings category", "Personalization")
    showBack: false

    AppearanceSettings { id: looks }
    BackgroundSettings { id: background }

    // the Raye wallpaper, what the themes are drawn over
    readonly property url rayeWallpaper: background.wallpapers.length > 0 ? background.wallpapers[0].preview : ""

    banner: Component {
        RowLayout {
            spacing: Theme.s7
            PersonalizationPreview {
                Layout.alignment: Qt.AlignTop
                image: background.mode === "picture" ? (background.previewImage.toString() !== "" ? background.previewImage : page.rayeWallpaper) : ""
                color: background.mode === "color" ? background.color : Theme.backdropSolid
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Theme.s3
                Text {
                    text: i18nc("@title:group", "Select a theme to apply")
                    font: Theme.bodyStrong
                    color: Theme.text
                }
                ThemesChoice {
                    Layout.fillWidth: true
                    appearance: looks
                    wallpaper: page.rayeWallpaper
                }
            }
        }
    }

    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-background"
        iconFallback: "preferences-desktop-wallpaper"
        title: i18nc("@title settings page", "Background")
        description: i18nc("@info", "Background picture, color, slideshow")
        target: "personalization/background"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-colors"
        iconFallback: "preferences-desktop-color"
        title: i18nc("@title settings page", "Colors")
        description: i18nc("@info", "Accent color, transparency effects, dark or light")
        target: "personalization/colors"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-themes"
        iconFallback: "preferences-desktop-theme-global"
        title: i18nc("@title settings page", "Themes")
        description: i18nc("@info", "Raye Night, Raye Day and other themes")
        target: "personalization/themes"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-lock-screen"
        iconFallback: "system-lock-screen"
        title: i18nc("@title settings page", "Lock screen")
        description: i18nc("@info", "Lock and sign-in screen pictures, screen timeout")
        target: "personalization/lock-screen"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-text-input"
        iconFallback: "input-keyboard-virtual"
        title: i18nc("@title settings page", "Text input")
        description: i18nc("@info", "On-screen keyboard")
        target: "personalization/text-input"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-start"
        iconFallback: "start-here"
        title: i18nc("@title settings page", "Start")
        description: i18nc("@info", "Recent apps and items, folders")
        target: "personalization/start"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-taskbar"
        iconFallback: "configure"
        title: i18nc("@title settings page", "Taskbar")
        description: i18nc("@info", "Taskbar behaviors, system pins")
        target: "personalization/taskbar"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-fonts"
        iconFallback: "preferences-desktop-font"
        title: i18nc("@title settings page", "Fonts")
        description: i18nc("@info", "Install, manage")
        target: "personalization/fonts"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-sounds"
        iconFallback: "preferences-desktop-notification"
        title: i18nc("@title settings page", "Sounds")
        description: i18nc("@info", "Sounds the system plays")
        target: "personalization/sounds"
    }
}
