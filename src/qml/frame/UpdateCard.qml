// SPDX-License-Identifier: GPL-3.0-or-later
// "Raye Update" on the launch screen: what updating is doing now, when
// it last looked, and a "Restart now" button once a restart is all that
// is left. The whole card opens Raye Update.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.raye.settings
import "../pages/update/updatetext.js" as U

HeroCard {
    id: card

    // UpdateStatus; the page and this card decide alike (updatetext.js)
    property var status: typeof updates !== "undefined" ? updates : null
    readonly property string state_: U.pageState(status, Date.now())
    readonly property bool busy: state_ === "checking" || state_ === "downloading" || state_ === "installing"
    readonly property bool restart: state_ === "restart" || state_ === "armed"
    readonly property int count: U.waitingCount(status)

    UpdateNoticeText { id: notice; notice: U.sourceNotice(card.status) }

    signal restartRequested()

    searchId: "update"
    title: restart ? i18nc("@title", "Restart to finish updating")
         : state_ === "checking" ? i18nc("@title", "Checking for updates…")
         : state_ === "downloading" ? i18nc("@title", "Downloading updates…")
         : state_ === "installing" ? i18nc("@title", "Installing updates…")
         : state_ === "install" ? i18ncp("@title", "One update ready to install", "%1 updates ready to install",
                                          status.pending.filter(p => !U.needsRestart(p)).length)
         : state_ === "paused" ? i18nc("@title", "Updates are paused")
         : state_ === "error" ? i18nc("@title", "Updates need attention")
         : count > 0 ? i18ncp("@title", "One update ready", "%1 updates ready", count)
         : state_ === "uptodate" ? i18nc("@title", "You're up to date")
         : state_ === "incomplete" ? i18nc("@title", "We couldn't check everything")
         : i18nc("@title", "Raye Update")
    // when part of it could not be read, that matters more than when it looked
    message: notice.text !== "" ? notice.text
           : state_ === "install" && U.installWait(status) === "battery" ? i18nc("@info", "Plug in your PC to install these updates.")
           : lastChecked()
    onClicked: Router.navigate("update")

    function lastChecked() {
        if (!status || status.checked <= 0)
            return i18n("Not checked for updates yet")
        const when = new Date(status.checked * 1000)
        const now = new Date()
        const time = when.toLocaleTimeString(Qt.locale(), Locale.ShortFormat)
        if (when.toDateString() === now.toDateString())
            return i18nc("@info %1 is a time", "Last checked: today, %1", time)
        const yesterday = new Date(now.getTime() - 86400000)
        if (when.toDateString() === yesterday.toDateString())
            return i18nc("@info %1 is a time", "Last checked: yesterday, %1", time)
        return i18nc("@info %1 is a date", "Last checked: %1", when.toLocaleDateString(Qt.locale(), Locale.ShortFormat))
    }

    // the spinner turns, the status never does: a single icon kept the
    // angle the animation stopped at and showed a tilted check mark
    glyph: Component {
        Item {
            implicitWidth: status.implicitWidth
            implicitHeight: status.implicitHeight

            Kirigami.Icon {
                id: status
                anchors.fill: parent
                visible: !card.busy || card.restart
                source: card.restart ? "org.raye.settings-status-restart"
                      : card.state_ === "error" || card.state_ === "incomplete" ? "org.raye.settings-status-warning"
                      : card.state_ === "paused" ? "org.raye.settings-status-info"
                      : card.state_ === "never" ? "org.raye.settings-raye-update"
                      : "org.raye.settings-status-ok"
                fallback: "update-none"
            }

            Kirigami.Icon {
                anchors.fill: parent
                visible: card.busy && !card.restart
                source: "org.raye.settings-status-updating"
                fallback: "update-none"
                RotationAnimator on rotation {
                    running: card.busy && !card.restart && !Theme.animationsOff
                    from: 0; to: 360
                    duration: 1600
                    loops: Animation.Infinite
                }
            }
        }
    }

    // no restart for these: they install while you work
    Button {
        visible: card.state_ === "install"
        accent: true
        text: i18nc("@action:button", "Install now")
        onClicked: card.status.installNow()
    }

    Button {
        visible: card.restart
        accent: true
        enabled: !card.status || !card.status.restarting
        text: i18nc("@action:button", "Restart now")
        onClicked: card.restartRequested()
    }
}
