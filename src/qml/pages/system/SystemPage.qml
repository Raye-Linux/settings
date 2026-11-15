// SPDX-License-Identifier: GPL-3.0-or-later
// System: the PC at the top (picture, name, model, Rename) with Raye
// Update and storage next to it, then one card per page of the
// category, as Windows' System page.
import QtQuick
import QtQuick.Layouts
import org.raye.settings
import "../update/updatetext.js" as U

PageScaffold {
    id: page

    title: i18nc("@title settings category", "System")
    showBack: false

    DeviceInfo { id: info }
    Hostname { id: pcName }

    // UpdateStatus, when the window has one
    readonly property var updateStatus: typeof updates !== "undefined" ? updates : null

    banner: Component {
        GridLayout {
            id: top
            // the cards go under the PC when there is no room beside it
            readonly property bool wide: width >= 820
            columns: wide ? 3 : 1
            columnSpacing: Theme.s4
            rowSpacing: Theme.s4

            AboutBanner {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                device: info
                name: pcName.name
                busy: pcName.busy
                pictureSize: 72
                renameButton: false
                onRenameClicked: renameDialog.start()
            }
            SystemPageCard {
                Layout.preferredWidth: top.wide ? 260 : -1
                Layout.fillWidth: !top.wide
                iconName: page.updateState === "restart" || page.updateState === "armed" ? "org.raye.settings-status-restart"
                        : page.updateState === "error" || updateNotice.text !== "" ? "org.raye.settings-status-warning"
                        : page.busy ? "org.raye.settings-status-updating" : "org.raye.settings-status-ok"
                title: i18nc("@title", "Raye Update")
                text: page.updateText()
                onClicked: Router.navigate("update")
            }
            SystemPageCard {
                Layout.preferredWidth: top.wide ? 260 : -1
                Layout.fillWidth: !top.wide
                iconName: "org.raye.settings-storage"
                title: i18nc("@title", "Storage")
                text: info.storageTotal > 0
                      ? i18nc("@info %1 free space, %2 size of the drive", "%1 free of %2",
                              info.formatSize(info.storageFree), info.formatSize(info.storageTotal))
                      : ""
                progress: info.storageTotal > 0 ? 1 - info.storageFree / info.storageTotal : -1
                onClicked: Router.navigate("system/storage")
            }
        }
    }

    readonly property string updateState: U.pageState(updateStatus, Date.now())
    readonly property bool busy: updateState === "checking" || updateState === "downloading"
    UpdateNoticeText { id: updateNotice; notice: U.sourceNotice(page.updateStatus) }

    function updateText() {
        if (!updateStatus)
            return i18nc("@info", "Check for updates")
        // what could not be read matters more than the rest of the line
        if (updateNotice.text !== "" && ["armed", "paused"].indexOf(updateState) < 0)
            return updateNotice.text
        switch (updateState) {
        case "restart":
        case "armed": return i18nc("@info", "Restart to finish updating")
        case "checking": return i18nc("@info", "Checking for updates…")
        case "downloading": return i18nc("@info", "Downloading updates…")
        case "paused": return i18nc("@info", "Updates are paused")
        case "error": return i18nc("@info", "Attention needed")
        case "never": return i18nc("@info", "Not checked for updates yet")
        }
        const waiting = U.waitingCount(updateStatus)
        if (waiting > 0)
            return i18ncp("@info", "One update ready", "%1 updates ready", waiting)
        const when = new Date(updateStatus.checked * 1000)
        return i18nc("@info %1 a date or a time", "Last checked: %1",
                     when.toDateString() === new Date().toDateString()
                     ? when.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
                     : when.toLocaleDateString(Qt.locale(), Locale.ShortFormat))
    }

    // [page id, glyph, fallback, title, description]
    readonly property var entries: [
        ["system/display", "org.raye.settings-display", "preferences-desktop-display", i18nc("@title settings page", "Display"),
         i18nc("@info", "Monitors, brightness, night light, scale")],
        ["system/sound", "org.raye.settings-sound", "audio-volume-high", i18nc("@title settings page", "Sound"),
         i18nc("@info", "Volume levels, output, input, sound devices")],
        ["system/notifications", "org.raye.settings-notifications", "preferences-desktop-notification-bell", i18nc("@title settings page", "Notifications"),
         i18nc("@info", "Alerts from apps and the system, do not disturb")],
        ["system/focus", "org.raye.settings-focus", "clock", i18nc("@title settings page", "Focus"),
         i18nc("@info", "Fewer distractions while you work")],
        ["system/power", "org.raye.settings-power", "battery",
         Router.has("battery") ? i18nc("@title settings page", "Power & battery") : i18nc("@title settings page on a PC without a battery", "Power"),
         Router.has("battery") ? i18nc("@info", "Sleep, battery usage, energy saver") : i18nc("@info", "Sleep, power mode, energy saver")],
        ["system/storage", "org.raye.settings-storage", "drive-harddisk", i18nc("@title settings page", "Storage"),
         i18nc("@info", "Storage space, clean up, Storage Sense")],
        ["system/multitasking", "org.raye.settings-multitasking", "preferences-system-windows", i18nc("@title settings page", "Multitasking"),
         i18nc("@info", "Snap windows, desktops, task switching")],
        ["system/graphics", "org.raye.settings-graphics", "preferences-desktop-display", i18nc("@title settings page", "Graphics"),
         i18nc("@info", "Choose which graphics card an app uses")],
        ["system/clipboard", "org.raye.settings-clipboard", "edit-paste", i18nc("@title settings page", "Clipboard"),
         i18nc("@info", "Copy history, clear")],
        ["system/remote-desktop", "org.raye.settings-remote-desktop", "krdc", i18nc("@title settings page", "Remote Desktop"),
         i18nc("@info", "Connect to this PC from another one")],
        ["system/advanced", "org.raye.settings-advanced", "preferences-other", i18nc("@title settings page", "Advanced"),
         i18nc("@info", "File extensions, hidden files, terminal")],
        ["system/troubleshoot", "org.raye.settings-troubleshoot", "tools-wizard", i18nc("@title settings page", "Troubleshoot"),
         i18nc("@info", "Find and fix common problems")],
        ["system/recovery", "org.raye.settings-recovery", "edit-undo", i18nc("@title settings page", "Recovery"),
         i18nc("@info", "Repair, go back, reset, advanced startup")],
        ["system/about", "org.raye.settings-about", "help-about", i18nc("@title settings page", "About"),
         i18nc("@info", "Device specifications, rename PC, Raye specifications")],
    ]

    Repeater {
        model: page.entries.filter(e => Router.isAvailable(e[0]))
        delegate: NavCard {
            required property var modelData
            Layout.fillWidth: true
            iconName: modelData[1]
            iconFallback: modelData[2]
            title: modelData[3]
            description: modelData[4]
            target: modelData[0]
        }
    }

    AboutRenameDialog {
        id: renameDialog
        objectName: "renameDialog"
        hostname: pcName
    }
}
