// SPDX-License-Identifier: GPL-3.0-or-later
// System › Display › Graphics: on a computer with two graphics cards,
// the apps that start on the dedicated one ("High performance").
// Shown only when the computer has two.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Display"), target: "system/display" },
             { text: i18nc("@title settings page", "Graphics") }]

    GpuApps { id: apps }
    property string filter: ""

    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s4
        text: i18n("Games and heavy apps run faster on the dedicated graphics card. Other apps save battery on the built-in one. The choice applies the next time the app starts.")
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    SectionHeader { text: i18n("Custom options for apps"); first: true }

    TextField {
        Layout.fillWidth: true
        Layout.maximumWidth: 360
        Layout.bottomMargin: Theme.s3
        placeholderText: i18n("Search this list")
        Accessible.name: i18n("Search apps")
        onTextChanged: page.filter = text.trim().toLowerCase()
    }

    Repeater {
        model: apps
        delegate: ColumnLayout {
            id: row
            required property int index
            required property var model
            Layout.fillWidth: true
            visible: page.filter === "" || model.name.toLowerCase().indexOf(page.filter) >= 0
            spacing: 0
            ComboCard {
                title: row.model.name
                iconName: row.model.iconName
                iconFallback: "application-x-executable"
                model: [{ text: i18nc("@item graphics card choice", "Let Raye decide"), value: false },
                        { text: i18nc("@item graphics card choice", "High performance"), value: true }]
                textRole: "text"
                valueRole: "value"
                currentIndex: row.model.dedicated ? 1 : 0
                onActivated: (i) => apps.setDedicated(row.index, i === 1)
            }
        }
    }

    EmptyState {
        Layout.fillWidth: true
        visible: apps.count === 0
        iconName: "org.raye.settings-graphics"
        title: i18n("No apps found")
    }

    SectionHeader { text: i18n("Related settings"); visible: nvidia.visible }
    LinkCard {
        id: nvidia
        searchId: "nvidia-panel"
        visible: Router.has("nvidia-panel")
        title: i18n("NVIDIA Control Panel")
        description: i18n("Settings of your NVIDIA graphics card, from NVIDIA")
        iconName: "nvidia-settings"
        iconFallback: "org.raye.settings-graphics"
        onClicked: apps.openNvidiaPanel()
    }
}
