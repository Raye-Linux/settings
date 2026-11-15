// SPDX-License-Identifier: GPL-3.0-or-later
// One font family in Personalization > Fonts: a sample sentence written
// in it, its name and how many faces it has. A click opens its page.
import QtQuick
import QtQuick.Templates as T
import org.raye.settings

T.AbstractButton {
    id: tile

    property string family
    property int faceCount: 1

    implicitHeight: 128
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: Theme.cardPadding
    Accessible.role: Accessible.Button
    Accessible.name: family
    Accessible.description: i18ncp("@info", "%1 font face", "%1 font faces", faceCount)

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: tile.down ? Theme.cardPressed : tile.hovered ? Theme.cardHover : Theme.card
            border.width: 1
            border.color: Theme.cardBorder
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
        }
        FocusRing { shown: tile.visualFocus }
    }

    contentItem: Item {
        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height - names.height - Theme.s3
            text: i18nc("@info a sentence to show what a font looks like", "The quick brown fox jumps over the lazy dog.")
            font.family: tile.family
            font.pixelSize: 22
            color: Theme.text
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            maximumLineCount: 2
            Accessible.ignored: true
        }
        Column {
            id: names
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            Text {
                width: parent.width
                text: tile.family
                font: Theme.body
                color: Theme.text
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                width: parent.width
                text: i18ncp("@info", "%1 font face", "%1 font faces", tile.faceCount)
                font: Theme.caption
                color: Theme.textSecondary
            }
        }
    }
}
