// SPDX-License-Identifier: GPL-3.0-or-later
// What went wrong with the Raye account, in words, from the service's
// error code. A cancelled sign-in says nothing.
import QtQuick
import org.raye.settings
import org.raye.account

InfoBar {
    id: bar

    // the code to show; the pages pass RayeAccount.error or a failed action's code
    property string code: ""

    readonly property var _texts: ({
        "offline": [i18nc("@info", "Can't reach your Raye account"),
                    i18nc("@info", "Check your internet connection. Your settings sync when you're back online.")],
        "signed_out": [i18nc("@info", "Sign in again"),
                       i18nc("@info", "This PC was signed out of your Raye account.")],
        "timeout": [i18nc("@info", "Sign-in took too long"),
                    i18nc("@info", "Start again when you're ready.")],
        "keyring": [i18nc("@info", "Couldn't keep you signed in"),
                    i18nc("@info", "The wallet that stores passwords on this PC didn't answer.")],
        "no_browser": [i18nc("@info", "No web browser found"),
                       i18nc("@info", "Install a web browser to sign in.")],
        "no_service": [i18nc("@info", "Raye account isn't installed"),
                       i18nc("@info", "Install updates, then try again.")],
        "profile": [i18nc("@info", "Couldn't change your picture or name on this PC"),
                    i18nc("@info", "Try again from Your info.")],
        "too_large": [i18nc("@info", "This picture is too big"),
                      i18nc("@info", "Choose a picture under 2 MB.")],
        "busy": [i18nc("@info", "Already signing in"),
                 i18nc("@info", "Finish in your browser, or cancel and start again.")]
    })
    readonly property var _text: _texts[code] || [i18nc("@info", "Something went wrong"),
                                                    i18nc("@info", "Try again in a few minutes.")]

    open: code !== "" && code !== "cancelled"
    severity: code === "offline" ? "warning" : "error"
    title: _text[0]
    message: _text[1]
    onClosed: code = ""
}
