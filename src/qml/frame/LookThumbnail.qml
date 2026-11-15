// SPDX-License-Identifier: GPL-3.0-or-later
// A look to choose with one click (Raye Night, Raye Day): its preview,
// its name under it, and an accent ring on the one in use. When the
// look has no preview picture, a small desktop is drawn in its colours.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

T.AbstractButton {
    id: thumb

    property string name
    property url preview
    property bool current: false
    property bool night: true

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    implicitWidth: 140
    implicitHeight: picture.height + Theme.s2 + label.implicitHeight

    Accessible.role: Accessible.RadioButton
    Accessible.name: name
    Accessible.checkable: true
    Accessible.checked: current
    Keys.onReturnPressed: clicked()

    scale: down ? 0.98 : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFaster } }

    contentItem: Item {}
    background: Item {
        Item {
            id: picture
            width: parent.width
            height: Math.round(width * 10 / 16)

            Rectangle {
                id: frame
                anchors.fill: parent
                radius: Theme.radiusControl
                clip: true
                color: thumb.night ? "#141318" : "#f5f4f7"
                border.width: 1
                border.color: Theme.cardBorder

                Image {
                    id: image
                    anchors.fill: parent
                    anchors.margins: 1
                    source: thumb.preview
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    sourceSize.width: 320
                    visible: status === Image.Ready
                }
                // no picture: a window and a taskbar in the look's colours
                Item {
                    anchors.fill: parent
                    visible: image.status !== Image.Ready
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: 1
                        radius: Theme.radiusControl - 1
                        gradient: Gradient {
                            GradientStop { position: 0; color: thumb.night ? "#26252b" : "#e8e7ec" }
                            GradientStop { position: 1; color: thumb.night ? "#141318" : "#fbfbfc" }
                        }
                    }
                    Rectangle {
                        x: parent.width * 0.18; y: parent.height * 0.16
                        width: parent.width * 0.64; height: parent.height * 0.5
                        radius: 3
                        color: thumb.night ? "#212026" : "#ffffff"
                        border.width: 1
                        border.color: thumb.night ? "#33ffffff" : "#1e17151c"
                        Rectangle {
                            x: 6; y: 6; width: parent.width * 0.35; height: 4; radius: 2
                            color: "#7b3cff"
                        }
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 4
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width * 0.5; height: parent.height * 0.1
                        radius: height / 2
                        color: thumb.night ? "#cc17151c" : "#ccffffff"
                    }
                }
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: Theme.radiusControl + 3
                color: "transparent"
                border.width: 2
                border.color: Theme.accent
                visible: thumb.current
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusControl
                color: Theme.subtleHover
                visible: thumb.hovered && !thumb.current
            }
            FocusRing { shown: thumb.visualFocus }
        }
        Text {
            id: label
            anchors.top: picture.bottom
            anchors.topMargin: Theme.s2
            width: parent.width
            text: thumb.name
            font: thumb.current ? Theme.captionStrong : Theme.caption
            color: Theme.text
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }
}
