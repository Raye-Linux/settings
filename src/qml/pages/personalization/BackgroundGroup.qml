// SPDX-License-Identifier: GPL-3.0-or-later
// A row of the background expander that holds more than one control:
// its title, then what goes under it (picture tiles, colour swatches),
// both at the rows' text indent, with the divider above like every row.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

Item {
    id: group

    property string title
    default property alias content: holder.data

    Layout.fillWidth: true
    implicitHeight: column.implicitHeight + 2 * Theme.s4
    Accessible.role: Accessible.Grouping
    Accessible.name: title

    function flash() { flashAnimation.restart() }

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.divider
    }
    Column {
        id: column
        x: Theme.cardTextLeft
        y: Theme.s4
        width: parent.width - Theme.cardTextLeft - Theme.cardPadding
        spacing: Theme.s3
        Text {
            width: parent.width
            text: group.title
            font: Theme.body
            color: Theme.text
        }
        Item {
            id: holder
            width: parent.width
            implicitHeight: childrenRect.height
        }
    }
    Rectangle {
        id: flashFrame
        anchors.fill: parent
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
}
