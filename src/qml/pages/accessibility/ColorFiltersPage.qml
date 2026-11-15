// SPDX-License-Identifier: GPL-3.0-or-later
// Accessibility > Colour filters: a filter on the whole screen that
// makes colours easier to tell apart, or takes them away. The strip at
// the top shows the colours the filters change.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accessibility"), target: "accessibility" },
             { text: i18nc("@title settings page", "Color filters") }]

    ColorFilterSettings { id: filters }

    // the colours to look at while trying a filter
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 112
        radius: Theme.radiusOverlay
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder
        Accessible.role: Accessible.Graphic
        Accessible.name: i18n("Colors to compare: red, orange, yellow, green, cyan, blue, violet and magenta")

        Row {
            anchors.centerIn: parent
            spacing: Theme.s2
            Repeater {
                model: ["#e5383b", "#f77f00", "#fcbf49", "#2a9d3f", "#2ec4b6", "#3a6ff7", "#7b3cff", "#c040ff"]
                Rectangle {
                    required property string modelData
                    width: Math.min(64, (page.width - 2 * Theme.contentPadding - 7 * Theme.s2 - 2 * Theme.s5) / 8)
                    height: 64
                    radius: Theme.radiusOverlay
                    color: modelData
                    Accessible.ignored: true
                }
            }
        }
    }

    ToggleCard {
        iconName: "org.raye.settings-color-filters"
        iconFallback: "preferences-desktop-color"
        title: i18n("Color filters")
        description: i18n("Change the colors of the whole screen to make them easier to tell apart")
        searchId: "color-filters"
        checked: filters.enabled
        onToggled: filters.enabled = checked
    }

    SectionHeader { text: i18n("Filter") }

    RadioCard {
        Layout.fillWidth: true
        title: i18n("Red-green (red weak, protanopia)")
        description: i18n("Reds look dark and are hard to tell from greens")
        searchId: "protanopia"
        enabled: filters.enabled
        checked: filters.mode === 0
        // clicking sets `checked` by hand: keep it following the setting
        onClicked: {
            filters.mode = 0
            checked = Qt.binding(() => filters.mode === 0)
        }
    }
    RadioCard {
        Layout.fillWidth: true
        title: i18n("Red-green (green weak, deuteranopia)")
        description: i18n("Greens are hard to tell from reds")
        searchId: "deuteranopia"
        enabled: filters.enabled
        checked: filters.mode === 1
        // clicking sets `checked` by hand: keep it following the setting
        onClicked: {
            filters.mode = 1
            checked = Qt.binding(() => filters.mode === 1)
        }
    }
    RadioCard {
        Layout.fillWidth: true
        title: i18n("Blue-yellow (tritanopia)")
        description: i18n("Blues are hard to tell from greens, and yellows from pinks")
        searchId: "tritanopia"
        enabled: filters.enabled
        checked: filters.mode === 2
        // clicking sets `checked` by hand: keep it following the setting
        onClicked: {
            filters.mode = 2
            checked = Qt.binding(() => filters.mode === 2)
        }
    }
    RadioCard {
        Layout.fillWidth: true
        title: i18n("Grayscale")
        description: i18n("Everything in shades of gray")
        searchId: "greyscale"
        enabled: filters.enabled
        checked: filters.mode === 3
        // clicking sets `checked` by hand: keep it following the setting
        onClicked: {
            filters.mode = 3
            checked = Qt.binding(() => filters.mode === 3)
        }
    }

    SliderCard {
        iconName: "org.raye.settings-color-filters"
        iconFallback: "preferences-desktop-color"
        title: i18n("Intensity")
        description: i18n("How strongly the filter changes the colors")
        searchId: "filter-intensity"
        enabled: filters.enabled
        from: 0
        to: 100
        stepSize: 5
        value: filters.intensity
        textFromValue: (v) => i18nc("@label percent", "%1%", Math.round(v))
        onMoved: filters.intensity = Math.round(value)
    }
}
