// SPDX-License-Identifier: GPL-3.0-or-later
// "Select a theme to apply": one RadioCard per theme, each with a small
// desktop drawn in that theme's colours over the Raye wallpaper (or the
// theme's own preview picture when it is not one of Raye's). A click
// applies the theme; the taskbar and the desktop stay as they are.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

Flow {
    id: choice

    property var appearance: null
    // the picture the Raye themes are drawn over
    property url wallpaper
    // which themes: Raye's own (true) or the others (false)
    property bool raye: true

    readonly property var shown: {
        if (!appearance)
            return []
        return appearance.themes.filter((t) => t.raye === choice.raye)
    }

    spacing: Theme.s4

    Repeater {
        model: choice.shown
        delegate: RadioCard {
            id: card
            required property var modelData
            width: 200
            title: modelData.name
            description: modelData.current ? i18nc("@info the theme in use", "In use") : ""
            checked: modelData.current
            enabled: !choice.appearance.applying
            Accessible.description: modelData.description
            onClicked: {
                if (!modelData.current)
                    choice.appearance.applyTheme(modelData.id)
                checked = Qt.binding(() => card.modelData.current)
            }
            Item {
                width: parent ? parent.width : 168
                height: width * 0.62
                PersonalizationPreview {
                    anchors.fill: parent
                    visible: card.modelData.raye || card.modelData.preview.toString() === ""
                    image: choice.wallpaper
                    mode: card.modelData.id === "org.raye.day.desktop" ? "day" : "night"
                    accent: Theme.accent
                }
                Image {
                    anchors.fill: parent
                    visible: !card.modelData.raye && card.modelData.preview.toString() !== ""
                    source: card.modelData.preview
                    sourceSize.width: 400
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                }
            }
        }
    }
}
