// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Update: what updating is doing now, with the one button that
// moves it on (check, restart, resume), the updates waiting with their
// download progress, then pause, history and advanced options.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.raye.settings
import "updatetext.js" as U

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings page", "Raye Update") }]

    readonly property var status: typeof updates !== "undefined" ? updates : null
    property real now: Date.now()
    // no ticking at idle: one wake-up when a pause runs out
    Timer {
        readonly property real left: page.status ? page.status.pausedUntil * 1000 - page.now : 0
        running: left > 0
        interval: Math.min(left + 1000, 2147483647)
        onTriggered: page.now = Date.now()
    }

    readonly property string state_: U.pageState(page.status, page.now)
    readonly property bool busy: state_ === "checking" || state_ === "downloading" || state_ === "installing"
    readonly property int updateCount: status ? status.pending.length + status.firmware.length : 0
    readonly property real progress: state_ === "installing" ? page.status.installPercent / 100 : U.downloadProgress(page.status)
    // downloaded updates that install without a restart
    readonly property int liveCount: status ? status.pending.filter(p => !U.needsRestart(p)).length : 0
    UpdateNoticeText { id: notice; notice: U.sourceNotice(page.status) }

    function lastChecked() {
        if (!status || status.checked <= 0)
            return i18nc("@info", "Not checked for updates yet")
        const when = new Date(status.checked * 1000)
        const today = new Date(page.now)
        const time = when.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
        if (when.toDateString() === today.toDateString())
            return i18nc("@info %1 is a time", "Last checked: today, %1", time)
        const yesterday = new Date(page.now - 86400000)
        if (when.toDateString() === yesterday.toDateString())
            return i18nc("@info %1 is a time", "Last checked: yesterday, %1", time)
        return i18nc("@info %1 is a date", "Last checked: %1", when.toLocaleDateString(Qt.locale(), Locale.ShortFormat))
    }

    function errorText(code) {
        switch (code) {
        case "no-internet": return i18nc("@info", "We couldn't connect to the update service. Check your internet connection.")
        case "server": return i18nc("@info", "The update service didn't answer. We'll try again later.")
        case "no-space": return i18nc("@info", "There isn't enough free space to update. Free up some space and try again.")
        case "broken": return i18nc("@info", "Some updates can't be installed together. We'll try again later.")
        case "install-failed": return i18nc("@info", "Updates couldn't be installed. Your PC was not changed.")
        case "firmware-failed": return i18nc("@info", "A firmware update couldn't be installed.")
        default: return i18nc("@info", "Something went wrong. We'll try again later.")
        }
    }

    function waitText(reason) {
        switch (reason) {
        case "battery": return i18nc("@info", "Waiting until your PC is plugged in.")
        case "metered": return i18nc("@info", "Waiting for a connection that isn't metered.")
        case "game": return i18nc("@info", "Waiting until you finish playing.")
        default: return i18nc("@info", "Waiting for another install to finish.")
        }
    }

    function sizeText(bytes) {
        return bytes >= 1073741824 ? i18nc("@info size", "%1 GB", (bytes / 1073741824).toFixed(1))
             : bytes >= 1048576 ? i18nc("@info size", "%1 MB", Math.max(1, Math.round(bytes / 1048576)))
             : i18nc("@info size", "%1 KB", Math.max(1, Math.round(bytes / 1024)))
    }

    function kindText(kind) {
        switch (kind) {
        case "kernel": return i18nc("@info kind of update", "Kernel")
        case "driver": return i18nc("@info kind of update", "Driver")
        case "security": return i18nc("@info kind of update", "Security")
        case "app": return i18nc("@info kind of update", "App")
        default: return i18nc("@info kind of update", "System")
        }
    }

    // --- the status at the top ------------------------------------------------
    Rectangle {
        id: hero
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        implicitHeight: heroRow.implicitHeight + 2 * Theme.s7
        radius: Theme.radiusOverlay
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder
        Accessible.role: Accessible.StaticText
        Accessible.name: heroTitle.text + ", " + heroMessage.text + (heroNotice.text !== "" ? ", " + heroNotice.text : "")

        RowLayout {
            id: heroRow
            anchors.fill: parent
            anchors.margins: Theme.s7
            spacing: Theme.s6

            // two icons: the spinner turns, the status never does. One
            // icon for both kept the angle the animation stopped at, and
            // the check mark came out tilted after a check.
            Item {
                Layout.alignment: Qt.AlignVCenter
                Layout.preferredWidth: 64
                Layout.preferredHeight: 64

                Kirigami.Icon {
                    id: heroIcon
                    objectName: "heroStatusIcon"
                    anchors.fill: parent
                    visible: page.state_ !== "checking"
                    source: page.state_ === "restart" || page.state_ === "armed" ? "org.raye.settings-status-restart"
                          : page.busy ? "org.raye.settings-status-updating"
                          : page.state_ === "error" || page.state_ === "incomplete" ? "org.raye.settings-status-warning"
                          : page.state_ === "paused" || page.state_ === "waiting" ? "org.raye.settings-status-info"
                          : ["never", "available", "install"].indexOf(page.state_) >= 0 ? "org.raye.settings-raye-update"
                          : "org.raye.settings-status-ok"
                    fallback: "update-none"
                }

                // the bar shows a download moving; only a check, which is
                // short and has no bar value, turns the icon
                Kirigami.Icon {
                    objectName: "heroSpinner"
                    anchors.fill: parent
                    visible: page.state_ === "checking"
                    source: "org.raye.settings-status-updating"
                    fallback: "update-none"
                    RotationAnimator on rotation {
                        running: page.state_ === "checking" && !Theme.animationsOff
                        from: 0; to: 360
                        duration: 1600
                        loops: Animation.Infinite
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.s1

                Text {
                    id: heroTitle
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font: Theme.subtitle
                    color: Theme.text
                    text: {
                        switch (page.state_) {
                        case "paused": return i18nc("@title", "Updates are paused")
                        case "armed": return i18nc("@title", "Your PC will update when it restarts")
                        case "restart": return page.status.state === "ready" || page.updateCount > 0
                                               ? i18nc("@title", "Restart required")
                                               : i18nc("@title", "Restart to finish updating")
                        case "downloading": return i18nc("@title", "Downloading updates")
                        case "installing": return i18nc("@title", "Installing updates")
                        case "install": return i18ncp("@title", "One update is ready to install", "%1 updates are ready to install", page.liveCount)
                        case "checking": return i18nc("@title", "Checking for updates…")
                        case "waiting": return i18ncp("@title", "One update is waiting", "%1 updates are waiting", page.updateCount)
                        case "error": return i18nc("@title", "Updates need attention")
                        case "available": return i18ncp("@title", "One update available", "%1 updates available", page.updateCount)
                        case "uptodate": return i18nc("@title", "You're up to date")
                        case "incomplete": return i18nc("@title", "We couldn't check everything")
                        default: return i18nc("@title", "Check for updates")
                        }
                    }
                }
                Text {
                    id: heroMessage
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font: Theme.body
                    color: page.state_ === "error" ? Theme.critical : Theme.textSecondary
                    text: {
                        switch (page.state_) {
                        case "paused": return i18nc("@info %1 is a date", "Until %1. Security fixes still install.",
                                                    new Date(page.status.pausedUntil * 1000).toLocaleDateString(Qt.locale(), Locale.LongFormat))
                        case "armed": return i18nc("@info", "Save your work. The updates install while your PC restarts.")
                        case "restart": return page.status.state === "ready" || page.updateCount > 0
                                               ? i18nc("@info", "Updates are downloaded. They install when you restart.")
                                               : i18nc("@info", "Some updates were installed and need a restart to finish.")
                        case "downloading": return page.progress >= 0
                                                   ? i18nc("@info %1 percent, %2 and %3 sizes", "%1% (%2 of %3)", Math.round(page.progress * 100),
                                                           page.sizeText(page.status.downloadedBytes), page.sizeText(page.status.totalBytes))
                                                   : page.lastChecked()
                        case "installing": return i18nc("@info %1 percent", "%1% done. You can keep working, no restart needed.",
                                                        Math.round(page.progress * 100))
                        case "install": if (U.installWait(page.status) === "battery")
                                            return i18nc("@info", "Plug in your PC to install these updates.")
                                        return i18ncp("@info", "It installs without a restart.", "They install without a restart.", page.liveCount)
                        case "waiting": return page.waitText(page.status.reason)
                        case "error": return page.errorText(page.status.errorCode)
                        default: return page.lastChecked()
                        }
                    }
                }
                // beside the status, never instead of it: what was found is real
                Text {
                    id: heroNotice
                    objectName: "updateNotice"
                    Layout.fillWidth: true
                    visible: text !== ""
                    wrapMode: Text.WordWrap
                    font: Theme.body
                    color: Theme.textSecondary
                    text: notice.text
                }
                ProgressBar {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.s2
                    visible: page.busy
                    indeterminate: page.state_ === "checking" || page.progress <= 0
                    value: Math.max(0, page.progress)
                    Accessible.name: heroTitle.text
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.s2
                Button {
                    visible: page.state_ === "restart" || page.state_ === "armed"
                    accent: true
                    enabled: !page.status || !page.status.restarting
                    text: i18nc("@action:button", "Restart now")
                    onClicked: restartDialog.open()
                }
                Button {
                    objectName: "installNowButton"
                    visible: page.state_ === "install"
                    accent: true
                    text: i18nc("@action:button", "Install now")
                    onClicked: page.status.installNow()
                }
                Button {
                    visible: page.state_ === "armed"
                    enabled: !page.status || !page.status.restarting
                    text: i18nc("@action:button", "Cancel")
                    onClicked: page.status.cancelInstall()
                }
                Button {
                    visible: page.state_ === "paused"
                    accent: true
                    text: i18nc("@action:button", "Resume updates")
                    onClicked: page.status.resume()
                }
                Button {
                    // as on Windows, no button while it checks or downloads
                    visible: ["restart", "armed", "paused", "install"].indexOf(page.state_) < 0 && !page.busy
                    accent: page.state_ !== "available"
                    enabled: page.status !== null
                    text: page.state_ === "error" ? i18nc("@action:button", "Try again")
                                                  : i18nc("@action:button", "Check for updates")
                    onClicked: page.status.checkNow()
                }
            }
        }
    }

    // --- what is waiting ------------------------------------------------------
    Expander {
        visible: page.updateCount > 0 && page.state_ !== "paused"
        expanded: page.updateCount <= 8
        iconName: "org.raye.settings-raye-update"
        iconFallback: "system-software-update"
        title: i18ncp("@title", "One update", "%1 updates", page.updateCount)
        description: page.state_ === "downloading" ? i18nc("@info", "Downloading")
                   : page.state_ === "installing" ? i18nc("@info", "Installing")
                   : ["restart", "armed", "install"].indexOf(page.state_) >= 0 ? i18nc("@info", "Ready to install")
                   : page.state_ === "waiting" ? i18nc("@info", "Waiting to download")
                   : i18nc("@info", "Downloads on its own")

        Repeater {
            model: page.status ? page.status.firmware : []
            delegate: ExpanderRow {
                required property var modelData
                title: modelData.device
                description: modelData.needsPower
                             ? i18nc("@info firmware update, %1 and %2 are versions", "Firmware %1 → %2 · Plug in the charger", modelData.from, modelData.to)
                             : i18nc("@info firmware update, %1 and %2 are versions", "Firmware %1 → %2", modelData.from, modelData.to)
                Accessible.description: description + ", " + restartTag.text
                RestartTag { id: restartTag }
            }
        }
        Repeater {
            model: page.status ? page.status.pending.slice(0, 60) : []
            delegate: ExpanderRow {
                required property var modelData
                title: modelData.name
                Accessible.description: description + (tag.visible ? ", " + tag.text : "")
                RestartTag { id: tag; visible: U.needsRestart(modelData) }
                description: modelData.from !== ""
                             ? i18nc("@info %1 kind of update, %2 and %3 versions", "%1 · %2 → %3", page.kindText(modelData.kind), modelData.from, modelData.to)
                             : i18nc("@info %1 kind of update, %2 version", "%1 · new, %2", page.kindText(modelData.kind), modelData.to)
            }
        }
        ExpanderRow {
            visible: page.status !== null && page.status.pending.length > 60
            title: page.status ? i18nc("@info", "And %1 more", page.status.pending.length - 60) : ""
        }
    }

    // the words on each update that waits for a restart
    component RestartTag: Rectangle {
        readonly property alias text: label.text
        implicitWidth: label.implicitWidth + 2 * Theme.s2
        implicitHeight: label.implicitHeight + Theme.s1
        radius: height / 2
        color: "transparent"
        border.width: 1
        border.color: Theme.cardBorder
        Accessible.ignored: true
        Text {
            id: label
            anchors.centerIn: parent
            text: i18nc("@info tag on an update", "Restart required")
            font: Theme.caption
            color: Theme.textSecondary
        }
    }

    // --- more options ---------------------------------------------------------
    SectionHeader { text: i18nc("@title:group", "More options") }

    ComboCard {
        id: pauseCard
        searchId: "pause"
        iconName: "org.raye.settings-status-info"
        iconFallback: "media-playback-pause"
        title: i18nc("@title", "Pause updates")
        description: page.state_ === "paused"
                     ? i18nc("@info %1 is a date", "Paused until %1", new Date(page.status.pausedUntil * 1000).toLocaleDateString(Qt.locale(), Locale.ShortFormat))
                     : i18nc("@info", "Nothing is checked or downloaded while paused. Security fixes still install.")
        enabled: page.status !== null && page.state_ !== "paused"
        model: [i18nc("@item", "Pause for 1 week"), i18nc("@item", "Pause for 2 weeks"), i18nc("@item", "Pause for 3 weeks"),
                i18nc("@item", "Pause for 4 weeks"), i18nc("@item", "Pause for 5 weeks")]
        currentIndex: 0
        onActivated: (index) => page.status.pause(index + 1)
    }

    NavCard {
        searchId: "update-history"
        iconName: "org.raye.settings-update-history"
        iconFallback: "view-history"
        title: i18nc("@title settings page", "Update history")
        target: "update/history"
    }

    NavCard {
        searchId: "update-advanced"
        iconName: "org.raye.settings-update-advanced"
        iconFallback: "preferences-other"
        title: i18nc("@title settings page", "Advanced options")
        description: i18nc("@info", "Active hours, restarts")
        target: "update/advanced"
    }

    // --- restart ---------------------------------------------------------------
    ContentDialog {
        id: restartDialog
        title: i18nc("@title:window", "Restart now?")
        text: page.status && (page.status.state === "ready" || page.updateCount > 0)
              ? i18n("Save your work first. Raye installs the updates while it restarts.")
              : i18n("Save your work first. Your PC restarts to finish installing updates.")
        primaryText: i18nc("@action:button", "Restart now")
        closeText: i18nc("@action:button", "Not now")
        onAccepted: page.status.restartAndInstall()
    }

    ContentDialog {
        id: restartError
        title: i18nc("@title:window", "Couldn't restart")
        closeText: i18nc("@action:button", "Close")
    }

    Connections {
        target: page.status
        function onRestartFailed(reason) {
            restartError.text = reason === "cancelled" ? i18n("The updates weren't set up because the password wasn't entered. Try again.")
                              : reason === "failed" ? i18n("The updates couldn't be set up to install. Your PC wasn't restarted. Try again later.")
                              : i18n("Your PC couldn't be restarted. Save your work and restart it from Start.")
            restartError.open()
        }
    }
}
