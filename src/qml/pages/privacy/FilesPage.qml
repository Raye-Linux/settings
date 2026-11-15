// SPDX-License-Identifier: GPL-3.0-or-later
// Privacy & security › File system: which Store apps may reach all your files.
import QtQuick
import org.raye.settings

AppPermissionPage {
    kind: "files"
    searchId: "files-access"
    pageTitle: i18nc("@title settings page", "File system")
    masterTitle: i18nc("@label", "Let apps reach all your files")
    masterDescription: i18nc("@info", "Most apps only see the files you open with them. These were made to see your whole home folder, or the whole PC.")
    listTitle: i18nc("@title:group", "Choose which apps can reach all your files")
    emptyTitle: i18nc("@info", "No app can reach all your files")
    emptyMessage: i18nc("@info", "Apps from Raye Store only see the files you open with them.")
    iconName: "org.raye.settings-privacy-files"
    iconFallback: "folder"
    footnote: i18nc("@info", "A change applies the next time the app opens.")
}
