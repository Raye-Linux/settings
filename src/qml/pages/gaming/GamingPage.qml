// SPDX-License-Identifier: GPL-3.0-or-later
// Gaming > Game Mode: the switch and what it does while a game runs, the
// games it found (each can be turned off, and any Windows program on),
// the frame rate overlay, and the other pages of the category.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Gaming"), target: "gaming" },
             { text: i18nc("@title settings page", "Game Mode") }]

    readonly property var sys: typeof system !== "undefined" ? system : null
    readonly property bool ready: gameMode.installed && gameMode.enabled

    Component.onCompleted: if (sys) sys.refresh()

    GameModeSettings { id: gameMode }

    Expander {
        id: modeExpander
        Layout.fillWidth: true
        searchId: "game-mode"
        iconName: "org.raye.settings-game-mode"
        iconFallback: "applications-games"
        title: i18nc("@title", "Game Mode")
        description: !gameMode.installed
                     ? i18nc("@info", "Not installed on this PC.")
                     : gameMode.active
                       ? i18nc("@info", "On now: a game is running and has the whole PC.")
                       : i18nc("@info", "Optimize your PC for play. Turns on by itself when a game starts, and everything goes back when you stop.")
        expanded: gameMode.installed

        headerContent: [
            Row {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                spacing: Theme.s3
                visible: gameMode.installed
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: gameMode.enabled ? i18nc("@label a setting that is on", "On")
                                           : i18nc("@label a setting that is off", "Off")
                    font: Theme.body
                    color: Theme.text
                    Accessible.ignored: true
                }
                ToggleSwitch {
                    anchors.verticalCenter: parent.verticalCenter
                    text: i18nc("@title", "Game Mode")
                    checked: gameMode.enabled
                    onToggled: gameMode.enabled = checked
                }
            }
        ]

        ExpanderRow {
            enabled: page.ready
            title: i18nc("@label", "Turn off transparency effects while playing")
            description: i18nc("@info", "Blur and see-through backgrounds pause, so the graphics card works for the game alone")
            searchId: "game-mode-effects"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@label", "Turn off transparency effects while playing")
                checked: gameMode.effects
                onToggled: gameMode.effects = checked
            }
        }
        ExpanderRow {
            enabled: page.ready
            visible: gameMode.schedulerAvailable
            title: i18nc("@label", "Game scheduler")
            description: i18nc("@info", "Keeps frame times steady when something else runs in the background")
            searchId: "game-mode-scheduler"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@label", "Game scheduler")
                checked: gameMode.scheduler
                onToggled: gameMode.scheduler = checked
            }
        }
        ExpanderRow {
            enabled: page.ready
            title: i18nc("@label", "Best performance power mode")
            description: i18nc("@info", "While the PC is plugged in. Your own power mode comes back when you stop playing")
            searchId: "game-mode-power"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@label", "Best performance power mode")
                checked: gameMode.power
                onToggled: gameMode.power = checked
            }
        }
    }

    ToggleCard {
        Layout.fillWidth: true
        searchId: "frame-rate-overlay"
        iconName: "org.raye.settings-game-mode"
        iconFallback: "speedometer"
        title: i18nc("@title", "Frame rate overlay")
        description: i18nc("@info", "Shows frames per second and temperatures in every game. Applies the next time you sign in.")
        enabled: page.sys !== null
        checked: page.sys !== null && page.sys.mangohud
        onCheckedChanged: if (page.sys && checked !== page.sys.mangohud) page.sys.mangohud = checked
    }

    SectionHeader {
        visible: gameMode.installed
        text: i18nc("@title:group", "Games")
    }
    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s2
        visible: gameMode.installed
        wrapMode: Text.WordWrap
        font: Theme.body
        color: Theme.textSecondary
        text: gameMode.games.length > 0
              ? i18nc("@info", "Game Mode turns on for these when they run. Turn it off for one that plays better without it, or on for a Windows program that is a game.")
              : i18nc("@info", "Games you play show up here, from Steam, the Store and Windows. You can turn Game Mode off for any of them.")
    }
    Repeater {
        model: gameMode.installed ? gameMode.games : []
        delegate: ToggleCard {
            required property var modelData
            Layout.fillWidth: true
            enabled: page.ready
            iconName: modelData.source === "windows" ? "org.raye.settings-windows-programs" : "org.raye.settings-game-mode"
            iconFallback: modelData.source === "windows" ? "application-x-ms-dos-executable" : "applications-games"
            title: modelData.name
            description: modelData.source === "steam" ? i18nc("@info where a game comes from", "Steam")
                         : modelData.source === "store" ? i18nc("@info where a game comes from", "Raye Store")
                         : i18nc("@info where a game comes from", "Windows program")
            checked: modelData.on
            onToggled: gameMode.setGameOn(modelData.key, checked)
        }
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-controllers"
        iconFallback: "input-gamepad"
        title: i18nc("@title settings page", "Controllers")
        description: i18nc("@info", "Game controllers connected to this PC")
        target: "gaming/controllers"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-captures"
        iconFallback: "folder-videos"
        title: i18nc("@title settings page", "Captures")
        description: i18nc("@info", "Where recordings and screenshots go")
        target: "gaming/captures"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-windows-programs"
        iconFallback: "application-x-ms-dos-executable"
        title: i18nc("@title settings page", "Windows programs")
        description: i18nc("@info", "Windows programs you have run")
        target: "gaming/windows-programs"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-display"
        iconFallback: "video-display"
        title: i18nc("@title settings page", "Display")
        description: i18nc("@info", "Refresh rate and adaptive sync, for smoother games")
        target: "system/display"
    }
}
