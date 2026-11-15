// SPDX-License-Identifier: GPL-3.0-or-later
// One category of a drive on the Storage page ("Apps", "Documents"):
// icon, name, a thin bar for its share of the space used, its size on
// the right. A folder category opens in Files, Apps opens Installed
// apps, Temporary files opens Clean up.
import QtQuick
import org.raye.settings

Card {
    id: card

    property string categoryId
    property real bytes: 0
    property real usedBytes: 0
    property string sizeText
    property color barColor: Theme.accent

    readonly property real _fraction: usedBytes > 0 ? Math.min(1, Math.max(0, bytes / usedBytes)) : 0

    // a line under the title, drawn over by the bar below
    description: " "
    Accessible.description: sizeText

    Text {
        text: card.sizeText
        font: Theme.body
        color: card.enabled ? Theme.textSecondary : Theme.textDisabled
        horizontalAlignment: Text.AlignRight
        Accessible.ignored: true
    }

    // the bar sits where the description line is (title 20, line 16)
    Item {
        id: track
        parent: card.contentItem
        readonly property real textX: card.textLeft - card.leftPadding
        width: Math.max(0, Math.min(card.contentItem.width - textX - card.actionWidth - Theme.controlMinWidth
                                    - Theme.cardHeaderGap, 520))
        x: card.mirrored ? card.contentItem.width - textX - width : textX
        y: Math.round((card.contentItem.height - Theme.bodyLineHeight - Theme.captionLineHeight) / 2)
           + Theme.bodyLineHeight + 6
        height: 4

        Rectangle {
            anchors.fill: parent
            radius: 2
            color: Theme.night ? Theme.control : Theme.divider
        }
        Rectangle {
            // 0 until shown, so the bar grows from the left when the page opens
            property bool ready: false
            property real shown: ready ? card._fraction : 0
            Component.onCompleted: ready = true
            Behavior on shown {
                NumberAnimation {
                    duration: Theme.animationsOff ? 0 : Math.round(400 * Theme.motionFactor)
                    easing.type: Theme.easingEnter
                }
            }
            width: card._fraction > 0 ? Math.max(height, parent.width * shown) : 0
            x: card.mirrored ? parent.width - width : 0
            height: parent.height
            radius: 2
            color: card.barColor
        }
    }
}
