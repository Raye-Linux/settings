// SPDX-License-Identifier: GPL-3.0-or-later
// Gaming > Game Mode: whether the PC can hand itself over to a game, the
// frame rate overlay, and the other pages of the category.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Gaming"), target: "gaming" },
             { text: i18nc("@title settings page", "Game Mode") }]

    readonly property var sys: typeof system !== "undefined" ? system : null
    readonly property bool gameMode: sys !== null && sys.gamemode

    Component.onCompleted: if (sys) sys.refresh()

    Card {
        Layout.fillWidth: true
        searchId: "game-mode"
        iconName: "org.raye.settings-game-mode"
        iconFallback: "applications-games"
        title: i18nc("@title", "Game Mode")
        description: page.gameMode
                     ? i18nc("@info", "Games that ask for it get the whole PC while they run, and updates wait until you finish.")
                     : i18nc("@info", "Not installed on this PC.")
        StatusGlyph {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            status: page.gameMode ? "success" : "caution"
            Accessible.name: page.gameMode ? i18nc("@info", "Ready") : i18nc("@info", "Not installed")
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
}
