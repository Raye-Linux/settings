// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Lock screen: the picture on the lock screen and whether
// the sign-in screen shows it too. When the screen locks is decided in one
// place, Power, and this page leads there.
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
             { text: i18nc("@title settings page", "Lock screen") }]

    LockScreenSettings { id: lock }
    // the pictures of the system, and the Pictures folder
    BackgroundSettings { id: background }

    readonly property bool rayePicture: lock.image.toString() === ""
    readonly property url rayeWallpaper: background.wallpapers.length > 0 ? background.wallpapers[0].preview : ""

    // recent pictures first, then the pictures of the system
    readonly property var tiles: {
        const list = []
        const seen = {}
        for (const url of lock.recentImages) {
            const key = url.toString()
            if (!seen[key]) {
                seen[key] = true
                list.push({ url: key, preview: url, name: "" })
            }
        }
        // a wallpaper of the system is a folder of sizes: the lock screen
        // takes a picture, the one the preview shows
        for (const w of background.wallpapers) {
            const key = w.preview.toString()
            if (!seen[key] && list.length < 9) {
                seen[key] = true
                list.push({ url: key, preview: w.preview, name: w.name })
            }
        }
        return list
    }

    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: Theme.s3
        implicitWidth: 320
        implicitHeight: 200
        radius: Theme.radiusOverlay
        color: Theme.backdropSolid
        clip: true
        Accessible.role: Accessible.Graphic
        Accessible.name: i18nc("@info", "Lock screen preview")
        Image {
            anchors.fill: parent
            source: page.rayePicture ? page.rayeWallpaper : lock.image
            sourceSize: Qt.size(640, 400)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
        }
        Column {
            anchors { top: parent.top; topMargin: 28; horizontalCenter: parent.horizontalCenter }
            Text {
                id: clock
                anchors.horizontalCenter: parent.horizontalCenter
                text: Qt.formatTime(new Date(), Qt.locale().timeFormat(Locale.ShortFormat))
                font.family: Theme.body.family
                font.pixelSize: 40
                font.weight: Font.DemiBold
                color: "#f1f0f4"
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: new Date().toLocaleDateString(Qt.locale(), Locale.LongFormat)
                font: Theme.caption
                color: "#f1f0f4"
            }
        }
    }

    Card {
        Layout.fillWidth: true
        searchId: "lock-picture"
        iconName: "org.raye.settings-lock-screen"
        iconFallback: "system-lock-screen"
        title: i18nc("@title", "Personalize your lock screen")
        description: page.rayePicture ? i18nc("@info", "The Raye picture") : i18nc("@info", "Your picture")
        Button {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            visible: !page.rayePicture
            text: i18nc("@action:button", "Use the Raye picture")
            onClicked: lock.useRayePicture()
        }
        Button {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: i18nc("@action:button", "Browse photos")
            onClicked: photoDialog.open()
        }
    }

    Flow {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s1
        spacing: Theme.s2
        visible: page.tiles.length > 0
        Repeater {
            model: page.tiles
            delegate: BackgroundTile {
                required property var modelData
                source: modelData.preview
                label: modelData.name
                selected: lock.image.toString() === modelData.url
                onClicked: lock.setPicture(modelData.url)
            }
        }
    }

    ToggleCard {
        id: onSignIn
        Layout.fillWidth: true
        Layout.topMargin: Theme.s3
        searchId: "sign-in-picture"
        iconName: "org.raye.settings-lock-screen"
        iconFallback: "system-lock-screen"
        title: i18nc("@title", "Show the lock screen picture on the sign-in screen")
        description: page.rayePicture ? i18nc("@info", "The sign-in screen shows the Raye picture already")
                                      : i18nc("@info", "Anyone who can see this PC can see the picture. Needs root access.")
        enabled: !page.rayePicture && !lock.busy
        checked: lock.onSignIn || page.rayePicture
        onCheckedChanged: if (enabled && checked !== lock.onSignIn) lock.setOnSignIn(checked)
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    NavCard {
        Layout.fillWidth: true
        searchId: "screen-timeout"
        iconName: "org.raye.settings-power"
        iconFallback: "battery"
        title: i18nc("@title", "Screen timeout")
        description: i18nc("@info", "The screen locks when it turns off")
        target: "system/power"
    }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-background"
        iconFallback: "preferences-desktop-wallpaper"
        title: i18nc("@title settings page", "Background")
        target: "personalization/background"
    }

    FileDialog {
        id: photoDialog
        title: i18nc("@title:window", "Choose a photo")
        fileMode: FileDialog.OpenFile
        currentFolder: background.picturesFolder()
        nameFilters: [i18nc("@item file type", "Pictures (*.jpg *.jpeg *.png *.webp)")]
        onAccepted: lock.setPicture(selectedFile)
    }

    ContentDialog {
        id: signInError
        title: i18nc("@title:window", "Couldn't change the sign-in screen")
        closeText: i18nc("@action:button", "Close")
    }

    Connections {
        target: lock
        function onSignInFailed(reason) {
            // the switch goes back to what is true
            onSignIn.checked = Qt.binding(() => lock.onSignIn || page.rayePicture)
            if (reason === "cancelled")
                return
            signInError.text = reason === "picture"
                ? i18n("The sign-in screen can show PNG, JPEG and WebP pictures up to 32 MB. Your lock screen still has this picture.")
                : i18n("Something went wrong. Your lock screen still has this picture.")
            signInError.open()
        }
    }
}
