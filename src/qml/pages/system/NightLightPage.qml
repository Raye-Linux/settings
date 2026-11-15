// SPDX-License-Identifier: GPL-3.0-or-later
// System › Display › Night light: warmer colours in the evening. On or
// off now, how warm, and when it comes on by itself (sunset to sunrise,
// or set hours). The strength shows on screen while the slider moves.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Display"), target: "system/display" },
             { text: i18nc("@title settings page", "Night light") }]

    NightLightSettings { id: night }

    // every quarter of an hour, for the "Turn on" and "Turn off" lists
    readonly property var times: {
        const list = []
        for (let m = 0; m < 24 * 60; m += 15) {
            const t = new Date(2000, 0, 1, Math.floor(m / 60), m % 60)
            list.push({ text: t.toLocaleTimeString(Qt.locale(), Locale.ShortFormat),
                        value: String(Math.floor(m / 60)).padStart(2, "0") + ":" + String(m % 60).padStart(2, "0") })
        }
        return list
    }

    ButtonCard {
        searchId: "night-light-now"
        title: night.enabled ? i18n("Night light is on") : i18n("Night light is off")
        description: night.enabled && night.scheduled
                     ? (night.schedule === "times" ? i18n("It comes on from %1 to %2.", page.timeText(night.turnOn), page.timeText(night.turnOff))
                                                    : i18n("It comes on at sunset and goes off at sunrise."))
                     : ""
        iconName: "org.raye.settings-night-light"
        iconFallback: "redshift-status-on"
        buttonText: night.enabled ? i18nc("@action:button", "Turn off now") : i18nc("@action:button", "Turn on now")
        accent: !night.enabled
        onButtonClicked: night.enabled = !night.enabled
    }

    SliderCard {
        id: strength
        searchId: "strength"
        title: i18nc("@label how warm night light makes the screen", "Strength")
        iconName: "color-management"
        from: 0; to: 100; stepSize: 1
        value: night.strength
        // seen live while dragging, saved when let go
        onMoved: night.preview(Math.round(value))
        slider.onPressedChanged: {
            if (!slider.pressed) {
                night.strength = Math.round(value)
                night.stopPreview()
            }
        }
    }

    ToggleCard {
        searchId: "schedule"
        title: i18n("Schedule night light")
        iconName: "chronometer"
        checked: night.scheduled
        onToggled: night.scheduled = checked
    }

    ComboCard {
        searchId: "schedule-kind"
        visible: night.scheduled
        title: i18n("When")
        iconName: "clock"
        model: [{ text: i18n("Sunset to sunrise"), value: "sunset" },
                { text: i18n("Set hours"), value: "times" }]
        textRole: "text"
        valueRole: "value"
        currentIndex: night.schedule === "times" ? 1 : 0
        onActivated: (index) => night.schedule = currentValue
    }
    ComboCard {
        searchId: "turn-on"
        visible: night.scheduled && night.schedule === "times"
        title: i18nc("@label a time", "Turn on")
        iconName: "clock"
        model: page.times
        textRole: "text"
        valueRole: "value"
        currentIndex: page.times.findIndex(t => t.value === night.turnOn)
        onActivated: (index) => night.turnOn = currentValue
    }
    ComboCard {
        searchId: "turn-off"
        visible: night.scheduled && night.schedule === "times"
        title: i18nc("@label a time", "Turn off")
        iconName: "clock"
        model: page.times
        textRole: "text"
        valueRole: "value"
        currentIndex: page.times.findIndex(t => t.value === night.turnOff)
        onActivated: (index) => night.turnOff = currentValue
    }
    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s3
        visible: night.scheduled
        text: i18n("Raye uses the same evening and morning times for the light and dark theme.")
        font: Theme.caption
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    function timeText(hhmm) {
        const parts = hhmm.split(":")
        const t = new Date(2000, 0, 1, Number(parts[0]), Number(parts[1]))
        return t.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
    }

    Component.onDestruction: night.stopPreview()
}
