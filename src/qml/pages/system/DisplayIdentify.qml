// SPDX-License-Identifier: GPL-3.0-or-later
// "Identify": the number of each screen, big, in the bottom left corner of
// the screen itself, for three seconds. One window per screen, made once
// and only shown or hidden, matched to the display list by the connector
// name ("HDMI-A-1").
import QtQuick
import QtQuick.Window
import org.kde.layershell as LayerShell
import org.raye.settings

Item {
    id: identify

    // DisplayConfig.outputs
    property var outputs: []
    property bool showing: false
    property int duration: 3000

    function show() {
        showing = true
        hideTimer.restart()
    }

    Timer {
        id: hideTimer
        interval: identify.duration
        onTriggered: identify.showing = false
    }

    Instantiator {
        model: Qt.application.screens
        delegate: Window {
            id: badge
            required property var modelData
            readonly property var output: identify.outputs.find(o => o.connector === modelData.name)
            readonly property int margin: 48

            // A plain popup on Wayland is placed by the compositor next to
            // the Settings window: both numbers ended up on that screen, one
            // over the other. A layer surface goes on the window's screen.
            LayerShell.Window.wantsToBeOnActiveScreen: false
            LayerShell.Window.layer: LayerShell.Window.LayerOverlay
            LayerShell.Window.anchors: LayerShell.Window.AnchorLeft | LayerShell.Window.AnchorBottom
            LayerShell.Window.exclusionZone: -1
            LayerShell.Window.keyboardInteractivity: LayerShell.Window.KeyboardInteractivityNone
            LayerShell.Window.activateOnShow: false
            LayerShell.Window.scope: "raye-display-identify"

            screen: modelData
            visible: identify.showing && output !== undefined
            flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
                   | Qt.WindowDoesNotAcceptFocus | Qt.WindowTransparentForInput
            color: "transparent"
            width: tile.width + margin
            height: tile.height + margin * 2
            // only used where there are no layer surfaces (X11)
            x: modelData.virtualX
            y: modelData.virtualY + modelData.height - height
            title: i18n("Display number")

            Rectangle {
                id: tile
                x: badge.margin
                y: badge.margin
                width: 168
                height: 168
                radius: Theme.radiusOverlay * 2
                gradient: Gradient {
                    GradientStop { position: 0; color: Theme.brandViolet }
                    GradientStop { position: 1; color: Theme.brandMagenta }
                }
                border.width: 1
                border.color: "#33ffffff"
                Text {
                    anchors.centerIn: parent
                    text: badge.output ? badge.output.number : ""
                    font.family: Theme.titleLarge.family
                    font.weight: Font.DemiBold
                    font.pixelSize: 112
                    color: "#ffffff"
                }
            }
        }
    }
}
