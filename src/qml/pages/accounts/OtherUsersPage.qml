// SPDX-License-Identifier: GPL-3.0-or-later
// Accounts > Other users: everyone else on this PC. Adding, changing and
// removing need the password of an administrator, which the system asks
// for itself when it comes to that.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accounts"), target: "accounts" },
             { text: i18nc("@title settings page", "Other users") }]

    AccountUsers { id: users }

    property var chosen: ({})

    InfoBar {
        Layout.fillWidth: true
        open: !users.available
        closable: false
        severity: "warning"
        title: i18nc("@title", "Accounts can't be changed right now")
        message: i18nc("@info", "The account service of this PC is not answering.")
    }

    InfoBar {
        Layout.fillWidth: true
        open: users.error !== ""
        severity: "error"
        title: i18nc("@title", "That didn't work")
        message: users.error
        onClosed: users.clearError()
    }

    ButtonCard {
        searchId: "add-user"
        iconName: "org.raye.settings-other-users"
        iconFallback: "system-users"
        title: i18nc("@title", "Add someone else to this PC")
        buttonText: i18nc("@action:button", "Add account")
        accent: true
        enabled: users.available && !users.busy
        onButtonClicked: addDialog.open()
    }

    SectionHeader {
        visible: users.others.length > 0
        text: i18nc("@title:group", "Other users")
    }

    EmptyState {
        Layout.fillWidth: true
        visible: users.available && users.others.length === 0
        iconName: "org.raye.settings-other-users"
        title: i18nc("@title", "You're the only one here")
        message: i18nc("@info", "Everyone gets their own files and settings when they have their own account.")
    }

    Repeater {
        model: users.others
        delegate: Expander {
            required property var modelData
            Layout.fillWidth: true
            iconName: "org.raye.settings-your-info"
            iconFallback: "user-identity"
            iconSource: modelData.picture
            title: modelData.displayName
            description: modelData.administrator ? i18nc("@label kind of account", "Sudo user")
                                                 : i18nc("@label kind of account", "Standard user")
            ExpanderRow {
                title: i18nc("@title", "Account type")
                ComboBox {
                    enabled: !users.busy
                    Accessible.name: i18nc("@title", "Account type")
                    model: [i18nc("@label kind of account", "Standard user"), i18nc("@label kind of account", "Sudo user")]
                    currentIndex: modelData.administrator ? 1 : 0
                    onActivated: (index) => users.setAdministrator(modelData.uid, index === 1)
                }
            }
            ExpanderRow {
                title: i18nc("@title", "Password")
                Button {
                    text: i18nc("@action:button", "Change")
                    enabled: !users.busy
                    onClicked: {
                        page.chosen = modelData
                        passwordDialog.open()
                    }
                }
            }
            ExpanderRow {
                title: i18nc("@title", "Account and files")
                Button {
                    text: i18nc("@action:button", "Remove")
                    enabled: !users.busy
                    onClicked: {
                        page.chosen = modelData
                        removeDialog.open()
                    }
                }
            }
        }
    }

    ContentDialog {
        id: removeDialog
        title: i18nc("@title:window %1 is a person", "Remove %1?", page.chosen.displayName || "")
        text: i18nc("@info", "Their account goes away. Do you want to keep their files in their home folder?")
        primaryText: i18nc("@action:button", "Keep files")
        secondaryText: i18nc("@action:button", "Delete files")
        onAccepted: users.removeUser(page.chosen.uid, false)
        onSecondaryClicked: users.removeUser(page.chosen.uid, true)
    }

    ContentDialog {
        id: passwordDialog
        title: i18nc("@title:window %1 is a person", "New password for %1", page.chosen.displayName || "")
        primaryText: i18nc("@action:button", "Change")
        primaryEnabled: users.passwordProblem(otherNew.text, otherConfirm.text) === ""
        onAccepted: users.setPasswordOf(page.chosen.uid, otherNew.text)
        onClosed: {
            otherNew.clear()
            otherConfirm.clear()
        }
        PasswordField {
            id: otherNew
            Layout.fillWidth: true
            placeholderText: i18nc("@label", "New password")
        }
        PasswordField {
            id: otherConfirm
            Layout.fillWidth: true
            placeholderText: i18nc("@label", "Confirm the new password")
        }
    }

    ContentDialog {
        id: addDialog
        title: i18nc("@title:window", "Add an account")
        primaryText: i18nc("@action:button", "Add account")
        primaryEnabled: realName.text.trim() !== "" && users.loginProblem(login.text) === ""
                        && users.passwordProblem(newPassword.text, confirmPassword.text) === ""
        onAccepted: users.addUser(realName.text, login.text, newPassword.text, administrator.checked)
        onClosed: {
            realName.text = ""
            login.text = ""
            newPassword.clear()
            confirmPassword.clear()
            administrator.checked = false
        }
        TextField {
            id: realName
            objectName: "realName"
            Layout.fillWidth: true
            placeholderText: i18nc("@label", "Their name")
            onTextEdited: login.text = users.suggestLogin(text)
        }
        TextField {
            id: login
            objectName: "login"
            Layout.fillWidth: true
            placeholderText: i18nc("@label", "User name")
        }
        Text {
            Layout.fillWidth: true
            visible: login.text !== "" && text !== ""
            wrapMode: Text.Wrap
            text: users.loginProblem(login.text)
            font: Theme.caption
            color: Theme.textSecondary
        }
        PasswordField {
            id: newPassword
            objectName: "newUserPassword"
            Layout.fillWidth: true
            placeholderText: i18nc("@label", "Password")
        }
        PasswordField {
            id: confirmPassword
            objectName: "newUserConfirm"
            Layout.fillWidth: true
            placeholderText: i18nc("@label", "Confirm the password")
        }
        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: i18nc("@label", "Sudo user: can make system changes with their own password")
                wrapMode: Text.Wrap
                font: Theme.body
                color: Theme.text
            }
            ToggleSwitch {
                id: administrator
                text: i18nc("@label kind of account", "Sudo user")
            }
        }
    }
}
