// SPDX-License-Identifier: GPL-3.0-or-later
// Recovery › Repair Raye: a question first, then the progress as the
// repair helper reports it. The progress window can be hidden; the
// repair goes on and the card shows how far it is.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

Item {
    id: root

    required property Recovery recovery

    // only its dialogs show, over the window; the item takes no room
    visible: false
    readonly property string state_: recovery ? recovery.repairState : "idle"
    readonly property bool running: ["update", "check", "download", "install"].indexOf(state_) >= 0

    function open() {
        if (running)
            progress.open()
        else
            ask.open()
    }
    function showProgress() { progress.open() }

    ContentDialog {
        id: ask
        title: i18nc("@title:window", "Repair Raye?")
        text: i18nc("@info", "Raye checks its system files and puts back a fresh copy of any that are damaged. Your files, apps and settings stay. It can take a while, and it needs an internet connection.")
        primaryText: i18nc("@action:button", "Repair now")
        onAccepted: {
            root.recovery.startRepair()
            progress.open()
        }
    }

    ContentDialog {
        id: progress
        objectName: "repairProgress"
        readonly property string error: root.recovery ? root.recovery.repairError : ""
        readonly property int found: root.recovery ? root.recovery.repairFound : -1

        title: root.state_ === "update" ? i18nc("@title:window", "Getting ready…")
             : root.state_ === "check" ? i18nc("@title:window", "Checking system files…")
             : root.state_ === "download" ? i18nc("@title:window", "Downloading…")
             : root.state_ === "install" ? i18nc("@title:window", "Repairing Raye…")
             : root.state_ === "done" ? (found > 0 ? i18nc("@title:window", "Raye is repaired")
                                                  : i18nc("@title:window", "No problems found"))
             : root.state_ === "error" ? i18nc("@title:window", "Raye couldn't be repaired")
             : ""
        primaryText: root.state_ === "error" && error !== "denied" ? i18nc("@action:button", "Try again")
                   : root.state_ === "done" && found > 0 ? i18nc("@action:button", "Restart now")
                   : ""
        closeText: root.running ? i18nc("@action:button", "Hide")
                 : root.state_ === "done" && found > 0 ? i18nc("@action:button", "Later")
                 : i18nc("@action:button", "Close")
        // while it runs Esc hides it, which the Hide button says too
        closePolicy: T.Popup.CloseOnEscape
        onAccepted: {
            if (root.state_ === "error") {
                root.recovery.startRepair()
                Qt.callLater(progress.open)
            } else {
                root.recovery.restartAfterRepair()
            }
        }
        // a result once read is done with; a repair under way goes on
        onRejected: if (!root.running) root.recovery.resetRepair()

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 420
            font: Theme.body
            color: Theme.text
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.bodyLineHeight
            text: root.running ? (progress.found > 0
                                  ? i18ncp("@info", "1 part of the system needs a fresh copy. You can keep using your PC.",
                                           "%1 parts of the system need a fresh copy. You can keep using your PC.", progress.found)
                                  : i18nc("@info", "This can take a while. You can keep using your PC."))
                : root.state_ === "done" ? (progress.found > 0
                                            ? i18ncp("@info", "1 part of the system was put back as it came. Restart your PC to finish.",
                                                     "%1 parts of the system were put back as they came. Restart your PC to finish.", progress.found)
                                            : i18nc("@info", "Raye's system files are as they should be. Nothing was changed."))
                : progress.error === "network" ? i18nc("@info", "Raye couldn't download what it needs. Check your internet connection, then try again.")
                : progress.error === "locked" ? i18nc("@info", "An update is being installed. Try again when it's done.")
                : progress.error === "denied" ? i18nc("@info", "Repairing Raye needs root access.")
                : i18nc("@info", "Something went wrong, and nothing was removed. Try again later.")
        }

        ProgressBar {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s2
            visible: root.running
            // the first two steps cannot say how far they are
            indeterminate: root.state_ === "update" || root.state_ === "check"
            from: 0
            to: 100
            value: root.recovery ? root.recovery.repairProgress : 0
        }
        Text {
            Layout.fillWidth: true
            visible: root.running && !(root.state_ === "update" || root.state_ === "check")
            font: Theme.caption
            color: Theme.textSecondary
            text: i18nc("@info a percentage", "%1%", root.recovery ? root.recovery.repairProgress : 0)
        }
    }
}
