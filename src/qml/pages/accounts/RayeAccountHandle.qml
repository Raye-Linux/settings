// SPDX-License-Identifier: GPL-3.0-or-later
// The Raye account for the parts of Settings that must load without it
// (the header, Your info): they load this file and read `account`.
import QtQml
import org.raye.account

QtObject {
    readonly property var account: RayeAccount
    Component.onCompleted: RayeAccount.refresh()
}
