// SPDX-License-Identifier: GPL-3.0-or-later
// Privacy & security › Screenshots: which Store apps may take screenshots and record the screen.
import QtQuick
import org.raye.settings

AppPermissionPage {
    kind: "screenshot"
    searchId: "screenshots-access"
    pageTitle: i18nc("@title settings page", "Screenshots")
    masterTitle: i18nc("@label", "Let apps take screenshots")
    masterDescription: i18nc("@info", "Apps ask before they capture what is on your screen.")
    listTitle: i18nc("@title:group", "Choose which apps can take screenshots")
    emptyTitle: i18nc("@info", "No app has asked to take screenshots yet")
    emptyMessage: i18nc("@info", "When an app from Raye Store asks, it appears here.")
    iconName: "org.raye.settings-screenshots"
    iconFallback: "camera-photo"
}
