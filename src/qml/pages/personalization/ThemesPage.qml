// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Themes: what the theme in use is made of (background,
// colour, sounds, mouse pointer, each leading to its page), then the
// themes to choose from: Raye's own first, then any other installed
// theme. Applying a theme keeps the taskbar and the desktop as they are.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
             { text: i18nc("@title settings page", "Themes") }]

    AppearanceSettings { id: looks }
    BackgroundSettings { id: background }

    readonly property url rayeWallpaper: background.wallpapers.length > 0 ? background.wallpapers[0].preview : ""
    readonly property var current: {
        for (const t of looks.themes) {
            if (t.current)
                return t
        }
        return ({ name: page.readableId(looks.currentTheme), description: "" })
    }

    // a theme that is not installed any more only has its id left:
    // "org.raye.night.desktop" reads "Raye Night"
    function readableId(id) {
        const words = String(id || "").replace(/^(org|com|net)\./, "").replace(/\.desktop$/, "")
                                     .split(/[._-]+/).filter((w) => w !== "")
        return words.map((w) => w.charAt(0).toUpperCase() + w.slice(1)).join(" ")
    }
    readonly property bool hasOthers: looks.themes.some((t) => !t.raye)

    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s7
        spacing: Theme.s7
        PersonalizationPreview {
            Layout.alignment: Qt.AlignTop
            image: background.mode === "picture" ? (background.previewImage.toString() !== "" ? background.previewImage : page.rayeWallpaper) : ""
            color: background.mode === "color" ? background.color : Theme.backdropSolid
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.s1
            Text {
                text: i18nc("@label", "Current theme: %1", page.current.name)
                font: Theme.subtitle
                color: Theme.text
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: page.current.description
                font: Theme.body
                color: Theme.textSecondary
                Layout.fillWidth: true
                wrapMode: Text.Wrap
            }
        }
    }

    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-background"
        iconFallback: "preferences-desktop-wallpaper"
        title: i18nc("@title settings page", "Background")
        description: background.mode === "color" ? i18nc("@info", "Solid color")
                   : background.mode === "slideshow" ? i18nc("@info", "Slideshow") : i18nc("@info", "Picture")
        target: "personalization/background"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-colors"
        iconFallback: "preferences-desktop-color"
        title: i18nc("@title settings page", "Color")
        description: looks.accentFromWallpaper ? i18nc("@info accent color", "Automatic")
                   : looks.swatchIndex(looks.accentColor) === 0 ? i18nc("@info accent color", "Raye violet")
                   : i18nc("@info accent color", "Custom")
        target: "personalization/colors"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-sounds"
        iconFallback: "preferences-desktop-notification"
        title: i18nc("@title settings page", "Sounds")
        description: i18nc("@info", "Sounds the system plays")
        target: "personalization/sounds"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-mouse-pointer"
        iconFallback: "preferences-desktop-cursors"
        title: i18nc("@title settings page", "Mouse pointer")
        description: i18nc("@info", "Pointer style, size and color")
        target: "accessibility/mouse-pointer"
    }

    SectionHeader { text: i18nc("@title:group", "Change theme") }
    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        text: looks.applying ? i18nc("@info", "Applying the theme…") : i18nc("@info", "Your taskbar, desktop and apps stay where they are.")
        font: Theme.caption
        color: Theme.textSecondary
    }
    ThemesChoice {
        Layout.fillWidth: true
        appearance: looks
        wallpaper: page.rayeWallpaper
        property string searchId: "change-theme"
    }

    SectionHeader {
        visible: page.hasOthers
        text: i18nc("@title:group", "More themes")
    }
    ThemesChoice {
        Layout.fillWidth: true
        visible: page.hasOthers
        appearance: looks
        raye: false
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-color-filters"
        iconFallback: "preferences-desktop-color"
        title: i18nc("@title settings page", "Color filters")
        description: i18nc("@info", "High contrast and color filters")
        target: "accessibility/color-filters"
    }
}
