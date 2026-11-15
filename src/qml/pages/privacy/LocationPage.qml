// SPDX-License-Identifier: GPL-3.0-or-later
// Privacy & security › Location: which Store apps may know where you are.
import QtQuick
import org.raye.settings

AppPermissionPage {
    kind: "location"
    searchId: "location-access"
    pageTitle: i18nc("@title settings page", "Location")
    masterTitle: i18nc("@label", "Let apps know your location")
    masterDescription: i18nc("@info", "Apps ask the first time they want to know where you are.")
    listTitle: i18nc("@title:group", "Choose which apps can know your location")
    emptyTitle: i18nc("@info", "No app has asked for your location yet")
    emptyMessage: i18nc("@info", "When an app from Raye Store asks, it appears here.")
    iconName: "org.raye.settings-location"
    iconFallback: "mark-location"
}
