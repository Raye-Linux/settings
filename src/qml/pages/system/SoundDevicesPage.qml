// SPDX-License-Identifier: GPL-3.0-or-later
// System › Sound › All sound devices: every output and input, the one
// in use marked, and each sound card with its profile and a switch to
// stop apps and Raye from using it.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Sound"), target: "system/sound" },
             { text: i18nc("@title settings page", "All sound devices") }]

    SoundDevices { id: outputs }
    SoundDevices { id: inputs; input: true }
    SoundCards { id: cards }

    component DeviceRow: Card {
        required property int index
        required property var model
        property var devices
        title: model.title
        description: model.subtitle
        iconName: model.iconName
        Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            visible: model.isDefault
            text: i18nc("@info the device apps use now", "In use")
            font: Theme.body
            color: Theme.textSecondary
        }
        Button {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            visible: !model.isDefault
            text: i18nc("@action:button", "Use this device")
            onClicked: devices.setDefault(index)
        }
    }

    SectionHeader { text: i18n("Output devices"); first: true }
    Repeater {
        model: outputs
        delegate: DeviceRow { devices: outputs }
    }
    Card {
        visible: outputs.count === 0
        title: i18n("No output devices found")
        iconName: "org.raye.settings-status-info"
    }

    SectionHeader { text: i18n("Input devices") }
    Repeater {
        model: inputs
        delegate: DeviceRow { devices: inputs }
    }
    Card {
        visible: inputs.count === 0
        title: i18n("No microphone found")
        iconName: "org.raye.settings-status-info"
    }

    SectionHeader {
        text: i18n("Sound cards")
        visible: cards.count > 0
    }
    Repeater {
        model: cards
        delegate: Expander {
            id: card
            required property int index
            required property var model
            Layout.fillWidth: true
            title: model.title
            description: model.allowed ? "" : i18nc("@info a sound card turned off", "Off: apps can't use it")
            iconName: model.iconName
            ExpanderRow {
                title: i18n("Allow apps and Raye to use this device")
                ToggleSwitch {
                    checked: card.model.allowed
                    text: i18n("Allow apps and Raye to use this device")
                    onToggled: cards.setAllowed(card.index, checked)
                }
            }
            ExpanderRow {
                title: i18nc("@label how a sound card is used (stereo, surround...)", "Profile")
                enabled: card.model.allowed && card.model.profiles.length > 0
                ComboBox {
                    model: card.model.profiles
                    textRole: "title"
                    valueRole: "name"
                    currentIndex: card.model.profiles.findIndex(p => p.name === card.model.activeProfile)
                    Accessible.name: i18nc("@label %1 is a sound card", "Profile of %1", card.model.title)
                    onActivated: cards.setProfile(card.index, currentValue)
                }
            }
        }
    }
}
