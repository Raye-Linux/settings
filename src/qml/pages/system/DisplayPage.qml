// SPDX-License-Identifier: GPL-3.0-or-later
// System › Display: how the screens sit, how bright they are, night
// light, HDR, scale, resolution, orientation and refresh rate. A change
// that could leave the screen black asks to be kept and goes back by
// itself after 15 seconds.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Display") }]

    DisplayConfig { id: display }
    ScreenBrightness { id: brightness }
    NightLightSettings { id: nightLight }
    DisplayIdentify { id: identify; outputs: display.outputs }
    DisplayKeepDialog { config: display }

    readonly property var screens: display.outputs
    readonly property bool several: screens.length > 1
    readonly property var current: screens.find(s => s.id === display.selected) || null

    InfoBar {
        Layout.fillWidth: true
        visible: display.error !== ""
        severity: "error"
        title: i18n("Display settings")
        message: display.error
        closable: false
    }

    Loader {
        Layout.fillWidth: true
        active: !display.ready && display.error === ""
        visible: active
        sourceComponent: Shimmer { implicitHeight: 248 }
    }

    // --- arrangement ------------------------------------------------------
    DisplayArrangement {
        visible: display.ready && page.several
        config: display
        searchId: "arrangement"
        onIdentifyRequested: identify.show()
    }

    // under the screens, for the one picked there: Windows' check box.
    // The main display gets the taskbar with the clock and the tray.
    RowLayout {
        property string searchId: "main-display"
        Layout.fillWidth: true
        visible: display.ready && page.several && page.current !== null
        spacing: Theme.s3
        NotificationsCheckBox {
            text: i18n("Make this my main display")
            // ticked and greyed on the main one: there is nothing to undo,
            // another display is made the main one instead
            checked: page.current !== null && page.current.primary
            enabled: page.current !== null && !page.current.primary
            // a screen reader's toggle changes `checked` without `toggled`
            onCheckedChanged: if (checked && page.current !== null && !page.current.primary) display.makePrimary(display.selected)
        }
        Item { Layout.fillWidth: true }
        Text {
            text: page.current !== null && page.current.primary
                  ? i18nc("@info %1 is a display number", "Display %1 is your main display. It has the taskbar with the clock.", page.current.number)
                  : i18n("Your main display has the taskbar with the clock and the notifications.")
            font: Theme.caption
            color: Theme.textSecondary
            elide: Text.ElideRight
            Layout.maximumWidth: page.width / 2
        }
    }

    Expander {
        searchId: "multiple-displays"
        visible: page.several
        title: i18n("Multiple displays")
        description: i18n("Choose how your displays are used")
        iconName: "org.raye.settings-display"
        iconFallback: "preferences-desktop-display"
        headerContent: ComboBox {
            id: modeBox
            readonly property var choices: {
                const list = [{ text: i18n("Extend these displays"), mode: "extend", id: -1 },
                              { text: i18n("Duplicate these displays"), mode: "duplicate", id: -1 }]
                for (const s of page.screens)
                    list.push({ text: i18nc("@item %1 is a display number", "Show only on %1", s.number), mode: "only", id: s.id })
                return list
            }
            model: choices
            textRole: "text"
            currentIndex: {
                const m = display.mode
                if (m !== "only")
                    return m === "duplicate" ? 1 : 0
                const on = page.screens.find(s => s.enabled)
                return on ? choices.findIndex(c => c.mode === "only" && c.id === on.id) : 0
            }
            Accessible.name: i18n("Multiple displays")
            onActivated: (index) => display.setMode(choices[index].mode, choices[index].id)
        }
        ExpanderRow {
            title: i18n("Display settings below apply to")
            ComboBox {
                model: page.screens.map(s => ({ text: i18nc("@item %1 number, %2 name", "%1. %2", s.number, s.name), id: s.id }))
                textRole: "text"
                currentIndex: page.screens.findIndex(s => s.id === display.selected)
                Accessible.name: i18n("Display to change")
                onActivated: (index) => display.selected = page.screens[index].id
            }
        }
    }

    // --- brightness & colour --------------------------------------------------
    SectionHeader { text: i18n("Brightness & color"); first: !page.several }

    // two screens of the same model get their number, as on the tiles above
    readonly property var brightnessTitles: brightness.titles(brightness.displays.map(d => d.label), page.screens)

    Repeater {
        model: brightness.displays
        delegate: SliderCard {
            required property var modelData
            required property int index
            searchId: "brightness"
            title: brightness.displays.length > 1
                   ? i18nc("@label %1 is a screen", "Brightness: %1", page.brightnessTitles[index] || modelData.label)
                   : i18n("Brightness")
            description: modelData.builtIn ? i18n("Change the brightness of the built-in display") : ""
            iconName: "brightness-high"
            iconFallback: "video-display-brightness"
            from: 0; to: 100; stepSize: 1
            value: modelData.percent
            onMoved: brightness.setBrightness(modelData.name, Math.round(value))
        }
    }
    Card {
        visible: !brightness.available
        searchId: "brightness"
        title: i18n("Brightness")
        description: i18n("Use the buttons on your display to change its brightness.")
        iconName: "brightness-high"
        iconFallback: "video-display-brightness"
    }

    Card {
        searchId: "night-light"
        title: i18n("Night light")
        description: i18n("Use warmer colors to help block blue light")
        iconName: "org.raye.settings-night-light"
        iconFallback: "redshift-status-on"
        clickable: true
        actionGlyph: "chevron-right"
        onClicked: Router.navigate("system/night-light")
        Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: nightLight.enabled ? i18nc("@label a setting that is on", "On") : i18nc("@label a setting that is off", "Off")
            font: Theme.body
            color: Theme.text
        }
        ToggleSwitch {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            checked: nightLight.enabled
            text: i18n("Night light")
            onToggled: nightLight.enabled = checked
        }
    }

    ToggleCard {
        searchId: "hdr"
        visible: display.hdrCapable
        title: i18n("Use HDR")
        description: i18n("Brighter, more vivid pictures in games and videos made for it")
        iconName: "video-display"
        checked: display.hdrEnabled
        onToggled: display.setHdrEnabled(checked)
    }
    SliderCard {
        searchId: "sdr-brightness"
        visible: display.hdrCapable && display.hdrEnabled
        title: i18n("Brightness of everything else")
        description: i18n("How bright apps and the desktop look while HDR is on")
        from: 80; to: 500; stepSize: 10
        value: display.sdrBrightness
        textFromValue: (v) => i18nc("@label brightness in nits", "%1 nits", Math.round(v))
        onMoved: display.setSdrBrightness(Math.round(value))
    }

    // --- scale & layout ----------------------------------------------------
    SectionHeader { text: i18n("Scale & layout") }

    ComboCard {
        searchId: "scale"
        title: i18n("Scale")
        description: i18n("Change the size of text, apps and other items")
        iconName: "zoom-fit-best"
        enabled: display.ready
        model: display.scales
        textRole: "text"
        valueRole: "value"
        currentIndex: display.scales.findIndex(s => s.value === display.scale)
        onActivated: (index) => display.setScale(display.scales[index].value)
    }
    ComboCard {
        searchId: "resolution"
        title: i18n("Display resolution")
        description: i18n("Adjust the resolution to fit your connected display")
        iconName: "video-display"
        enabled: display.ready
        model: display.resolutions
        textRole: "text"
        currentIndex: display.resolutionIndex
        onActivated: (index) => display.setResolution(index)
    }
    ComboCard {
        searchId: "orientation"
        title: i18n("Display orientation")
        iconName: "object-rotate-right"
        enabled: display.ready
        model: [{ text: i18nc("@item screen orientation", "Landscape"), value: 0 },
                { text: i18nc("@item screen orientation", "Portrait"), value: 90 },
                { text: i18nc("@item screen orientation", "Landscape (flipped)"), value: 180 },
                { text: i18nc("@item screen orientation", "Portrait (flipped)"), value: 270 }]
        textRole: "text"
        valueRole: "value"
        currentIndex: [0, 90, 180, 270].indexOf(display.orientation)
        onActivated: (index) => display.setOrientation(currentValue)
    }

    // --- advanced -----------------------------------------------------------
    SectionHeader { text: i18n("Related settings") }

    ComboCard {
        searchId: "refresh-rate"
        title: i18n("Choose a refresh rate")
        description: i18n("A higher rate gives smoother motion, but also uses more power")
        iconName: "chronometer"
        visible: display.refreshRates.length > 0 && display.refreshIndex >= 0
        enabled: display.ready && display.refreshRates.length > 1
        model: display.refreshRates
        textRole: "text"
        currentIndex: display.refreshIndex
        onActivated: (index) => display.setRefreshRate(index)
    }
    ToggleCard {
        searchId: "adaptive-sync"
        visible: display.vrrCapable
        title: i18n("Adaptive sync")
        description: i18n("Matches the screen to games and videos to stop tearing")
        iconName: "games-config-options"
        checked: display.vrrEnabled
        onToggled: display.setVrrEnabled(checked)
    }
    NavCard {
        searchId: "graphics"
        visible: Router.has("two-gpus")
        title: i18n("Graphics")
        description: i18n("Choose which graphics card each app uses")
        iconName: "org.raye.settings-graphics"
        iconFallback: "preferences-desktop-display"
        target: "system/graphics"
    }
}
