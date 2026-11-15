// SPDX-License-Identifier: GPL-3.0-or-later
// The notifications of one app, over the Notifications list: the app's
// switch, banners, the notification centre, do not disturb, the count
// on its taskbar button; its sounds in the desktop's own window.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    required property NotificationSettings settings
    required property string appId

    // read again whenever a switch changes something
    property var app: settings.app(appId)
    signal closed()

    Connections {
        target: page.settings
        function onChanged() { page.app = page.settings.app(page.appId) }
        function onAppsChanged() { page.app = page.settings.app(page.appId) }
    }

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Notifications"), target: "notifications-list" },
             { text: app.name || "" }]
    showBack: true
    followAnchors: false
    onBackClicked: page.closed()
    // the parent crumb goes back to the list, not to a new page
    onCrumbActivated: (target) => {
        if (target === "notifications-list")
            page.closed()
        else if (target !== "")
            Router.navigate(target)
    }
    Keys.onEscapePressed: page.closed()

    function set(option, on) {
        page.settings.setAppOption(page.appId, option, on)
    }

    ToggleCard {
        iconName: page.app.icon || "application-x-executable"
        iconFallback: "application-x-executable"
        title: i18nc("@title", "Notifications")
        description: i18nc("@info", "Let this app send notifications")
        checked: page.app.enabled === true
        onToggled: page.set("enabled", checked)
    }

    ToggleCard {
        title: i18nc("@title", "Show notification banners")
        description: i18nc("@info", "A banner shows for a few seconds at the bottom right of the screen")
        enabled: page.app.enabled === true
        checked: page.app.popups === true
        onToggled: page.set("popups", checked)
    }
    ToggleCard {
        title: i18nc("@title", "Show notifications in the notification center")
        description: i18nc("@info", "Keep them until you clear them")
        enabled: page.app.enabled === true
        checked: page.app.history === true
        onToggled: page.set("history", checked)
    }
    ToggleCard {
        title: i18nc("@title", "Show banners when do not disturb is on")
        description: i18nc("@info", "For apps whose notifications matter most")
        enabled: page.app.enabled === true && page.app.popups === true
        checked: page.app.dnd === true
        onToggled: page.set("dnd", checked)
    }
    ToggleCard {
        title: i18nc("@title", "Show a count on the taskbar button")
        description: i18nc("@info", "How many unread notifications the app has")
        enabled: page.app.enabled === true
        checked: page.app.badges === true
        onToggled: page.set("badges", checked)
    }

    SectionHeader { text: i18nc("@title", "Related settings") }

    Card {
        iconName: "org.raye.settings-sounds"
        iconFallback: "preferences-desktop-sound"
        title: i18nc("@title", "Sounds and more options")
        description: i18nc("@info", "The sound of each kind of notification from this app")
        clickable: true
        actionGlyph: "open-external"
        onClicked: page.settings.openMoreOptions(page.appId)
    }
}
