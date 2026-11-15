// SPDX-License-Identifier: GPL-3.0-or-later
// The top of Power & battery on a laptop: the charge on a ring, the
// level in big type and what it means ("About 3 h 20 min left").
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import org.raye.settings

Rectangle {
    id: banner

    property PowerSettings power

    readonly property real level: power ? power.batteryLevel : 0
    readonly property bool charging: power && power.batteryState === "charging"
    readonly property bool low: level <= 20 && !charging
    readonly property color ringColor: low ? Theme.caution : charging ? Theme.success : Theme.accent

    implicitHeight: row.implicitHeight + 2 * Theme.s7
    radius: Theme.radiusOverlay
    color: Theme.card
    border.width: 1
    border.color: Theme.cardBorder

    Accessible.role: Accessible.StaticText
    Accessible.name: i18nc("@info battery level", "Battery %1 percent", Math.round(level)) + ", " + (power ? power.batteryText : "")

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: Theme.s7
        spacing: Theme.s7

        Item {
            Layout.preferredWidth: 112
            Layout.preferredHeight: 112
            Shape {
                anchors.fill: parent
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: Theme.strokeSurface
                    strokeWidth: 10
                    fillColor: "transparent"
                    PathAngleArc { centerX: 56; centerY: 56; radiusX: 49; radiusY: 49; startAngle: -90; sweepAngle: 360 }
                }
                ShapePath {
                    strokeColor: banner.ringColor
                    strokeWidth: 10
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    PathAngleArc {
                        centerX: 56; centerY: 56; radiusX: 49; radiusY: 49; startAngle: -90
                        sweepAngle: 3.6 * Math.max(1, Math.min(100, banner.level))
                        Behavior on sweepAngle { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }
                    }
                }
            }
            // a lightning bolt in the middle while charging
            Shape {
                visible: banner.charging
                anchors.centerIn: parent
                width: 24
                height: 32
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeWidth: 0
                    fillColor: Theme.success
                    startX: 14; startY: 0
                    PathLine { x: 2; y: 18 }
                    PathLine { x: 11; y: 18 }
                    PathLine { x: 9; y: 32 }
                    PathLine { x: 22; y: 13 }
                    PathLine { x: 13; y: 13 }
                    PathLine { x: 14; y: 0 }
                }
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.s1
            Text {
                font: Theme.titleLarge
                color: Theme.text
                text: i18nc("@info battery level", "%1%", Math.round(banner.level))
            }
            Text {
                Layout.fillWidth: true
                font: Theme.body
                color: banner.low ? Theme.caution : Theme.textSecondary
                wrapMode: Text.Wrap
                text: banner.power ? banner.power.batteryText : ""
            }
        }
    }
}
