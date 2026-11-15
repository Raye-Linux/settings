// SPDX-License-Identifier: GPL-3.0-or-later
// One picture to choose in Personalization > Background: 120 x 80,
// radius 6, an accent ring when it is the one on the desktop.
import QtQuick
import QtQuick.Templates as T
import org.raye.settings

T.AbstractButton {
    id: tile

    property url source
    property string label
    property bool selected: false

    implicitWidth: 120
    implicitHeight: 80
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.RadioButton
    Accessible.name: label !== "" ? label : i18nc("@info a picture", "Picture")
    Accessible.checked: selected

    contentItem: Item {
        Rectangle {
            id: frame
            anchors.fill: parent
            radius: Theme.radiusControl
            color: Theme.control
            clip: true
            Image {
                anchors.fill: parent
                source: tile.source
                sourceSize.width: 240
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
        }
        // hover lifts it a little, as the tiles of the launch screen
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: tile.down ? "#26000000" : tile.hovered ? "#14ffffff" : "transparent"
        }
        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: Theme.radiusControl + 3
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            visible: tile.selected
        }
        FocusRing { shown: tile.visualFocus }
    }
}
