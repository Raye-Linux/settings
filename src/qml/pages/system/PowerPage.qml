// SPDX-License-Identifier: GPL-3.0-or-later
// System › Power & battery ("Power" on a PC without a battery): the
// charge and time left, energy saver, the battery's day, when the
// screen turns off and the PC sleeps, the power mode, and what the lid
// and the power button do. Everything applies at once.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    PowerSettings { id: powerSettings }

    // a battery is known from the router at once, before UPower answers
    readonly property bool laptop: powerSettings.hasBattery || Router.has("battery")
    readonly property var timeouts: powerSettings.timeoutChoices()

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: laptop ? i18nc("@title settings page", "Power & battery")
                            : i18nc("@title settings page on a PC without a battery", "Power") }]

    banner: page.laptop && powerSettings.hasBattery ? batteryBanner : null
    Component {
        id: batteryBanner
        PowerBatteryBanner { power: powerSettings }
    }

    // --- Energy saver -----------------------------------------------------
    Expander {
        searchId: "energy-saver"
        visible: powerSettings.profilesAvailable && powerSettings.profiles.indexOf("power-saver") >= 0
        iconName: "org.raye.settings-power"
        iconFallback: "battery-profile-powersave"
        title: i18nc("@title", "Energy saver")
        description: page.laptop ? i18nc("@info", "Make the battery last longer by limiting some background activity")
                                 : i18nc("@info", "Use less power by limiting some background activity")
        headerContent: Row {
            spacing: Theme.s4
            Text {
                anchors.verticalCenter: parent.verticalCenter
                font: Theme.body
                color: Theme.text
                text: powerSettings.energySaver ? i18nc("@label a setting that is on", "On")
                                        : i18nc("@label a setting that is off", "Off")
            }
            ToggleSwitch {
                anchors.verticalCenter: parent.verticalCenter
                text: i18nc("@title", "Energy saver")
                checked: powerSettings.energySaver
                onToggled: powerSettings.energySaver = checked
            }
        }

        ExpanderRow {
            searchId: "energy-saver-automatic"
            visible: page.laptop && powerSettings.energySaverAutoAvailable
            title: i18nc("@label", "Turn energy saver on automatically at")
            PowerChoiceBox {
                label: i18nc("@label", "Turn energy saver on automatically at")
                choices: [
                    { value: 0, text: i18nc("@item:inlistbox energy saver", "Never") },
                    { value: 10, text: i18nc("@item:inlistbox battery level", "%1%", 10) },
                    { value: 20, text: i18nc("@item:inlistbox battery level", "%1%", 20) },
                    { value: 30, text: i18nc("@item:inlistbox battery level", "%1%", 30) },
                    { value: 40, text: i18nc("@item:inlistbox battery level", "%1%", 40) },
                    { value: 50, text: i18nc("@item:inlistbox battery level", "%1%", 50) },
                    { value: 100, text: i18nc("@item:inlistbox energy saver on whenever on battery", "Always") },
                ]
                value: powerSettings.energySaverAt
                onChosen: (v) => powerSettings.energySaverAt = v
            }
        }
        ExpanderRow {
            searchId: "energy-saver-brightness"
            visible: page.laptop && powerSettings.energySaverAutoAvailable
            title: i18nc("@label", "Lower screen brightness when using energy saver")
            ToggleSwitch {
                text: i18nc("@label", "Lower screen brightness when using energy saver")
                checked: powerSettings.energySaverDims
                onToggled: powerSettings.energySaverDims = checked
            }
        }
        ExpanderRow {
            title: i18nc("@info", "Energy saver uses the Best power efficiency mode. Turning it off goes back to the mode you had.")
        }
    }

    // --- Battery usage ---------------------------------------------------
    Expander {
        searchId: "battery-usage"
        visible: powerSettings.hasBattery
        iconName: "org.raye.settings-power"
        iconFallback: "battery"
        title: i18nc("@title", "Battery usage")
        description: i18nc("@info", "Battery level in the last 24 hours")
        PowerUsageChart {
            Layout.fillWidth: true
            Layout.maximumWidth: 720
            Layout.leftMargin: Theme.cardTextLeft
            Layout.rightMargin: Theme.s7
            Layout.topMargin: Theme.s4
            Layout.bottomMargin: Theme.s5
            history: powerSettings.history
        }
    }

    // --- Screen and sleep --------------------------------------------------
    Expander {
        searchId: "screen-and-sleep"
        iconName: "org.raye.settings-display"
        iconFallback: "preferences-desktop-display"
        title: i18nc("@title", "Screen and sleep")
        description: i18nc("@info", "The screen locks when it turns off")
        expanded: !page.laptop

        ExpanderRow {
            searchId: "screen-off-battery"
            visible: page.laptop
            title: i18nc("@label", "On battery power, turn off my screen after")
            PowerChoiceBox {
                label: i18nc("@label", "On battery power, turn off my screen after")
                choices: page.timeouts
                value: powerSettings.screenOffBattery
                onChosen: (v) => powerSettings.screenOffBattery = v
            }
        }
        ExpanderRow {
            searchId: "screen-off"
            title: page.laptop ? i18nc("@label", "When plugged in, turn off my screen after")
                               : i18nc("@label", "Turn off my screen after")
            PowerChoiceBox {
                label: i18nc("@label", "Turn off my screen after")
                choices: page.timeouts
                value: powerSettings.screenOffAc
                onChosen: (v) => powerSettings.screenOffAc = v
            }
        }
        ExpanderRow {
            searchId: "sleep-battery"
            visible: page.laptop
            title: i18nc("@label", "On battery power, put my device to sleep after")
            PowerChoiceBox {
                label: i18nc("@label", "On battery power, put my device to sleep after")
                choices: page.timeouts
                value: powerSettings.sleepBattery
                onChosen: (v) => powerSettings.sleepBattery = v
            }
        }
        ExpanderRow {
            searchId: "sleep"
            title: page.laptop ? i18nc("@label", "When plugged in, put my device to sleep after")
                               : i18nc("@label", "Put my device to sleep after")
            PowerChoiceBox {
                label: i18nc("@label", "Put my device to sleep after")
                choices: page.timeouts
                value: powerSettings.sleepAc
                onChosen: (v) => powerSettings.sleepAc = v
            }
        }
    }

    // --- Power mode --------------------------------------------------------
    SectionHeader {
        visible: powerSettings.profilesAvailable
        text: i18nc("@title", "Power mode")
    }
    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s2
        visible: powerSettings.profilesAvailable
        font: Theme.caption
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        text: i18nc("@info", "Choose between saving energy and getting the most speed out of your PC")
    }
    GridLayout {
        id: modes
        // search lands on the three cards
        property string searchId: "power-mode"
        Layout.fillWidth: true
        visible: powerSettings.profilesAvailable
        columns: width >= 660 ? 3 : 1
        columnSpacing: Theme.s3
        rowSpacing: Theme.cardSpacing

        Repeater {
            model: powerSettings.profiles
            delegate: RadioCard {
                required property string modelData
                Layout.fillWidth: true
                iconName: modelData === "power-saver" ? "battery-profile-powersave"
                        : modelData === "performance" ? "battery-profile-performance" : "battery-profile-balanced"
                title: modelData === "power-saver" ? i18nc("@title power mode", "Best power efficiency")
                     : modelData === "performance" ? i18nc("@title power mode", "Best performance")
                     : i18nc("@title power mode", "Balanced")
                description: modelData === "power-saver" ? i18nc("@info power mode", "Quieter and cooler, uses the least energy")
                           : modelData === "performance" ? i18nc("@info power mode", "The most speed, more fan and more heat")
                           : i18nc("@info power mode", "Speed when you need it, energy saved when you don't")
                checked: powerSettings.profile === modelData
                onClicked: powerSettings.profile = modelData
            }
        }
    }

    // --- Lid and power button ---------------------------------------------
    Expander {
        searchId: "lid-and-power-button"
        Layout.topMargin: powerSettings.profilesAvailable ? Theme.s7 - Theme.cardSpacing : 0
        iconName: "org.raye.settings-power"
        iconFallback: "system-shutdown"
        title: powerSettings.lidPresent ? i18nc("@title", "Lid and power button controls")
                                : i18nc("@title", "Power button controls")
        description: powerSettings.lidPresent ? i18nc("@info", "What happens when you press the power button or close the lid")
                                          : i18nc("@info", "What happens when you press the power button")

        ExpanderRow {
            searchId: "power-button-battery"
            visible: page.laptop
            title: i18nc("@label", "On battery power, pressing the power button will make my PC")
            PowerChoiceBox {
                label: i18nc("@label", "On battery power, pressing the power button will make my PC")
                choices: powerSettings.actionChoices(false, powerSettings.buttonBattery)
                value: powerSettings.buttonBattery
                onChosen: (v) => powerSettings.buttonBattery = v
            }
        }
        ExpanderRow {
            searchId: "power-button"
            title: page.laptop ? i18nc("@label", "When plugged in, pressing the power button will make my PC")
                               : i18nc("@label", "Pressing the power button will make my PC")
            PowerChoiceBox {
                label: i18nc("@label", "Pressing the power button will make my PC")
                choices: powerSettings.actionChoices(false, powerSettings.buttonAc)
                value: powerSettings.buttonAc
                onChosen: (v) => powerSettings.buttonAc = v
            }
        }
        ExpanderRow {
            searchId: "lid-battery"
            visible: powerSettings.lidPresent && page.laptop
            title: i18nc("@label", "On battery power, closing the lid will make my PC")
            PowerChoiceBox {
                label: i18nc("@label", "On battery power, closing the lid will make my PC")
                choices: powerSettings.actionChoices(true, powerSettings.lidBattery)
                value: powerSettings.lidBattery
                onChosen: (v) => powerSettings.lidBattery = v
            }
        }
        ExpanderRow {
            searchId: "lid"
            visible: powerSettings.lidPresent
            title: i18nc("@label", "When plugged in, closing the lid will make my PC")
            PowerChoiceBox {
                label: i18nc("@label", "When plugged in, closing the lid will make my PC")
                choices: powerSettings.actionChoices(true, powerSettings.lidAc)
                value: powerSettings.lidAc
                onChosen: (v) => powerSettings.lidAc = v
            }
        }
    }

    SectionHeader { text: i18nc("@title", "Related settings") }

    LinkCard {
        searchId: "more-power-options"
        iconName: "org.raye.settings-power"
        iconFallback: "preferences-system-power-management"
        title: i18nc("@title", "More power options")
        description: i18nc("@info", "Dimming, brightness for each power source, running a program when idle")
        onClicked: powerSettings.openMoreOptions()
    }
}
