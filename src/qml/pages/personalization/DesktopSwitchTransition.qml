// SPDX-License-Identifier: GPL-3.0-or-later
// The screen while Raye goes from one desktop to the other: the screen
// fades to the Raye backdrop, the small picture of this desktop gives way
// to the other one, which grows to fill the screen; then the session is
// left (finished()). With animations off (Plasma's animation speed at
// instant) it is one still frame. It gives way after a few seconds if the
// session is still there, so an app's "Save changes?" can be answered.
import QtQuick
import QtQuick.Window
import org.raye.settings

Window {
    id: transition

    property string from: "plasma"
    property string to: "hyprland"
    property string title

    // the animation is over: leave the session now
    signal finished()

    function play() {
        sequence.stop()
        backdrop.opacity = 0
        fromPicture.opacity = 1; fromPicture.scale = 1
        toPicture.opacity = 0; toPicture.scale = 0.9
        words.opacity = 0
        // the whole screen, even where nothing manages windows
        transition.x = 0
        transition.y = 0
        transition.width = Screen.width
        transition.height = Screen.height
        transition.visibility = Window.FullScreen
        transition.requestActivate()
        if (Theme.animationsOff) {
            backdrop.opacity = 1
            fromPicture.opacity = 0
            toPicture.opacity = 1; toPicture.scale = 1
            words.opacity = 1
            stillFrame.start()
        } else {
            sequence.start()
        }
    }
    function dismiss() {
        sequence.stop()
        stillFrame.stop()
        transition.visibility = Window.Hidden
    }

    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "transparent"
    visibility: Window.Hidden
    title: transition.title

    Timer {
        id: stillFrame
        interval: 300
        onTriggered: transition.finished()
    }

    Rectangle {
        id: backdrop
        anchors.fill: parent
        color: Theme.backdropSolid
        opacity: 0
        Accessible.role: Accessible.Animation
        Accessible.name: transition.title
    }

    Item {
        id: stage
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.42, 560)
        height: Math.round(width * 10 / 16)

        DesktopEnvironmentPreview {
            id: fromPicture
            anchors.fill: parent
            desktop: transition.from
        }
        DesktopEnvironmentPreview {
            id: toPicture
            objectName: "toPicture"
            anchors.fill: parent
            desktop: transition.to
            opacity: 0
        }
    }

    Text {
        id: words
        objectName: "transitionWords"
        anchors.top: stage.bottom
        anchors.topMargin: Theme.s7
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 2 * Theme.s7, 640)
        horizontalAlignment: Text.AlignHCenter
        text: transition.title
        font: Theme.subtitle
        color: Theme.text
        wrapMode: Text.WordWrap
        opacity: 0
    }

    SequentialAnimation {
        id: sequence
        // the screen settles into the Raye backdrop, with the words
        ParallelAnimation {
            NumberAnimation { target: backdrop; property: "opacity"; to: 1; duration: Theme.durationSlow; easing.type: Easing.OutCubic }
            NumberAnimation { target: words; property: "opacity"; to: 1; duration: Theme.durationSlow; easing.type: Easing.OutCubic }
        }
        // this desktop gives way to the other one
        ParallelAnimation {
            NumberAnimation { target: fromPicture; property: "opacity"; to: 0; duration: 2 * Theme.durationSlow; easing.type: Easing.InOutCubic }
            NumberAnimation { target: fromPicture; property: "scale"; to: 0.9; duration: 2 * Theme.durationSlow; easing.type: Easing.InOutCubic }
            NumberAnimation { target: toPicture; property: "opacity"; to: 1; duration: 2 * Theme.durationSlow; easing.type: Easing.InOutCubic }
            NumberAnimation { target: toPicture; property: "scale"; to: 1; duration: 2 * Theme.durationSlow; easing.type: Easing.OutBack }
        }
        PauseAnimation { duration: Theme.durationFast }
        // and opens out to the whole screen
        ParallelAnimation {
            NumberAnimation {
                target: toPicture; property: "scale"
                to: Math.max(transition.width / Math.max(1, stage.width), transition.height / Math.max(1, stage.height))
                duration: Theme.durationSlow + Theme.durationNormal; easing.type: Easing.InCubic
            }
            NumberAnimation { target: words; property: "opacity"; to: 0; duration: Theme.durationNormal }
        }
        ScriptAction { script: transition.finished() }
    }
}
