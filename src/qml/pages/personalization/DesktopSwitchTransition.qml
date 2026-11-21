// SPDX-License-Identifier: GPL-3.0-or-later
// The screen while Raye goes from one desktop to the other: every screen
// fades to the colour the other desktop starts with, the small picture of
// this desktop gives way to the other one, which grows to fill the screen;
// then the session is left (finished()). With animations off (Plasma's
// animation speed at instant) it is one still frame. It gives way after a
// few seconds if the session is still there, so an app's "Save changes?"
// can be answered.
//
// One window per screen, on the compositor's overlay layer (wlr layer
// shell: KWin and Hyprland both have it), anchored to the four edges: the
// compositor itself sizes it to the whole screen, above the taskbar and
// every other window. A plain full-screen window did not cover the screen
// on the owner's Dell (Hyprland floats windows, Plasma keeps panels over a
// window it does not see as full screen), and the desktop and its taskbar
// showed around the picture. It stays up until the session is gone: the
// desktop-session helper paints the text terminal behind it in the same
// colour, and the other desktop starts on that colour too.
import QtQuick
import QtQuick.Window
import org.kde.layershell as LayerShell
import org.raye.settings

Item {
    id: transition

    property string from: "plasma"
    property string to: "hyprland"
    property string title
    // the screen the Settings window is on: the pictures and the words show
    // there, the other screens only take the colour
    property string mainScreen: ""
    readonly property bool playing: shown

    // the colour the other desktop first draws: Hyprland's background_color
    // (raye-hyprland), Raye's Plasma start screen (lookandfeel splash)
    readonly property color backdropColor: to === "hyprland" ? "#0e0a17" : "#100f13"
    readonly property color wordsColor: "#f3f1f7"

    // the animation is over: leave the session now
    signal finished()

    property bool shown: false
    // 0 ... 1: the screens settle into the backdrop, with the words
    property real settle: 0
    // 0 ... 1: this desktop's picture gives way to the other one
    property real morph: 0
    // 0 ... 1: the other desktop's picture opens out to the whole screen
    property real open: 0

    visible: shown

    function play() {
        sequence.stop()
        stillFrame.stop()
        settle = 0
        morph = 0
        open = 0
        shown = true
        if (Theme.animationsOff) {
            settle = 1
            morph = 1
            stillFrame.start()
        } else {
            sequence.start()
        }
    }
    function dismiss() {
        sequence.stop()
        stillFrame.stop()
        shown = false
    }

    Timer {
        id: stillFrame
        interval: 300
        onTriggered: transition.finished()
    }

    SequentialAnimation {
        id: sequence
        NumberAnimation { target: transition; property: "settle"; to: 1; duration: Theme.durationSlow; easing.type: Easing.OutCubic }
        NumberAnimation { target: transition; property: "morph"; to: 1; duration: 2 * Theme.durationSlow; easing.type: Easing.InOutCubic }
        PauseAnimation { duration: Theme.durationFast }
        NumberAnimation { target: transition; property: "open"; to: 1; duration: Theme.durationSlow + Theme.durationNormal; easing.type: Easing.InCubic }
        ScriptAction { script: transition.finished() }
    }

    Instantiator {
        id: overlays
        model: Qt.application.screens
        delegate: Window {
            id: overlay
            objectName: "switchOverlay"
            required property var modelData
            required property int index
            readonly property bool main: transition.mainScreen !== ""
                                         ? modelData.name === transition.mainScreen
                                         : index === 0

            LayerShell.Window.wantsToBeOnActiveScreen: false
            LayerShell.Window.layer: LayerShell.Window.LayerOverlay
            LayerShell.Window.anchors: LayerShell.Window.AnchorTop | LayerShell.Window.AnchorBottom
                                       | LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorRight
            // over the taskbar too, and never pushed aside by it
            LayerShell.Window.exclusionZone: -1
            // the apps' own "Save changes?" questions must still get the keys
            LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone
            LayerShell.Window.activateOnShow: false
            LayerShell.Window.scope: "raye-desktop-switch"

            screen: modelData
            // where there is no layer shell (X11): the whole screen, on top
            x: modelData.virtualX
            y: modelData.virtualY
            width: modelData.width
            height: modelData.height
            flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.WindowDoesNotAcceptFocus
            color: "transparent"
            title: transition.title
            visible: transition.shown

            Rectangle {
                id: backdrop
                anchors.fill: parent
                color: transition.backdropColor
                opacity: transition.settle
                Accessible.role: Accessible.Animation
                Accessible.name: transition.title
            }

            Item {
                id: stage
                visible: overlay.main
                anchors.centerIn: parent
                width: Math.min(parent.width * 0.42, 560)
                height: Math.round(width * 10 / 16)
                // how much the other picture grows to cover the screen
                readonly property real cover: Math.max(overlay.width / Math.max(1, width),
                                                       overlay.height / Math.max(1, height))

                DesktopEnvironmentPreview {
                    anchors.fill: parent
                    desktop: transition.from
                    opacity: (1 - transition.morph) * transition.settle
                    scale: 1 - 0.1 * transition.morph
                }
                DesktopEnvironmentPreview {
                    objectName: "toPicture"
                    anchors.fill: parent
                    desktop: transition.to
                    opacity: transition.morph
                    scale: (0.9 + 0.1 * transition.morph) * (1 + (stage.cover - 1) * transition.open)
                }
            }

            Text {
                objectName: "transitionWords"
                visible: overlay.main
                anchors.top: stage.bottom
                anchors.topMargin: Theme.s7
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width - 2 * Theme.s7, 640)
                horizontalAlignment: Text.AlignHCenter
                text: transition.title
                font: Theme.subtitle
                color: transition.wordsColor
                wrapMode: Text.WordWrap
                opacity: transition.settle * (1 - transition.open)
            }
        }
    }
}
