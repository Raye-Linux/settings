// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts > Sign-in options: your password, your fingerprint, signing in
// without either, and when Raye asks again.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accounts"), target: "accounts" },
             { text: i18nc("@title settings page", "Sign-in options") }]

    SignInSettings {
        id: signIn
        onPasswordChanged: page.notice = i18nc("@info", "Your password has been changed.")
        onPasswordBusyChanged: if (!passwordBusy && passwordError !== "") passwordDialog.open()
    }
    FingerprintSettings {
        id: fingerprint
        onEnrolled: if (!signInEnabled) setSignInEnabled(true)
    }

    AccountUsers { id: users }

    property string notice: ""

    function fingerList() {
        return fingerprint.fingers.map(f => fingerprint.fingerName(f)).join(", ")
    }

    InfoBar {
        Layout.fillWidth: true
        open: page.notice !== ""
        severity: "success"
        title: page.notice
        onClosed: page.notice = ""
    }

    InfoBar {
        Layout.fillWidth: true
        open: signIn.autoLoginFailed
        severity: "error"
        closable: false
        title: i18nc("@title", "That didn't work")
        message: i18nc("@info", "Automatic sign-in could not be changed.")
    }

    SectionHeader { text: i18nc("@title:group", "Ways to sign in") }

    ButtonCard {
        searchId: "password"
        iconName: "org.raye.settings-sign-in"
        iconFallback: "preferences-desktop-user-password"
        title: i18nc("@title", "Password")
        description: i18nc("@info", "Change the password you sign in with")
        buttonText: i18nc("@action:button", "Change")
        enabled: !signIn.passwordBusy
        onButtonClicked: passwordDialog.open()
    }

    Card {
        searchId: "fingerprint"
        iconName: "fingerprint"
        iconFallback: "preferences-desktop-user-password"
        title: i18nc("@title", "Fingerprint")
        description: !fingerprint.available
                     ? i18nc("@info", "This PC has no fingerprint reader.")
                     : fingerprint.fingers.length === 0
                       ? i18nc("@info", "Sign in with a finger instead of your password")
                       : page.fingerList()
        Row {
            visible: fingerprint.available
            spacing: Theme.s2
            Button {
                text: i18nc("@action:button", "Add a finger")
                onClicked: enrollDialog.open()
            }
            Button {
                visible: fingerprint.fingers.length > 0
                text: i18nc("@action:button", "Remove all")
                onClicked: fingerprint.removeAll()
            }
        }
    }

    ToggleCard {
        visible: fingerprint.available && fingerprint.fingers.length > 0
        iconName: "fingerprint"
        iconFallback: "preferences-desktop-user-password"
        title: i18nc("@title", "Accept my finger when I sign in")
        checked: fingerprint.signInEnabled
        onToggled: fingerprint.setSignInEnabled(checked)
    }

    SectionHeader { text: i18nc("@title:group", "Additional settings") }

    ToggleCard {
        searchId: "automatic-sign-in"
        iconName: "org.raye.settings-sign-in"
        iconFallback: "preferences-desktop-user-password"
        title: i18nc("@title", "Sign in automatically")
        description: signIn.autoLoginUser !== "" && !signIn.autoLogin
                     ? i18nc("@info %1 is a user name", "This PC signs in as %1 by itself. Turning this on makes it you.", signIn.autoLoginUser)
                     : i18nc("@info", "Anyone who turns this PC on gets to your files without a password")
        enabled: !signIn.autoLoginBusy
        checked: signIn.autoLogin
        onToggled: {
            signIn.setAutoLogin(checked)
            checked = Qt.binding(() => signIn.autoLogin)
        }
    }

    ToggleCard {
        searchId: "restart-apps"
        iconName: "org.raye.settings-sign-in"
        iconFallback: "system-reboot"
        title: i18nc("@title", "Automatically save my restartable apps and restart them when I sign back in")
        checked: signIn.restartApps
        onToggled: signIn.restartApps = checked
    }

    ComboCard {
        searchId: "when-away"
        iconName: "system-lock-screen"
        iconFallback: "system-lock-screen"
        title: i18nc("@title", "If you've been away, when should Raye ask you to sign in again?")
        model: [i18nc("@item:inlistbox", "When the screen turns off"), i18nc("@item:inlistbox", "Never")]
        currentIndex: signIn.lockWhenAway ? 0 : 1
        onActivated: (index) => signIn.lockWhenAway = index === 0
    }

    ContentDialog {
        id: passwordDialog
        title: i18nc("@title:window", "Change your password")
        primaryText: i18nc("@action:button", "Change")
        primaryEnabled: currentField.text !== "" && users.passwordProblem(newField.text, confirmField.text) === ""
        onAccepted: signIn.changePassword(currentField.text, newField.text)
        onClosed: {
            currentField.clear()
            newField.clear()
            confirmField.clear()
        }
        Text {
            Layout.fillWidth: true
            visible: signIn.passwordError !== ""
            wrapMode: Text.Wrap
            text: signIn.passwordError
            font: Theme.bodyStrong
            color: Theme.critical
        }
        PasswordField {
            id: currentField
            objectName: "currentPassword"
            Layout.fillWidth: true
            placeholderText: i18nc("@label", "Current password")
        }
        PasswordField {
            id: newField
            objectName: "newPassword"
            Layout.fillWidth: true
            placeholderText: i18nc("@label", "New password")
        }
        PasswordField {
            id: confirmField
            objectName: "confirmPassword"
            Layout.fillWidth: true
            placeholderText: i18nc("@label", "Confirm the new password")
        }
    }

    ContentDialog {
        id: enrollDialog
        title: i18nc("@title:window", "Add a finger")
        primaryText: fingerprint.enrolling ? "" : i18nc("@action:button", "Start")
        closeText: fingerprint.enrolling ? i18nc("@action:button", "Cancel") : i18nc("@action:button", "Close")
        onAccepted: {
            fingerprint.startEnroll(fingerChoice.currentValue)
            enrollDialog.open()
        }
        onRejected: fingerprint.cancelEnroll()
        ComboBox {
            id: fingerChoice
            Layout.fillWidth: true
            visible: !fingerprint.enrolling
            textRole: "text"
            valueRole: "value"
            Accessible.name: i18nc("@title:window", "Add a finger")
            model: ["right-index-finger", "left-index-finger", "right-thumb", "left-thumb", "right-middle-finger",
                    "left-middle-finger", "right-ring-finger", "left-ring-finger", "right-little-finger", "left-little-finger"]
                   .map(f => ({ text: fingerprint.fingerName(f), value: f }))
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            text: fingerprint.message
            font: Theme.body
            color: Theme.text
        }
        ProgressBar {
            Layout.fillWidth: true
            visible: fingerprint.enrolling && fingerprint.stagesNeeded > 0
            value: fingerprint.stagesNeeded > 0 ? fingerprint.stagesDone / fingerprint.stagesNeeded : 0
        }
    }
}
