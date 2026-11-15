// SPDX-License-Identifier: GPL-3.0-or-later
// Privacy & security › Camera: which Store apps may use the camera.
import QtQuick
import org.raye.settings

AppPermissionPage {
    kind: "camera"
    searchId: "camera-access"
    pageTitle: i18nc("@title settings page", "Camera")
    masterTitle: i18nc("@label", "Let apps use your camera")
    masterDescription: i18nc("@info", "Apps ask the first time they want to see you.")
    listTitle: i18nc("@title:group", "Choose which apps can use your camera")
    emptyTitle: i18nc("@info", "No app has asked to use your camera yet")
    emptyMessage: i18nc("@info", "When an app from Raye Store asks, it appears here.")
    iconName: "org.raye.settings-privacy-camera"
    iconFallback: "camera-web"
}
