// SPDX-License-Identifier: GPL-3.0-or-later
// The screens as tiles to drag, the way they sit on the desk: click one
// to pick it, drag it beside another and let go. "Identify" shows each
// screen's number on the screen itself for three seconds.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

Item {
    id: arrangement

    // a DisplayConfig
    property var config
    readonly property var screens: config ? config.outputs.filter(o => o.enabled) : []
    property string searchId: "arrangement"

    signal identifyRequested()

    function flash() { flashFrame.flashing.restart() }

    Layout.fillWidth: true
    implicitHeight: 248

    // the desk, scaled to fit the box with a margin
    readonly property real _left: Math.min(...screens.map(s => s.x), 0)
    readonly property real _top: Math.min(...screens.map(s => s.y), 0)
    readonly property real _right: Math.max(...screens.map(s => s.x + s.width), 1)
    readonly property real _bottom: Math.max(...screens.map(s => s.y + s.height), 1)
    readonly property real _scale: Math.min((area.width - 48) / Math.max(1, _right - _left),
                                            (area.height - 48) / Math.max(1, _bottom - _top))
    readonly property real _offsetX: (area.width - (_right - _left) * _scale) / 2
    readonly property real _offsetY: (area.height - (_bottom - _top) * _scale) / 2

    Accessible.role: Accessible.Grouping
    Accessible.name: i18n("Arrangement of your displays")

    Rectangle {
        id: area
        anchors.fill: parent
        anchors.bottomMargin: 44
        radius: Theme.radiusControl
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder

        Rectangle {
            id: flashFrame
            property alias flashing: flashAnimation
            anchors.fill: parent
            radius: Theme.radiusControl
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            opacity: 0
            SequentialAnimation {
                id: flashAnimation
                NumberAnimation { target: flashFrame; property: "opacity"; to: 1; duration: Theme.durationFast }
                PauseAnimation { duration: Math.max(0, Theme.durationFlash - Theme.durationFast - Theme.durationNormal) }
                NumberAnimation { target: flashFrame; property: "opacity"; to: 0; duration: Theme.durationNormal }
            }
        }

        Repeater {
            model: arrangement.screens
            delegate: Rectangle {
                id: tile
                required property var modelData
                readonly property bool selected: arrangement.config.selected === modelData.id
                x: arrangement._offsetX + (modelData.x - arrangement._left) * arrangement._scale
                y: arrangement._offsetY + (modelData.y - arrangement._top) * arrangement._scale
                width: modelData.width * arrangement._scale - 3
                height: modelData.height * arrangement._scale - 3
                radius: 4
                z: drag.active ? 2 : selected ? 1 : 0
                gradient: Gradient {
                    GradientStop { position: 0; color: tile.selected ? Qt.alpha(Theme.accent, 0.55) : Theme.controlHover }
                    GradientStop { position: 1; color: tile.selected ? Qt.alpha(Theme.accentGlowEnd, 0.35) : Theme.control }
                }
                border.width: tile.selected ? 2 : 1
                border.color: tile.selected ? Theme.accent : Theme.strokeStrong

                Accessible.role: Accessible.Button
                Accessible.name: i18nc("@info %1 is a number, %2 a screen name", "Display %1, %2", modelData.number, modelData.name)

                Text {
                    anchors.centerIn: parent
                    text: tile.modelData.number
                    font.family: Theme.titleLarge.family
                    font.weight: Font.DemiBold
                    font.pixelSize: Math.max(14, Math.min(tile.height * 0.45, 48))
                    color: tile.selected ? Theme.textOnAccent : Theme.text
                    Accessible.ignored: true
                }

                DragHandler {
                    id: drag
                    target: arrangement.screens.length > 1 ? tile : null
                    onActiveChanged: {
                        if (active) {
                            arrangement.config.selected = tile.modelData.id
                            return
                        }
                        // back to desk pixels, then the backend snaps it to a neighbour
                        const x = Math.round((tile.x - arrangement._offsetX) / arrangement._scale + arrangement._left)
                        const y = Math.round((tile.y - arrangement._offsetY) / arrangement._scale + arrangement._top)
                        arrangement.config.moveOutput(tile.modelData.id, x, y)
                    }
                }
                TapHandler { onTapped: arrangement.config.selected = tile.modelData.id }
            }
        }
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 36
        spacing: Theme.s3
        Button {
            text: i18nc("@action:button show each screen's number on it", "Identify")
            onClicked: arrangement.identifyRequested()
        }
        Item { Layout.fillWidth: true }
        Text {
            visible: arrangement.screens.length > 1
            text: i18n("Drag a display to match how they sit on your desk.")
            font: Theme.caption
            color: Theme.textSecondary
        }
    }
}
