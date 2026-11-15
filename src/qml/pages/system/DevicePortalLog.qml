// SPDX-License-Identifier: GPL-3.0-or-later
// One line of Device Portal's connection log, in words.
pragma Singleton
import QtQuick

QtObject {
    function what(event) {
        switch (event) {
        case "sign-in": return i18nc("@info portal log", "Signed in")
        case "paired-sign-in": return i18nc("@info portal log", "Signed in with a paired browser")
        case "sign-in-failed": return i18nc("@info portal log", "Wrong user name or password")
        case "paired": return i18nc("@info portal log", "Browser paired")
        case "pair-failed": return i18nc("@info portal log", "Wrong pairing code")
        case "locked": return i18nc("@info portal log", "Refused after too many tries")
        case "refused-network": return i18nc("@info portal log", "Refused, not on this network")
        case "refused-busy": return i18nc("@info portal log", "Refused, too many connections at once")
        case "refused-path": return i18nc("@info portal log", "Refused a folder outside the home folder")
        case "sign-out": return i18nc("@info portal log", "Signed out")
        case "download": return i18nc("@info portal log", "File downloaded")
        case "upload": return i18nc("@info portal log", "File sent to this PC")
        case "end-task":
        case "end-task-ok": return i18nc("@info portal log", "Ended a task")
        case "screenshot": return i18nc("@info portal log", "Took a screenshot")
        case "reboot-ok": return i18nc("@info portal log", "Restarted the PC")
        case "poweroff-ok": return i18nc("@info portal log", "Shut down the PC")
        case "turned-on": return i18nc("@info portal log", "Device Portal turned on")
        case "turned-off": return i18nc("@info portal log", "Device Portal turned off")
        case "sign-in-changed": return i18nc("@info portal log", "Sign-in changed")
        case "pair-code": return i18nc("@info portal log", "Pairing code made")
        case "devices-forgotten": return i18nc("@info portal log", "Paired browsers forgotten")
        }
        if (event.endsWith("-denied"))
            return i18nc("@info portal log", "Wrong sudo password")
        if (event.endsWith("-unavailable"))
            return i18nc("@info portal log", "Couldn't ask for the sudo password")
        if (event.endsWith("-locked"))
            return i18nc("@info portal log", "Refused after too many tries")
        if (event.endsWith("-failed"))
            return i18nc("@info portal log", "That action didn't work")
        return event
    }

    function line(entry) {
        const when = new Date(entry.time * 1000).toLocaleString(Qt.locale(), Locale.ShortFormat)
        const parts = [when, what(entry.event)]
        if (entry.address)
            parts.push(entry.address)
        if (entry.detail && entry.event !== "sign-in-failed")
            parts.push(entry.detail)
        // rows show their text as rich text when it looks like markup
        return parts.join(" · ").replace(/</g, "‹").replace(/>/g, "›")
    }
}
