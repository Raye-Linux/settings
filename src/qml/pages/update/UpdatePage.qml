// SPDX-License-Identifier: GPL-3.0-or-later
// Raye Update: what updating is doing now, with the one button that
// moves it on (check, restart, resume), the updates waiting with their
// download progress, then pause, history and advanced options.
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
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

    readonly property string rawState: U.pageState(page.status, page.now)
    // "Restart now" shows a moment after the updates finish, not on the
    // same frame (U.holdsRestart)
    property string lastRaw: rawState
    property bool restartHeld: false
    onRawStateChanged: {
        if (U.holdsRestart(lastRaw, rawState)) {
            restartHeld = true
            restartHold.restart()
        } else if (rawState !== "restart") {
            restartHeld = false
        }
        lastRaw = rawState
    }
    Timer {
        id: restartHold
        interval: U.RESTART_HOLD_MS
        onTriggered: page.restartHeld = false
    }
    readonly property string state_: restartHeld ? "installing" : rawState
    readonly property bool busy: state_ === "checking" || state_ === "downloading" || state_ === "installing"
    readonly property int updateCount: status ? status.pending.length + status.firmware.length : 0
    readonly property real progress: restartHeld ? 1 : state_ === "installing" ? page.status.installPercent / 100
                                   : state_ === "checking" ? U.checkProgress(page.status) : U.downloadProgress(page.status)
    // downloaded updates that install without a restart
    readonly property int liveCount: status ? status.pending.filter(p => !U.needsRestart(p)).length : 0
    UpdateNoticeText { id: notice; notice: U.sourceNotice(page.status) }

    // the rows the page saw install in use: they keep their check mark
    // after they leave the file, as long as the page stays open
    property var seen: ({})
    function noteRows() {
        if (!status || status.phase === "checking" || status.phase === "downloading") {
            if (Object.keys(seen).length > 0)
                seen = ({})
        } else {
            seen = U.remember(seen, status.pending, Date.now() / 1000)
        }
    }
    Component.onCompleted: noteRows()
    readonly property var rows: { page.seen; return U.shownRows(page.status, page.seen) }
    readonly property var groups: U.groupRows(rows)
    readonly property int firmwareCount: status ? status.firmware.length : 0
    // the rest of the list, with Raye's own packages in a group of their own
    readonly property var otherRows: groups.raye.length > 0 ? groups.other : rows
    readonly property bool showList: rows.length + firmwareCount > 0 && state_ !== "paused"
    readonly property string listState: state_ === "downloading" ? i18nc("@info", "Downloading")
                                      : state_ === "installing" ? i18nc("@info", "Installing")
                                      : ["restart", "armed", "install"].indexOf(state_) >= 0 ? i18nc("@info", "Ready to install")
                                      : state_ === "waiting" ? i18nc("@info", "Waiting to download")
                                      : i18nc("@info", "Downloads on its own")

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

    function timeLeftText(seconds) {
        const minutes = U.minutesLeft(seconds)
        if (minutes < 60)
            return i18ncp("@info time left", "%1 minute", "%1 minutes", minutes)
        const hours = Math.floor(minutes / 60), rest = minutes % 60
        return rest === 0 ? i18ncp("@info time left", "%1 hour", "%1 hours", hours)
                          : i18nc("@info time left, %1 hours %2 minutes", "%1 h %2 min", hours, rest)
    }

    // "45% (41 MB of 87 MB) · 4 MB/s · about 3 minutes left"; the speed and
    // the time only when raye-update knows them
    function downloadText() {
        if (page.progress < 0)
            return page.lastChecked()
        const percent = Math.round(page.progress * 100)
        const done = page.sizeText(status.downloadedBytes), total = page.sizeText(status.totalBytes)
        if (!(status.bytesPerSecond > 0))
            return i18nc("@info %1 percent, %2 and %3 sizes", "%1% (%2 of %3)", percent, done, total)
        const speed = page.sizeText(status.bytesPerSecond)
        const minutes = U.minutesLeft(status.secondsLeft)
        if (minutes < 0)
            return i18nc("@info %1 percent, %2 and %3 sizes, %4 a size per second", "%1% (%2 of %3) · %4/s",
                         percent, done, total, speed)
        if (minutes === 0)
            return i18nc("@info %1 percent, %2 and %3 sizes, %4 a size per second",
                         "%1% (%2 of %3) · %4/s · less than a minute left", percent, done, total, speed)
        return i18nc("@info %1 percent, %2 and %3 sizes, %4 a size per second, %5 a time",
                     "%1% (%2 of %3) · %4/s · about %5 left", percent, done, total, speed, page.timeLeftText(status.secondsLeft))
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
                        case "downloading": return page.downloadText()
                        case "installing": return U.restartAfter(page.status)
                                                  ? i18nc("@info %1 percent", "%1% done. A restart is needed to finish.",
                                                          Math.round(page.progress * 100))
                                                  : i18nc("@info %1 percent", "%1% done. You can keep working, no restart needed.",
                                                          Math.round(page.progress * 100))
                        case "install": if (U.installWait(page.status) === "battery")
                                            return i18nc("@info", "Plug in your PC to install these updates.")
                                        return i18ncp("@info", "It installs without a restart.", "They install without a restart.", page.liveCount)
                        case "waiting": return page.waitText(page.status.reason)
                        case "error": return page.errorText(page.status.errorCode)
                        case "available": return page.status.totalBytes > 0
                                                 ? i18nc("@info %1 a size, %2 when it last checked", "%1 to download · %2",
                                                         page.sizeText(page.status.totalBytes), page.lastChecked())
                                                 : page.lastChecked()
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
                    indeterminate: page.progress <= 0
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
    // Raye's own packages, folded: one release, not a long technical list
    Expander {
        id: rayeGroup
        objectName: "rayeUpdates"
        visible: page.showList && page.groups.raye.length > 0
        iconName: "org.raye.settings-raye-update"
        iconFallback: "system-software-update"
        title: i18nc("@title %1 number of updates", "Raye updates (%1)", page.groups.raye.length)
        description: page.groupDescription(page.groups.raye)

        Repeater {
            model: page.groups.raye.slice(0, 60)
            delegate: UpdateRow {}
        }
        ExpanderRow {
            visible: page.groups.raye.length > 60
            title: i18nc("@info", "And %1 more", page.groups.raye.length - 60)
        }
    }

    Expander {
        objectName: "otherUpdates"
        visible: page.showList && page.otherRows.length + page.firmwareCount > 0
        expanded: page.otherRows.length + page.firmwareCount <= 8
        iconName: "org.raye.settings-raye-update"
        iconFallback: "system-software-update"
        title: {
            if (rayeGroup.visible)
                return i18nc("@title", "Other updates")
            const n = page.rows.length + page.firmwareCount
            // the size, while it waits to download (the status above says
            // why); found and not waiting, the status above says it
            if (page.state_ === "waiting" && page.status.totalBytes > 0)
                return i18ncp("@title %2 a size", "One update · %2", "%1 updates · %2", n, page.sizeText(page.status.totalBytes))
            return i18ncp("@title", "One update", "%1 updates", n)
        }
        description: rayeGroup.visible ? page.groupDescription(page.otherRows) : page.groupDescription(page.rows)

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
            model: page.otherRows.slice(0, 60)
            delegate: UpdateRow {}
        }
        ExpanderRow {
            visible: page.otherRows.length > 60
            title: i18nc("@info", "And %1 more", page.otherRows.length - 60)
        }
    }

    // under a group: what it does now, and how far an install in use got
    function groupDescription(list) {
        const p = U.groupProgress(list)
        if (page.state_ === "installing" && p.total > 0)
            return i18ncp("@info %1 and %2 numbers of updates", "%1 of %2 done", "%1 of %2 done", p.done, p.total)
        if (list === page.groups.raye && U.rayeRelease(list) !== "")
            return i18nc("@info %1 a Raye version, %2 what the updates do now", "Raye %1 · %2", U.rayeRelease(list), page.listState)
        return page.listState
    }

    // one package: its versions, and where it is in an install in use
    component UpdateRow: ExpanderRow {
        id: updateRow
        required property var modelData
        readonly property string phase: U.rowState(modelData)
        title: modelData.name
        Accessible.description: description + (rowStatus.visible ? ", " + rowStatus.text
                                                : tag.visible ? ", " + tag.text : "")
        description: modelData.from !== ""
                     ? i18nc("@info %1 kind of update, %2 and %3 versions", "%1 · %2 → %3", page.kindText(modelData.kind), modelData.from, modelData.to)
                     : i18nc("@info %1 kind of update, %2 version", "%1 · new, %2", page.kindText(modelData.kind), modelData.to)
        RestartTag { id: tag; visible: updateRow.phase === "" && U.needsRestart(modelData) }
        Item {
            implicitWidth: 16
            implicitHeight: 16
            anchors.verticalCenter: parent.verticalCenter
            visible: updateRow.phase === "installing" || updateRow.phase === "installed"
            Ring { anchors.fill: parent; visible: updateRow.phase === "installing" }
            StatusGlyph { anchors.fill: parent; size: 16; status: "success"; visible: updateRow.phase === "installed" }
        }
        Text {
            id: rowStatus
            anchors.verticalCenter: parent.verticalCenter
            visible: updateRow.phase !== ""
            font: Theme.caption
            color: updateRow.phase === "installed" ? Theme.text : Theme.textSecondary
            text: {
                switch (updateRow.phase) {
                case "queued": return i18nc("@info state of one update", "Waiting")
                case "installing": return i18nc("@info state of one update", "Installing")
                case "installed": return i18nc("@info state of one update", "Installed")
                case "failed": return i18nc("@info state of one update", "Will install at restart")
                }
                return ""
            }
            Accessible.ignored: true
        }
    }

    // the small turning ring of the row being installed
    component Ring: Item {
        Shape {
            id: ringShape
            anchors.fill: parent
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Theme.accent
                strokeWidth: 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                PathAngleArc { centerX: 8; centerY: 8; radiusX: 6.5; radiusY: 6.5; startAngle: -90; sweepAngle: 270 }
            }
            RotationAnimator on rotation {
                running: ringShape.visible && !Theme.animationsOff
                from: 0; to: 360
                duration: 1000
                loops: Animation.Infinite
            }
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
        searchId: "update-optional"
        iconName: "org.raye.settings-drivers"
        iconFallback: "preferences-system-hardware"
        title: i18nc("@title settings page", "Optional updates")
        description: i18nc("@info", "Drivers for your hardware, Raye kernel")
        target: "update/drivers"
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
        function onChanged() { page.noteRows() }
        function onRestartFailed(reason) {
            restartError.text = reason === "cancelled" ? i18n("The updates weren't set up because the password wasn't entered. Try again.")
                              : reason === "failed" ? i18n("The updates couldn't be set up to install. Your PC wasn't restarted. Try again later.")
                              : i18n("Your PC couldn't be restarted. Save your work and restart it from Start.")
            restartError.open()
        }
    }
}
