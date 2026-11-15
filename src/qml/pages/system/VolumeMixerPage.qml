// SPDX-License-Identifier: GPL-3.0-or-later
// System › Sound › Volume mixer: the volume of the whole system, then
// one row per app playing sound, with its own volume, mute and the
// output it plays on.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Sound"), target: "system/sound" },
             { text: i18nc("@title settings page", "Volume mixer") }]

    SoundDevices { id: outputs }
    SoundStreams { id: streams }
    SoundDefaultDevice { id: output; devices: outputs }

    // [{ text, index }] of the outputs, for the per-app choice
    readonly property var outputChoices: {
        void outputs.count
        const list = []
        for (let i = 0; i < outputs.count; ++i) {
            const d = outputs.get(i)
            list.push({ text: d.title, deviceIndex: d.deviceIndex })
        }
        return list
    }

    SectionHeader { text: i18nc("@title:group", "System"); first: true }

    Card {
        searchId: "system-volume"
        title: output.title !== "" ? output.title : i18n("No output device")
        description: i18n("The volume of everything")
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
            Accessible.name: i18n("System volume")
            onMoved: outputs.setVolume(output.row, Math.round(value))
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

    SectionHeader { text: i18nc("@title:group", "Apps") }

    Repeater {
        model: streams
        delegate: Expander {
            id: app
            required property int index
            required property var model
            Layout.fillWidth: true
            title: model.appName
            description: model.playing ? "" : i18nc("@info an app that stopped playing", "Paused")
            iconName: model.iconName
            iconFallback: "applications-multimedia"
            headerContent: [
                SoundMuteButton {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    muted: app.model.muted
                    volume: app.model.volume
                    onClicked: streams.setMuted(app.index, !app.model.muted)
                },
                Slider {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    width: Theme.sliderWidth
                    from: 0; to: 100; stepSize: 1
                    value: app.model.volume
                    Accessible.name: i18nc("@label %1 is an app", "Volume of %1", app.model.appName)
                    onMoved: streams.setVolume(app.index, Math.round(value))
                },
                Text {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    width: 32
                    text: app.model.volume
                    font: Theme.body
                    color: Theme.textSecondary
                    horizontalAlignment: Text.AlignRight
                    Accessible.ignored: true
                }
            ]
            ExpanderRow {
                title: i18n("Output device")
                ComboBox {
                    model: page.outputChoices
                    textRole: "text"
                    valueRole: "deviceIndex"
                    currentIndex: page.outputChoices.findIndex(o => o.deviceIndex === app.model.deviceIndex)
                    Accessible.name: i18nc("@label %1 is an app", "Output device of %1", app.model.appName)
                    onActivated: streams.setDevice(app.index, currentValue)
                }
            }
        }
    }

    EmptyState {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s7
        visible: streams.count === 0
        iconName: "org.raye.settings-volume-mixer"
        title: i18n("No apps are playing sound")
        message: i18n("Apps show up here while they play sound.")
    }
}
