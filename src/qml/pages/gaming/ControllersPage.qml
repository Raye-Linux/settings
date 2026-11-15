// SPDX-License-Identifier: GPL-3.0-or-later
// Gaming > Controllers: the game controllers on this PC. Each one can be
// opened to try its buttons and sticks.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Gaming"), target: "gaming" },
             { text: i18nc("@title settings page", "Controllers") }]

    GameControllers { id: controllers }

    EmptyState {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s6
        visible: controllers.count === 0
        iconName: "org.raye.settings-controllers"
        title: i18nc("@info", "No controller connected")
        message: i18nc("@info", "Plug a controller in with a cable, or pair a wireless one in Bluetooth & devices.")
        Button {
            text: i18nc("@action:button", "Bluetooth & devices")
            onClicked: Router.navigate("devices")
        }
    }

    SectionHeader {
        text: i18nc("@title:group", "Connected controllers")
        first: true
        visible: controllers.count > 0
    }

    Repeater {
        model: controllers
        delegate: Expander {
            id: row
            required property string name
            required property string device
            required property string bus

            Layout.fillWidth: true
            iconName: "org.raye.settings-controllers"
            iconFallback: "input-gamepad"
            title: row.name
            description: row.bus === "bluetooth" ? i18nc("@info", "Connected with Bluetooth")
                       : row.bus === "usb" ? i18nc("@info", "Connected with a cable")
                       : i18nc("@info", "Connected")

            ControllerTester {
                id: tester
                device: row.expanded ? row.device : ""
            }

            ExpanderRow {
                title: i18nc("@label", "Buttons")
                description: !tester.working ? i18nc("@info", "This controller can't be read right now.")
                           : i18nc("@info", "Press a button: it lights up here.")
                Flow {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    width: Math.min(360, tester.buttons.length * 30)
                    spacing: 6
                    Repeater {
                        model: tester.buttons
                        delegate: Rectangle {
                            required property bool modelData
                            required property int index
                            width: 24
                            height: 24
                            radius: 12
                            color: modelData ? Theme.accent : Theme.control
                            border.width: 1
                            border.color: modelData ? Theme.accent : Theme.strokeStrong
                            Text {
                                anchors.centerIn: parent
                                text: index + 1
                                font: Theme.caption
                                color: modelData ? Theme.textOnAccent : Theme.textSecondary
                            }
                        }
                    }
                }
            }

            ExpanderRow {
                title: i18nc("@label", "Sticks and triggers")
                description: i18nc("@info", "Move them: each bar follows one axis.")
                visible: tester.axes.length > 0
                Column {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    spacing: 4
                    Repeater {
                        model: tester.axes
                        delegate: Item {
                            required property real modelData
                            width: 200
                            height: 6
                            Rectangle {
                                anchors.fill: parent
                                radius: 3
                                color: Theme.control
                                border.width: 1
                                border.color: Theme.strokeControl
                            }
                            Rectangle {
                                x: modelData < 0 ? parent.width / 2 * (1 + modelData) : parent.width / 2
                                width: Math.max(2, parent.width / 2 * Math.abs(modelData))
                                height: parent.height
                                radius: 3
                                color: Theme.accent
                            }
                        }
                    }
                }
            }
        }
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-bluetooth"
        iconFallback: "preferences-system-bluetooth"
        title: i18nc("@title settings page", "Bluetooth & devices")
        description: i18nc("@info", "Pair a wireless controller")
        target: "devices"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-game-mode"
        iconFallback: "applications-games"
        title: i18nc("@title settings page", "Game Mode")
        description: i18nc("@info", "Give the whole PC to the game you play")
        target: "gaming/game-mode"
    }
}
