// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Background: the preview, then "Personalize your
// background" (Picture, Solid colour, Slideshow, Animated) and what goes with each:
// recent pictures and Raye's own with "Browse photos" and how the
// picture fills the screen; a grid of colours and "Custom colour"; a
// folder, how often the picture changes, and shuffle; Raye's animations, a
// GIF or video of your own, and when they rest. Every desktop changes at once.
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
             { text: i18nc("@title settings page", "Background") }]

    BackgroundSettings { id: wallpaperSettings }

    // what the combo shows until the shell confirms it
    property string mode: wallpaperSettings.mode
    readonly property url rayeWallpaper: wallpaperSettings.wallpapers.length > 0 ? wallpaperSettings.wallpapers[0].preview : ""

    // the colours of Windows' solid colour grid, drawn again for Raye: deep first
    readonly property var solidColors: ["#0e0a17", "#1a1028", "#2a1052", "#3d1d78", "#4b2a8f", "#7b3cff",
                                        "#5a1846", "#7a2a2a", "#8a4a12", "#6b5a12", "#1f4d2e", "#145a5a",
                                        "#123d6b", "#1d2f7a", "#3a3a4a", "#5f6b7d", "#9a8f80", "#c9c2d6",
                                        "#c040ff", "#d6336c", "#ff7a3d", "#3f8f3a", "#2672d9", "#000000"]

    // the still of the animation on the desktop, else the first of Raye's
    readonly property url animationPreview: {
        for (const a of wallpaperSettings.animations) {
            if (a.url === wallpaperSettings.animation.toString())
                return a.preview
        }
        return wallpaperSettings.animations.length > 0 ? wallpaperSettings.animations[0].preview : ""
    }

    // the picture tiles: recent pictures, then the wallpapers of the system
    readonly property var pictureTiles: {
        const tiles = []
        const seen = {}
        for (const url of wallpaperSettings.recentImages) {
            const key = url.toString()
            if (seen[key])
                continue
            seen[key] = true
            tiles.push({ url: key, preview: url, name: "" })
        }
        for (const w of wallpaperSettings.wallpapers) {
            if (seen[w.url] || tiles.length >= 10)
                continue
            seen[w.url] = true
            tiles.push(w)
        }
        return tiles
    }

    PersonalizationPreview {
        Layout.bottomMargin: Theme.s7
        image: page.mode === "picture" ? (wallpaperSettings.previewImage.toString() !== "" ? wallpaperSettings.previewImage : page.rayeWallpaper)
             : page.mode === "slideshow" ? page.rayeWallpaper
             : page.mode === "animated" ? page.animationPreview : ""
        color: page.mode === "color" ? wallpaperSettings.color : Theme.backdropSolid
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: wallpaperSettings.failed
        closable: false
        severity: "warning"
        title: i18nc("@info", "The desktop did not take the change")
        message: i18nc("@info", "Sign out and in again, then try once more.")
    }

    Expander {
        Layout.fillWidth: true
        iconName: "org.raye.settings-background"
        iconFallback: "preferences-desktop-wallpaper"
        title: i18nc("@label:listbox", "Personalize your background")
        description: page.mode === "slideshow" ? i18nc("@info", "Pictures change on their own, from a folder you choose")
                   : page.mode === "animated" ? i18nc("@info", "Moves only while you can see it") : ""
        searchId: "background-type"
        expanded: true

        headerContent: [
            ComboBox {
                id: typeBox
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                textRole: "text"
                valueRole: "value"
                Accessible.name: i18nc("@label:listbox", "Personalize your background")
                readonly property var types: [{ text: i18nc("@item:inlistbox background type", "Picture"), value: "picture" },
                                              { text: i18nc("@item:inlistbox background type", "Solid color"), value: "color" },
                                              { text: i18nc("@item:inlistbox background type", "Slideshow"), value: "slideshow" },
                                              { text: i18nc("@item:inlistbox background type", "Animated"), value: "animated" }]
                model: types
                // not indexOfValue(): it answers -1 while the combo is being
                // made, so a page opened on a slideshow said "Picture"
                currentIndex: Math.max(0, types.findIndex(t => t.value === page.mode))
                onActivated: {
                    page.mode = currentValue
                    // a slideshow waits for its folder; picture and colour apply what is chosen
                    if (currentValue === "color")
                        wallpaperSettings.setColor(wallpaperSettings.color)
                    else if (currentValue === "animated" && wallpaperSettings.animations.length > 0)
                        wallpaperSettings.setAnimation(wallpaperSettings.animation.toString() !== "" ? wallpaperSettings.animation : wallpaperSettings.animations[0].url)
                    else if (currentValue === "picture" && page.pictureTiles.length > 0)
                        wallpaperSettings.setPicture(wallpaperSettings.image.toString() !== "" ? wallpaperSettings.image : page.pictureTiles[0].url)
                    currentIndex = Qt.binding(() => Math.max(0, types.findIndex(t => t.value === page.mode)))
                }
            }
        ]

        // --- picture ---
        BackgroundGroup {
            visible: page.mode === "picture"
            title: i18nc("@label", "Recent images")
            property string searchId: "recent-images"
            Flow {
                width: parent.width
                spacing: Theme.s3
                Repeater {
                    model: page.pictureTiles
                    delegate: BackgroundTile {
                        required property var modelData
                        source: modelData.preview
                        label: modelData.name
                        selected: wallpaperSettings.image.toString() === modelData.url
                                  || wallpaperSettings.image.toString() === modelData.url + "/"
                        onClicked: wallpaperSettings.setPicture(modelData.url)
                    }
                }
            }
        }
        ExpanderRow {
            visible: page.mode === "picture"
            title: i18nc("@label", "Choose a photo")
            searchId: "browse-photos"
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@action:button", "Browse photos")
                onClicked: photoDialog.open()
            }
        }

        // --- animated ---
        BackgroundGroup {
            visible: page.mode === "animated"
            title: i18nc("@label", "Animated backgrounds")
            property string searchId: "animated-backgrounds"
            Flow {
                width: parent.width
                spacing: Theme.s3
                Repeater {
                    model: wallpaperSettings.animations
                    delegate: BackgroundTile {
                        required property var modelData
                        source: modelData.preview
                        label: modelData.name
                        selected: wallpaperSettings.animation.toString() === modelData.url
                        onClicked: wallpaperSettings.setAnimation(modelData.url)
                    }
                }
            }
        }
        ExpanderRow {
            visible: page.mode === "animated"
            title: i18nc("@label", "Choose a video or a GIF")
            description: wallpaperSettings.preparing ? i18nc("@info", "Making a lighter copy for the desktop…")
                         : i18nc("@info", "Videos play at up to 30 frames per second, in 1080p")
            searchId: "browse-animation"
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                enabled: !wallpaperSettings.preparing
                text: i18nc("@action:button", "Browse")
                onClicked: animationDialog.open()
            }
        }
        ExpanderRow {
            visible: page.mode === "animated"
            title: i18nc("@label", "Pause when a window fills the screen")
            description: i18nc("@info", "Full-screen games and movies always pause it")
            searchId: "animated-pause-covered"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@label", "Pause when a window fills the screen")
                checked: wallpaperSettings.pauseCovered
                onToggled: wallpaperSettings.setAnimationRest(checked, wallpaperSettings.pauseOnBattery)
            }
        }
        ExpanderRow {
            visible: page.mode === "animated"
            title: i18nc("@label", "Pause on battery")
            searchId: "animated-pause-battery"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@label", "Pause on battery")
                checked: wallpaperSettings.pauseOnBattery
                onToggled: wallpaperSettings.setAnimationRest(wallpaperSettings.pauseCovered, checked)
            }
        }

        // --- solid colour ---
        BackgroundGroup {
            visible: page.mode === "color"
            title: i18nc("@label", "Choose your background color")
            property string searchId: "background-colour"
            ColorsSwatches {
                colors: page.solidColors
                current: wallpaperSettings.color
                columns: 12
                onPicked: (c) => wallpaperSettings.setColor(c)
            }
        }
        ExpanderRow {
            visible: page.mode === "color"
            title: i18nc("@label", "Custom colors")
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@action:button", "View colors")
                onClicked: {
                    colorDialog.selectedColor = wallpaperSettings.color
                    colorDialog.open()
                }
            }
        }

        // --- slideshow ---
        ExpanderRow {
            visible: page.mode === "slideshow"
            title: i18nc("@label", "Choose a picture album for a slideshow")
            description: wallpaperSettings.slideshowFolder !== "" ? wallpaperSettings.slideshowFolder : i18nc("@info", "No folder chosen yet")
            searchId: "slideshow-folder"
            Button {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@action:button", "Browse")
                onClicked: {
                    folderDialog.currentFolder = wallpaperSettings.slideshowFolder !== "" ? wallpaperSettings.slideshowFolderUrl : wallpaperSettings.picturesFolder()
                    folderDialog.open()
                }
            }
        }
        ExpanderRow {
            visible: page.mode === "slideshow"
            enabled: wallpaperSettings.slideshowFolder !== ""
            title: i18nc("@label:listbox", "Change picture every")
            searchId: "slideshow-interval"
            ComboBox {
                id: intervalBox
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                textRole: "text"
                valueRole: "value"
                Accessible.name: i18nc("@label:listbox", "Change picture every")
                model: [{ text: i18ncp("@item:inlistbox", "%1 minute", "%1 minutes", 1), value: 60 },
                        { text: i18ncp("@item:inlistbox", "%1 minute", "%1 minutes", 10), value: 600 },
                        { text: i18ncp("@item:inlistbox", "%1 minute", "%1 minutes", 30), value: 1800 },
                        { text: i18ncp("@item:inlistbox", "%1 hour", "%1 hours", 1), value: 3600 },
                        { text: i18ncp("@item:inlistbox", "%1 hour", "%1 hours", 6), value: 21600 },
                        { text: i18ncp("@item:inlistbox", "%1 day", "%1 days", 1), value: 86400 }]
                currentIndex: {
                    const i = indexOfValue(wallpaperSettings.slideshowInterval)
                    return i >= 0 ? i : 2
                }
                onActivated: wallpaperSettings.setSlideshow(wallpaperSettings.slideshowFolderUrl, currentValue, wallpaperSettings.shuffle)
            }
        }
        ExpanderRow {
            visible: page.mode === "slideshow"
            enabled: wallpaperSettings.slideshowFolder !== ""
            title: i18nc("@label", "Shuffle the picture order")
            searchId: "shuffle"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18nc("@label", "Shuffle the picture order")
                checked: wallpaperSettings.shuffle
                onToggled: wallpaperSettings.setSlideshow(wallpaperSettings.slideshowFolderUrl, wallpaperSettings.slideshowInterval, checked)
            }
        }
    }

    ComboCard {
        Layout.fillWidth: true
        visible: page.mode === "picture"
        title: i18nc("@label:listbox", "Choose a fit for your desktop image")
        searchId: "fit"
        textRole: "text"
        valueRole: "value"
        model: [{ text: i18nc("@item:inlistbox picture fit", "Fill"), value: "fill" },
                { text: i18nc("@item:inlistbox picture fit", "Fit"), value: "fit" },
                { text: i18nc("@item:inlistbox picture fit", "Stretch"), value: "stretch" },
                { text: i18nc("@item:inlistbox picture fit", "Tile"), value: "tile" },
                { text: i18nc("@item:inlistbox picture fit", "Center"), value: "centre" }]
        currentIndex: Math.max(0, indexOfValue(wallpaperSettings.fillMode))
        onActivated: wallpaperSettings.setFillMode(currentValue)
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-lock-screen"
        iconFallback: "system-lock-screen"
        title: i18nc("@title settings page", "Lock screen")
        description: i18nc("@info", "Lock and sign-in screen pictures")
        target: "personalization/lock-screen"
    }

    FileDialog {
        id: photoDialog
        title: i18nc("@title:window", "Choose a photo")
        fileMode: FileDialog.OpenFile
        currentFolder: wallpaperSettings.picturesFolder()
        nameFilters: [i18nc("@item file type", "Pictures (*.jpg *.jpeg *.png *.webp *.avif *.jxl *.bmp)")]
        onAccepted: wallpaperSettings.setPicture(selectedFile)
    }
    FileDialog {
        id: animationDialog
        title: i18nc("@title:window", "Choose a video or a GIF")
        fileMode: FileDialog.OpenFile
        currentFolder: wallpaperSettings.picturesFolder()
        nameFilters: [i18nc("@item file type", "Videos and GIFs (*.mp4 *.webm *.gif)")]
        onAccepted: wallpaperSettings.setAnimation(selectedFile)
    }
    FolderDialog {
        id: folderDialog
        title: i18nc("@title:window", "Choose a picture album")
        onAccepted: wallpaperSettings.setSlideshow(selectedFolder, intervalBox.currentValue || 1800, wallpaperSettings.shuffle)
    }
    ColorDialog {
        id: colorDialog
        title: i18nc("@title:window", "Choose a background color")
        onAccepted: wallpaperSettings.setColor(selectedColor)
    }
}
