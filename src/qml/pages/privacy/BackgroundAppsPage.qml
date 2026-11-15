// SPDX-License-Identifier: GPL-3.0-or-later
// Privacy & security › Background apps: which Store apps may keep running when their window is closed.
import QtQuick
import org.raye.settings

AppPermissionPage {
    kind: "background"
    searchId: "background-access"
    pageTitle: i18nc("@title settings page", "Background apps")
    masterTitle: i18nc("@label", "Let apps run in the background")
    masterDescription: i18nc("@info", "Apps can keep working when you close their window: messages, music, downloads.")
    listTitle: i18nc("@title:group", "Choose which apps can run in the background")
    emptyTitle: i18nc("@info", "No app has asked to run in the background yet")
    emptyMessage: i18nc("@info", "When an app from Raye Store asks, it appears here.")
    iconName: "org.raye.settings-background-apps"
    iconFallback: "system-run"
}
