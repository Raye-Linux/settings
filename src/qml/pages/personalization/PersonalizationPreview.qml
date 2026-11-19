// SPDX-License-Identifier: GPL-3.0-or-later
// The live preview of Personalization: the desktop as it is now, drawn
// small (320 x 200, radius 8) with a window and the taskbar in the
// current colours and accent. It follows every change on the page.
import QtQuick
import org.raye.settings

Item {
    id: preview

    // what the desktop shows: a picture, else a colour
    property url image
    property color color: Theme.backdropSolid
    property color accent: Theme.accent
    // draw it as Raye Day ("day") or Raye Night ("night"), else as now
    property string mode: ""
    readonly property bool night: mode === "" ? Theme.night : mode === "night"
    // the Windows 10 style: taskbar on the left, square corners
    property bool windows10: false

    implicitWidth: 320
    implicitHeight: 200
    Accessible.role: Accessible.Graphic
    Accessible.name: i18nc("@info", "Preview of your desktop")

    // the desktop
    Rectangle {
        id: desk
        anchors.fill: parent
        radius: Theme.radiusOverlay
        color: preview.color
        clip: true
        Image {
            anchors.fill: parent
            visible: preview.image.toString() !== ""
            source: preview.image
            sourceSize.width: 640
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            smooth: true
        }

        // a window
        Rectangle {
            id: win
            x: parent.width * 0.16
            y: parent.height * 0.14
            width: parent.width * 0.58
            height: parent.height * 0.56
            radius: preview.windows10 ? 0 : 4
            color: preview.night ? "#212026" : "#ffffff"
            border.width: 1
            border.color: preview.night ? "#33ffffff" : "#1a17151c"
            // everything inside scales with the window, so a small preview
            // (a theme card) looks like the big one
            readonly property real unit: width / 186
            Row {
                x: 10 * parent.unit
                y: 9 * parent.unit
                spacing: 4 * parent.unit
                Repeater {
                    model: 3
                    Rectangle {
                        width: 22 * win.unit; height: Math.max(2, 4 * win.unit); radius: height / 2
                        color: preview.night ? "#3df1f0f4" : "#3317151c"
                    }
                }
            }
            Column {
                x: 10 * parent.unit
                y: 26 * parent.unit
                spacing: 6 * parent.unit
                Repeater {
                    model: [120, 90, 104]
                    Rectangle {
                        required property int index
                        required property int modelData
                        width: modelData * win.unit; height: Math.max(3, 6 * win.unit); radius: height / 2
                        color: preview.night ? (index === 0 ? "#59f1f0f4" : "#33f1f0f4") : (index === 0 ? "#4d17151c" : "#2617151c")
                    }
                }
            }
            // a button in the accent colour
            Rectangle {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 10 * parent.unit
                width: 46 * parent.unit
                height: 14 * parent.unit
                radius: 3 * parent.unit
                color: preview.accent
            }
        }

        // the taskbar, centred as in Raye, or on the left
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.round(22 * win.unit)
            color: preview.night ? "#d9141318" : "#e6f5f4f7"
            Row {
                x: preview.windows10 ? 6 * win.unit : (parent.width - width) / 2
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6 * win.unit
                // Start: the Raye eye's colours
                Rectangle {
                    width: 12 * win.unit; height: width; radius: preview.windows10 ? 0 : 3 * win.unit
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0; color: Theme.brandViolet }
                        GradientStop { position: 1; color: Theme.brandMagenta }
                    }
                }
                Repeater {
                    model: 4
                    Rectangle {
                        required property int index
                        width: 12 * win.unit; height: width; radius: preview.windows10 ? 0 : 3 * win.unit
                        color: preview.night ? "#4df1f0f4" : "#4017151c"
                        Rectangle {
                            visible: index === 0
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.top: parent.bottom
                            anchors.topMargin: 2
                            width: 6 * win.unit; height: Math.max(1, 2 * win.unit); radius: height / 2
                            color: preview.accent
                        }
                    }
                }
            }
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusOverlay
        color: "transparent"
        border.width: 1
        border.color: Theme.strokeSurface
    }
}
