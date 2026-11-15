// SPDX-License-Identifier: GPL-3.0-or-later
// Who to show at the top of Settings: the Raye account when this PC is
// signed in to one (its name, email and picture, like a Microsoft account
// on Windows), else the account of this PC. The Raye account module ships
// on its own, so it is read through a Loader and its absence is fine.
pragma Singleton
import QtQuick
import org.raye.settings

Item {
    id: identity

    readonly property var raye: loader.status === Loader.Ready && loader.item ? loader.item.account : null
    readonly property bool signedIn: raye !== null && raye.signedIn
    readonly property bool usesAccountPicture: signedIn && raye.picture.toString() !== ""
        && (raye.profileMode === "both" || raye.profileMode === "picture")
    readonly property string name: signedIn && raye.displayName !== "" ? raye.displayName : CurrentUser.displayName
    readonly property string email: signedIn ? raye.email : ""
    readonly property url picture: usesAccountPicture ? raye.picture : CurrentUser.picture
    readonly property url manageUrl: signedIn ? raye.manageUrl : ""

    Loader {
        id: loader
        active: Router.isAvailable("accounts/raye-account")
        source: Qt.resolvedUrl("../pages/accounts/RayeAccountHandle.qml")
    }
}
