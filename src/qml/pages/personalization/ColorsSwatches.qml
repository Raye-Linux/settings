// SPDX-License-Identifier: GPL-3.0-or-later
// A grid of colours to pick from (accent colours, background colours):
// 36 px squares, radius 4, a ring and a check on the one in use. Arrow
// keys move between them, Space or Enter picks.
import QtQuick
import QtQuick.Templates as T
import org.raye.settings

Grid {
    id: grid

    // colours as "#rrggbb" strings or colors
    property var colors: []
    property color current
    property bool currentShown: true
    signal picked(color color)

    columns: 6
    spacing: Theme.s2

    function same(a, b) {
        return Math.abs(a.r - b.r) + Math.abs(a.g - b.g) + Math.abs(a.b - b.b) < 0.03
    }

    Repeater {
        model: grid.colors
        delegate: T.AbstractButton {
            id: swatch
            required property var modelData
            required property int index
            readonly property color swatchColor: modelData
            readonly property bool chosen: grid.currentShown && grid.same(swatchColor, grid.current)
            width: 36
            height: 36
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.RadioButton
            Accessible.name: swatchColor.toString()
            Accessible.checked: chosen
            onClicked: grid.picked(swatchColor)
            Keys.onRightPressed: nextItemInFocusChain().forceActiveFocus(Qt.TabFocusReason)
            Keys.onLeftPressed: nextItemInFocusChain(false).forceActiveFocus(Qt.BacktabFocusReason)
            contentItem: Item {
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: swatch.chosen ? 4 : (swatch.hovered ? 1 : 2)
                    radius: 4
                    color: swatch.swatchColor
                    border.width: 1
                    border.color: "#1f000000"
                    Behavior on anchors.margins { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingFast } }
                }
                Rectangle {
                    anchors.fill: parent
                    radius: 6
                    color: "transparent"
                    border.width: 2
                    border.color: Theme.text
                    visible: swatch.chosen
                }
                Glyph {
                    anchors.centerIn: parent
                    visible: swatch.chosen
                    name: "check"
                    size: 14
                    weight: 2
                    color: swatch.swatchColor.hslLightness > 0.6 ? "#17151c" : "#ffffff"
                }
                FocusRing { shown: swatch.visualFocus }
            }
        }
    }
}
