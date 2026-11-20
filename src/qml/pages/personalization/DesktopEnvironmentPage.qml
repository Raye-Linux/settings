// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Desktop environment: KDE Plasma (the Raye desktop) or
// Hyprland, the one this account uses. A card per desktop with a small
// picture of it; the one in use says so. Picking the other card shows,
// right on the page, what a switch does (the open apps close and open
// again there, the apps open now) and two buttons: switch now (no
// password, no sign-in screen: a short transition and the other desktop
// opens) or at the next sign-in. A missing desktop is installed first.
// The other desktop is never removed by itself: removing Hyprland is its
// own button, shown when both are there.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
             { text: i18nc("@title settings page", "Desktop environment") }]

    property DesktopEnvironment desktops: DesktopEnvironment {}
    // the card picked on this page; the next sign-in's desktop until then
    property string target: desktops.next
    // "Later" closes the sign-out bar until the choice changes again
    property bool signOutLater: false
    // the last thing that did not work: {action, desktop, message}
    property var failure: null
    // the desktop this PC could not switch to right away
    property string notNow: ""
    // the transition gave way: the apps are being asked to close
    property bool waitingForApps: false

    // the other desktop picked, or this one again while the next sign-in
    // would open the other
    readonly property bool offerSwitch: target !== "" && (target !== desktops.current || desktops.next !== desktops.current)
    readonly property bool keeping: target === desktops.current

    function nameOf(desktop) {
        return desktop === "hyprland" ? i18nc("@title desktop environment", "Hyprland")
                                      : i18nc("@title desktop environment", "KDE Plasma")
    }
    function installed(desktop) {
        return desktop === "hyprland" ? desktops.hyprlandInstalled : desktop === "plasma" && desktops.plasmaInstalled
    }

    // what a card says under its name
    function stateOf(desktop) {
        if (desktops.busyDesktop === desktop && desktops.busy === "installing")
            return i18nc("@info desktop environment", "Installing…")
        if (desktops.busyDesktop === desktop && desktops.busy === "removing")
            return i18nc("@info desktop environment", "Removing…")
        if (!page.installed(desktop))
            return i18nc("@info desktop environment", "Not installed")
        if (desktops.current === desktop && desktops.next === desktop)
            return i18nc("@info desktop environment", "In use")
        if (desktops.current === desktop)
            return i18nc("@info desktop environment", "In use until you sign out")
        if (desktops.next === desktop)
            return i18nc("@info desktop environment", "Opens the next time you sign in")
        return i18nc("@info desktop environment", "Installed")
    }

    function switchNow() {
        page.failure = null
        page.notNow = ""
        if (page.installed(page.target))
            desktops.switchNow(page.target)
        else
            desktops.install(page.target, desktops.canSwitchNow)
    }
    function switchLater() {
        page.failure = null
        page.signOutLater = false
        if (page.installed(page.target))
            desktops.choose(page.target)
        else
            desktops.install(page.target, false)
    }

    Connections {
        target: page.desktops
        function onFinished(action, desktop, ok, dismissed, message) {
            if (action === "switch-now" && !ok) {
                transition.dismiss()
                page.waitingForApps = false
            }
            if (ok) {
                page.signOutLater = false
                page.failure = null
            } else if (!dismissed) {
                page.failure = { action: action, desktop: desktop, message: message }
            }
        }
        function onReadyToLeave(desktop) {
            transition.from = page.desktops.current
            transition.to = desktop
            transition.title = i18nc("@info:status", "Switching to %1…", page.nameOf(desktop))
            transition.play()
        }
        function onSwitchNowUnavailable(desktop) {
            page.notNow = desktop
        }
    }

    DesktopSwitchTransition {
        id: transition
        objectName: "switchTransition"
        onFinished: {
            page.desktops.leave()
            giveWay.restart()
        }
    }
    // still here a few seconds after leaving: an app asks to save
    Timer {
        id: giveWay
        interval: 3000
        onTriggered: {
            if (page.desktops.busy === "leaving") {
                transition.dismiss()
                page.waitingForApps = true
            }
        }
    }
    // the open apps, kept current while the page is shown
    Timer {
        interval: 4000
        repeat: true
        triggeredOnStart: true
        running: page.visible && page.offerSwitch && !page.keeping && page.desktops.busy === ""
        onTriggered: page.desktops.refreshOpenApps()
    }

    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s4
        text: i18nc("@info", "Choose the desktop you see after you sign in. You can also pick it on the sign-in screen, with the Desktop button at the bottom right.")
        font: Theme.body
        color: Theme.text
        wrapMode: Text.WordWrap
    }

    InfoBar {
        objectName: "signOutBar"
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.desktops.signOutNeeded && !page.signOutLater && page.desktops.busy === ""
        severity: "informational"
        closable: false
        title: i18nc("@title", "Sign out to switch")
        message: page.notNow !== ""
                 ? i18nc("@info", "This PC can't switch right away (another account signs in by itself). Your next sign-in opens %1.", page.nameOf(page.desktops.next))
                 : i18nc("@info", "Your next sign-in opens %1.", page.nameOf(page.desktops.next))

        Button {
            objectName: "signOutNow"
            accent: true
            text: i18nc("@action:button end the session", "Sign out now")
            onClicked: page.desktops.signOut()
        }
        Button {
            objectName: "signOutLater"
            text: i18nc("@action:button", "Later")
            onClicked: page.signOutLater = true
        }
    }

    InfoBar {
        objectName: "failureBar"
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.failure !== null
        severity: "critical"
        onClosed: page.failure = null
        title: !page.failure ? ""
             : page.failure.action === "install" ? i18nc("@title", "%1 couldn't be installed", page.nameOf(page.failure.desktop))
             : page.failure.action === "remove" ? i18nc("@title", "%1 couldn't be removed", page.nameOf(page.failure.desktop))
             : page.failure.message === "still-open" ? i18nc("@title", "An app is still open")
             : i18nc("@title", "The desktop wasn't changed")
        message: !page.failure ? ""
               : page.failure.action === "install" ? i18nc("@info", "Check that you're connected to the internet, then try again.")
               : page.failure.message === "still-open" ? i18nc("@info", "Save your work and close it, then switch again. Nothing was changed.")
               : i18nc("@info", "Try again. If it keeps failing, restart the PC.")
    }

    RowLayout {
        id: cards
        objectName: "desktopCards"
        Layout.fillWidth: true
        spacing: Theme.s4

        Repeater {
            model: ["plasma", "hyprland"]
            delegate: RadioCard {
                id: card
                required property string modelData
                // in use, opens next, not installed... (a badge on the picture)
                readonly property string stateText: page.stateOf(modelData)
                objectName: modelData + "Card"
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                Layout.alignment: Qt.AlignTop
                searchId: modelData
                title: modelData === "hyprland" ? page.nameOf("hyprland")
                                                : i18nc("@title desktop environment", "KDE Plasma (the Raye desktop)")
                description: modelData === "hyprland"
                             ? i18nc("@info", "A tiling desktop: windows line up side by side on their own, and you do most things with the keyboard. For people who already know Hyprland.")
                             : i18nc("@info", "The full Raye desktop, made to work like Windows: taskbar, Start menu, windows you drag and snap.")
                checked: page.target === modelData
                enabled: page.desktops.busy === ""
                Accessible.description: description + " " + stateText
                onClicked: {
                    page.target = modelData
                    checked = Qt.binding(() => page.target === card.modelData)
                }
                ColumnLayout {
                    width: parent ? parent.width : 220
                    spacing: 0
                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.round(width * 10 / 16)
                        implicitWidth: 220
                        DesktopEnvironmentPreview {
                            anchors.fill: parent
                            desktop: card.modelData
                        }
                        Rectangle {
                            objectName: "stateBadge"
                            readonly property bool strong: page.desktops.current === card.modelData || page.desktops.next === card.modelData
                            x: Theme.s3
                            y: parent.height - height - Theme.s3 - (card.modelData === "hyprland" ? 0 : parent.height * 0.13)
                            width: badgeText.implicitWidth + 2 * Theme.s3
                            height: badgeText.implicitHeight + Theme.s2
                            radius: height / 2
                            color: strong ? Theme.accent : Theme.flyout
                            border.width: strong ? 0 : 1
                            border.color: Theme.cardBorder
                            Text {
                                id: badgeText
                                anchors.centerIn: parent
                                text: card.stateText
                                font: Theme.caption
                                color: parent.strong ? Theme.textOnAccent : Theme.text
                            }
                            Behavior on color { ColorAnimation { duration: Theme.durationNormal } }
                        }
                    }
                }
            }
        }
    }

    // what switching does, and the two ways to do it, on the page itself
    Rectangle {
        id: switchPanel
        objectName: "switchPanel"
        Layout.fillWidth: true
        Layout.topMargin: Theme.s4
        visible: page.offerSwitch
        implicitHeight: panelColumn.implicitHeight + 2 * Theme.cardPadding
        radius: Theme.radiusOverlay
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder
        opacity: visible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }

        ColumnLayout {
            id: panelColumn
            anchors.fill: parent
            anchors.margins: Theme.cardPadding
            spacing: Theme.s3

            Text {
                objectName: "saveNote"
                Layout.fillWidth: true
                text: page.keeping
                      ? i18nc("@info", "You're using %1 now. Your next sign-in opens %2.", page.nameOf(page.target), page.nameOf(page.desktops.next))
                      : page.desktops.canSwitchNow
                      ? i18nc("@info", "Open apps close and open again on %1. Save your work first.", page.nameOf(page.target))
                      : i18nc("@info", "%1 opens the next time you sign in.", page.nameOf(page.target))
                font: Theme.bodyStrong
                color: Theme.text
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "openAppsLine"
                Layout.fillWidth: true
                visible: page.desktops.canSwitchNow && !page.keeping
                text: page.desktops.openApps.length === 0
                      ? i18nc("@info", "No other apps are open.")
                      : i18ncp("@info %2 is a list of app names", "1 app is open: %2", "%1 apps are open: %2",
                               page.desktops.openApps.length, page.desktops.openApps.join(", "))
                font: Theme.body
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "downloadLine"
                Layout.fillWidth: true
                visible: !page.installed(page.target)
                text: i18nc("@info", "%1 isn't installed yet. Raye downloads it first, so stay connected to the internet.", page.nameOf(page.target))
                font: Theme.body
                color: Theme.textSecondary
                wrapMode: Text.WordWrap
            }
            RowLayout {
                spacing: Theme.s3
                Button {
                    objectName: "switchNowButton"
                    visible: page.desktops.canSwitchNow && !page.keeping
                    accent: true
                    enabled: page.desktops.busy === ""
                    text: page.installed(page.target)
                          ? i18nc("@action:button", "Switch to %1 now", page.nameOf(page.target))
                          : i18nc("@action:button", "Install and switch now")
                    onClicked: page.switchNow()
                }
                Button {
                    objectName: "switchLaterButton"
                    visible: page.desktops.next !== page.target
                    accent: !page.desktops.canSwitchNow || page.keeping
                    enabled: page.desktops.busy === ""
                    text: page.keeping ? i18nc("@action:button", "Keep %1", page.nameOf(page.target))
                          : page.installed(page.target)
                          ? i18nc("@action:button", "At my next sign-in")
                          : i18nc("@action:button", "Install for my next sign-in")
                    onClicked: page.switchLater()
                }
            }
        }
    }

    RowLayout {
        objectName: "busyRow"
        Layout.fillWidth: true
        Layout.topMargin: Theme.s3
        visible: page.desktops.busy !== "" && (page.desktops.busy !== "leaving" || page.waitingForApps)
        spacing: Theme.s4
        ProgressBar {
            Layout.preferredWidth: 160
            indeterminate: page.desktops.busyPercent <= 0
            value: Math.max(0, page.desktops.busyPercent) / 100
        }
        Text {
            Layout.fillWidth: true
            text: page.desktops.busy === "installing" ? i18nc("@info", "Installing %1. This can take a few minutes.", page.nameOf(page.desktops.busyDesktop))
                : page.desktops.busy === "removing" ? i18nc("@info", "Removing %1…", page.nameOf(page.desktops.busyDesktop))
                : page.desktops.busy === "leaving" ? i18nc("@info", "Waiting for your apps to close. If one asks, save your work.")
                : page.desktops.busy === "preparing" ? i18nc("@info", "Getting %1 ready…", page.nameOf(page.desktops.busyDesktop))
                : i18nc("@info", "Setting up your next sign-in…")
            font: Theme.body
            color: Theme.textSecondary
            wrapMode: Text.WordWrap
        }
    }

    SectionHeader {
        visible: removeCard.visible
        text: i18nc("@title:group", "Related")
    }

    ButtonCard {
        id: removeCard
        objectName: "removeHyprland"
        Layout.fillWidth: true
        visible: page.desktops.plasmaInstalled && page.desktops.hyprlandInstalled
        enabled: page.desktops.busy === "" && page.desktops.canRemoveHyprland
        iconName: "edit-delete"
        title: i18nc("@title", "Remove Hyprland")
        description: page.desktops.canRemoveHyprland
                     ? i18nc("@info", "Frees a little space. KDE Plasma isn't touched, and you can install Hyprland again here.")
                     : i18nc("@info", "Switch to KDE Plasma first: Hyprland is in use or opens at your next sign-in.")
        searchId: "remove-hyprland"
        buttonText: i18nc("@action:button", "Remove")
        onButtonClicked: removeDialog.open()
    }

    ContentDialog {
        id: removeDialog
        objectName: "removeDialog"
        title: i18nc("@title:window", "Remove Hyprland?")
        text: i18nc("@info", "Hyprland and what it brought are removed from this PC. KDE Plasma stays as it is.")
        primaryText: i18nc("@action:button", "Remove")
        onAccepted: page.desktops.removeHyprland()
    }
}
