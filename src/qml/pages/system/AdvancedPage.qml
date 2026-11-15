// SPDX-License-Identifier: GPL-3.0-or-later
// System > Advanced: Files options people look for here, End task on
// the taskbar, the terminal app, and the settings for developers.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Advanced") }]

    AdvancedSettings { id: advanced }

    SectionHeader {
        text: i18n("Files")
        first: true
    }

    ToggleCard {
        iconName: "org.raye.settings-privacy-files"
        iconFallback: "folder"
        title: i18n("Show file extensions")
        description: i18n("Show the end of file names, like .pdf or .jpg, for every file")
        searchId: "file-extensions"
        checked: advanced.showExtensions
        onToggled: advanced.showExtensions = checked
    }

    ToggleCard {
        iconName: "org.raye.settings-privacy-files"
        iconFallback: "view-hidden"
        title: i18n("Show hidden and system files")
        description: i18n("Show the files and folders that are hidden to keep them safe")
        searchId: "hidden-files"
        checked: advanced.showHidden
        onToggled: advanced.showHidden = checked
    }

    SectionHeader { text: i18n("Taskbar") }

    ToggleCard {
        iconName: "org.raye.settings-taskbar"
        iconFallback: "process-stop"
        title: i18n("End task")
        description: i18n("Add End task to the menu of a taskbar button, to close an app that stopped responding")
        searchId: "end-task"
        checked: advanced.endTask
        onToggled: advanced.endTask = checked
    }

    SectionHeader { text: i18n("Terminal") }

    ComboCard {
        iconName: "org.raye.settings-advanced"
        iconFallback: "utilities-terminal"
        title: i18n("Terminal")
        description: i18n("Choose the app that opens when another app needs a terminal")
        searchId: "terminal"
        visible: advanced.terminals.length > 0
        model: advanced.terminals
        textRole: "name"
        valueRole: "id"
        currentIndex: {
            const list = advanced.terminals
            for (let i = 0; i < list.length; ++i) {
                if (list[i].id === advanced.terminal)
                    return i
            }
            return -1
        }
        onActivated: (index) => advanced.terminal = currentValue
    }

    InfoCard {
        iconName: "org.raye.settings-advanced"
        iconFallback: "utilities-terminal"
        title: i18n("Terminal")
        description: i18n("Choose the app that opens when another app needs a terminal")
        searchId: "terminal"
        visible: advanced.terminals.length === 0
        value: i18n("No terminal app is installed")
        copyable: false
    }

    AdvancedDeveloperSection {}
}
