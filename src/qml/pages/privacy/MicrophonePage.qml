// SPDX-License-Identifier: GPL-3.0-or-later
// Privacy & security › Microphone: which Store apps may use the microphone.
import QtQuick
import org.raye.settings

AppPermissionPage {
    kind: "microphone"
    searchId: "microphone-access"
    pageTitle: i18nc("@title settings page", "Microphone")
    masterTitle: i18nc("@label", "Let apps use your microphone")
    masterDescription: i18nc("@info", "Apps ask the first time they want to hear you.")
    listTitle: i18nc("@title:group", "Choose which apps can use your microphone")
    emptyTitle: i18nc("@info", "No app has asked to use your microphone yet")
    emptyMessage: i18nc("@info", "When an app from Raye Store asks, it appears here.")
    iconName: "org.raye.settings-microphone"
    iconFallback: "audio-input-microphone"
}
