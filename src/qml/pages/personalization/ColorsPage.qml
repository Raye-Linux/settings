// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Colours: the preview, "Choose your mode" (Dark is
// Raye Night, Light is Raye Day), transparency effects, the accent
// colour (from the wallpaper, or one of 24 colours tuned for Raye, or
// any colour) and the accent on window borders. Every change shows at
// once, everywhere.
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
             { text: i18nc("@title settings page", "Colors") }]

    AppearanceSettings { id: appearance }
    BackgroundSettings { id: background }

    readonly property url rayeWallpaper: background.wallpapers.length > 0 ? background.wallpapers[0].preview : ""

    PersonalizationPreview {
        Layout.bottomMargin: Theme.s7
        image: background.mode === "picture" ? (background.previewImage.toString() !== "" ? background.previewImage : page.rayeWallpaper) : ""
        color: background.mode === "color" ? background.color : Theme.backdropSolid
        accent: appearance.accentColor
        mode: appearance.mode === "light" ? "day" : "night"
    }

    ComboCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-colors"
        iconFallback: "preferences-desktop-color"
        title: i18nc("@label:listbox", "Choose your mode")
        description: i18nc("@info", "Change the colors that appear in Raye and your apps")
        searchId: "mode"
        enabled: !appearance.applying
        textRole: "text"
        valueRole: "value"
        model: [{ text: i18nc("@item:inlistbox color mode", "Dark"), value: "dark" },
                { text: i18nc("@item:inlistbox color mode", "Light"), value: "light" }]
        currentIndex: Math.max(0, indexOfValue(appearance.mode))
        onActivated: {
            appearance.setMode(currentValue)
            currentIndex = Qt.binding(() => Math.max(0, indexOfValue(appearance.mode)))
        }
    }

    ToggleCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-transparency"
        iconFallback: "preferences-desktop-effects"
        title: i18nc("@label", "Transparency effects")
        description: i18nc("@info", "Windows and surfaces appear translucent")
        searchId: "transparency"
        checked: appearance.transparency
        onToggled: appearance.transparency = checked
    }

    Expander {
        id: accentExpander
        Layout.fillWidth: true
        iconName: "org.raye.settings-accent"
        iconFallback: "color-management"
        title: i18nc("@label", "Accent color")
        searchId: "accent"
        expanded: true

        headerContent: [
            ComboBox {
                id: accentMode
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                textRole: "text"
                valueRole: "value"
                model: [{ text: i18nc("@item:inlistbox accent color", "Manual"), value: false },
                        { text: i18nc("@item:inlistbox accent color", "Automatic"), value: true }]
                currentIndex: appearance.accentFromWallpaper ? 1 : 0
                Accessible.name: i18nc("@label", "Accent color")
                onActivated: {
                    appearance.accentFromWallpaper = currentValue
                    currentIndex = Qt.binding(() => appearance.accentFromWallpaper ? 1 : 0)
                }
            }
        ]

        Item {
            implicitHeight: accentColumn.implicitHeight + 2 * Theme.cardPadding
            Layout.fillWidth: true
            ColumnLayout {
                id: accentColumn
                x: Theme.cardTextLeft
                y: Theme.cardPadding
                width: parent.width - Theme.cardTextLeft - Theme.cardPadding
                spacing: Theme.s4
                Text {
                    text: appearance.accentFromWallpaper ? i18nc("@info", "The accent color follows your background picture.")
                                                         : i18nc("@label", "Raye colors")
                    font: Theme.body
                    color: Theme.text
                }
                ColorsSwatches {
                    colors: appearance.swatches
                    current: appearance.accentColor
                    currentShown: !appearance.accentFromWallpaper
                    enabled: !appearance.accentFromWallpaper
                    opacity: enabled ? 1 : 0.4
                    columns: 12
                    onPicked: (c) => appearance.setAccentColor(c)
                }
                RowLayout {
                    spacing: Theme.s3
                    enabled: !appearance.accentFromWallpaper
                    Text {
                        text: i18nc("@label", "Custom colors")
                        font: Theme.body
                        color: parent.enabled ? Theme.text : Theme.textDisabled
                    }
                    Button {
                        text: i18nc("@action:button", "View colors")
                        onClicked: {
                            accentDialog.selectedColor = appearance.accentColor
                            accentDialog.open()
                        }
                    }
                }
            }
        }
    }

    ToggleCard {
        Layout.fillWidth: true
        visible: appearance.rayeBorders
        title: i18nc("@label", "Show accent color on title bars and window borders")
        searchId: "accent-borders"
        checked: appearance.accentOnBorders
        onToggled: appearance.accentOnBorders = checked
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-color-filters"
        iconFallback: "preferences-desktop-color"
        title: i18nc("@title settings page", "Color filters")
        description: i18nc("@info", "Color filters and high contrast for low vision and light sensitivity")
        target: "accessibility/color-filters"
    }

    ColorDialog {
        id: accentDialog
        title: i18nc("@title:window", "Choose an accent color")
        onAccepted: appearance.setAccentColor(selectedColor)
    }
}
