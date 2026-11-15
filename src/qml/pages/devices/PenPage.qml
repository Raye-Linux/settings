// SPDX-License-Identifier: GPL-3.0-or-later
// Devices > Pen & tablet: which pens and tablets this PC sees. The fine
// settings of a drawing tablet (screen mapping, pressure, buttons) open in
// a window of their own.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Bluetooth & devices"), target: "devices" },
             { text: i18nc("@title settings page", "Pen & tablet") }]

    InputDeviceSettings { id: input; kind: "pen" }
    ExternalModule { id: external }

    property string failure: ""

    InfoBar {
        Layout.fillWidth: true
        open: page.failure !== ""
        severity: "error"
        title: i18nc("@title", "It could not be opened")
        message: page.failure
        onClosed: page.failure = ""
    }

    EmptyState {
        Layout.fillWidth: true
        visible: input.deviceNames.length === 0
        iconName: "input-tablet"
        title: i18nc("@title", "No pen or tablet found")
        message: i18nc("@info", "Plug in a drawing tablet, and it shows up here.")
    }

    Repeater {
        model: input.deviceNames
        delegate: Card {
            required property string modelData
            Layout.fillWidth: true
            iconName: "org.raye.settings-pen"
            iconFallback: "input-tablet"
            title: modelData
        }
    }

    ButtonCard {
        searchId: "tablet-settings"
        iconName: "org.raye.settings-pen"
        iconFallback: "input-tablet"
        title: i18nc("@title", "Tablet settings")
        description: i18nc("@info", "Which screen the tablet draws on, pen pressure, buttons")
        buttonText: i18nc("@action:button", "Open")
        enabled: input.deviceNames.length > 0
        onButtonClicked: {
            if (!external.open("kcm_tablet"))
                page.failure = i18nc("@info", "The tablet settings are not installed.")
        }
    }
}
