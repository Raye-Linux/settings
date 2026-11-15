// SPDX-License-Identifier: GPL-3.0-or-later
// System › Sound: where sound plays and where it is recorded from, how
// loud, and the way to the volume mixer and every sound device. Changes
// apply at once, as in Windows.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Sound") }]

    SoundTheme { id: chime }
    SoundDevices { id: outputs }
    SoundDevices { id: inputs; input: true }
    SoundDefaultDevice { id: output; devices: outputs }
    SoundDefaultDevice { id: microphone; devices: inputs }
    LevelMeter {
        id: meter
        device: microphone.name
        active: false
    }
    // the test stops when the page goes away or the window hides
    Connections {
        target: page.Window.window
        function onVisibleChanged() { if (!page.Window.window.visible) meter.active = false }
    }

    InfoBar {
        Layout.fillWidth: true
        visible: !outputs.ready
        severity: "warning"
        closable: false
        title: i18n("Sound isn't available right now")
        message: i18n("Raye can't reach the sound service. Try restarting your PC.")
    }

    // --- Output -----------------------------------------------------------
    SectionHeader { text: i18nc("@title:group sound output", "Output"); first: true }

    SoundDeviceChooser {
        searchId: "output-device"
        devices: outputs
        emptyText: i18n("No output devices found")
    }

    Card {
        searchId: "volume"
        title: i18nc("@label", "Volume")
        iconName: "audio-volume-high"
        enabled: output.row >= 0
        SoundMuteButton {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            muted: output.muted
            volume: output.volume
            onClicked: outputs.setMuted(output.row, !output.muted)
        }
        Slider {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            width: Theme.sliderWidth
            from: 0; to: 100; stepSize: 1
            value: output.volume
            Accessible.name: i18nc("@label", "Volume")
            onMoved: outputs.setVolume(output.row, Math.round(value))
            // one sound when the slider is let go, none while it moves
            onPressedChanged: if (!pressed && !output.muted && output.volume > 0) chime.playVolumeChime()
        }
        Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            width: 32
            text: output.volume
            font: Theme.body
            color: Theme.textSecondary
            horizontalAlignment: Text.AlignRight
            Accessible.ignored: true
        }
    }

    ToggleCard {
        Layout.fillWidth: true
        searchId: "volume-sound"
        iconName: "audio-volume-medium"
        title: i18nc("@label", "Play a sound when I change the volume")
        checked: chime.volumeChime
        onToggled: {
            chime.volumeChime = checked
            checked = Qt.binding(() => chime.volumeChime)
        }
    }

    Expander {
        searchId: "balance"
        visible: output.hasBalance
        title: i18nc("@label left and right speakers", "Balance")
        description: i18n("How loud the left side is next to the right side")
        iconName: "audio-speakers-symbolic"
        ExpanderRow {
            title: i18nc("@label", "Left and right")
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@label the left speaker", "Left")
                font: Theme.caption
                color: Theme.textSecondary
            }
            Slider {
                id: balanceSlider
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                width: Theme.sliderWidth
                from: -100; to: 100; stepSize: 5
                value: output.balance
                Accessible.name: i18nc("@label left and right speakers", "Balance")
                onMoved: outputs.setBalance(output.row, Math.round(value))
            }
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@label the right speaker", "Right")
                font: Theme.caption
                color: Theme.textSecondary
            }
        }
        ExpanderRow {
            title: i18nc("@action:button", "Put the balance back in the middle")
            Button {
                text: i18nc("@action:button", "Reset")
                enabled: output.balance !== 0
                onClicked: outputs.setBalance(output.row, 0)
            }
        }
    }

    // --- Input ------------------------------------------------------------
    SectionHeader { text: i18nc("@title:group sound input", "Input") }

    SoundDeviceChooser {
        searchId: "input-device"
        devices: inputs
        emptyText: i18n("No microphone found")
    }

    Card {
        searchId: "input-volume"
        title: i18nc("@label microphone level", "Volume")
        iconName: "audio-input-microphone"
        enabled: microphone.row >= 0
        SoundMuteButton {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            input: true
            muted: microphone.muted
            onClicked: inputs.setMuted(microphone.row, !microphone.muted)
        }
        Slider {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            width: Theme.sliderWidth
            from: 0; to: 100; stepSize: 1
            value: microphone.volume
            Accessible.name: i18nc("@label microphone level", "Input volume")
            onMoved: inputs.setVolume(microphone.row, Math.round(value))
        }
        Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            width: 32
            text: microphone.volume
            font: Theme.body
            color: Theme.textSecondary
            horizontalAlignment: Text.AlignRight
            Accessible.ignored: true
        }
    }

    Card {
        searchId: "test-microphone"
        title: i18n("Test your microphone")
        description: meter.active ? i18n("Speak: the bar moves when Raye hears you.")
                                  : i18n("Start a test to see if your microphone picks up your voice.")
        iconName: "audio-input-microphone"
        enabled: microphone.row >= 0
        ProgressBar {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            visible: meter.active
            width: 160
            from: 0; to: 1
            value: meter.level
            Accessible.name: i18n("Microphone level")
        }
        Button {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: meter.active ? i18nc("@action:button stop the microphone test", "Stop test")
                               : i18nc("@action:button", "Start test")
            onClicked: meter.active = !meter.active
        }
    }

    // --- Advanced ---------------------------------------------------------
    SectionHeader { text: i18nc("@title:group", "Advanced") }

    NavCard {
        searchId: "all-sound-devices"
        title: i18n("All sound devices")
        description: i18n("Turn devices on or off, and choose their profile")
        iconName: "org.raye.settings-sound-devices"
        iconFallback: "audio-card"
        target: "system/sound-devices"
    }
    NavCard {
        searchId: "volume-mixer"
        title: i18n("Volume mixer")
        description: i18n("Set the volume and the output of each app")
        iconName: "org.raye.settings-volume-mixer"
        iconFallback: "audio-volume-medium"
        target: "system/volume-mixer"
    }
    NavCard {
        searchId: "troubleshoot-sound"
        title: i18n("Troubleshoot common sound problems")
        iconName: "org.raye.settings-troubleshoot"
        iconFallback: "tools-report-bug"
        target: "system/troubleshoot"
    }

    Component.onDestruction: meter.active = false
}
