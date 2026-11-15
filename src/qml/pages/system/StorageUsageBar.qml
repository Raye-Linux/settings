// SPDX-License-Identifier: GPL-3.0-or-later
// How full a drive is, as one rounded bar: each category in its own
// colour, the free space left as the empty track. It fills from the
// left when the page opens (400 ms) and moves to the new sizes after
// each count. Drawn with plain rectangles: no masks, so software
// rendering draws it the same.
import QtQuick
import org.raye.settings

Item {
    id: bar

    property real total: 0
    property real used: 0
    // [{ id, bytes }]; empty: the used part in the accent gradient alone
    property var categories: []
    // the colour of a category id
    property var colorOf: (id) => Theme.accent
    property real thickness: 8
    // colours the bar like Windows does when the drive is almost full
    property bool warn: false

    readonly property real _usedFraction: total > 0 ? Math.min(1, Math.max(0, used / total)) : 0
    property real _shown: 0
    readonly property real _usedWidth: Math.round(width * _shown)
    readonly property real _radius: height / 2
    // where each segment starts and how wide it is, in the used part
    readonly property var _segments: {
        let sum = 0
        for (const c of categories)
            sum += Math.max(0, c.bytes)
        const out = []
        if (sum <= 0)
            return out
        let x = 0
        for (const c of categories) {
            const w = _usedWidth * Math.max(0, c.bytes) / sum
            if (w >= 0.5)
                out.push({ id: c.id, x: x, w: w })
            x += w
        }
        return out
    }

    implicitWidth: 320
    implicitHeight: thickness
    LayoutMirroring.childrenInherit: true
    Accessible.role: Accessible.ProgressBar
    Accessible.description: Math.round(_usedFraction * 100) + " %"

    Component.onCompleted: _shown = _usedFraction
    on_UsedFractionChanged: _shown = _usedFraction
    Behavior on _shown {
        NumberAnimation {
            duration: Theme.animationsOff ? 0 : Math.round(400 * Theme.motionFactor)
            easing.type: Theme.easingEnter
        }
    }

    // the empty track
    Rectangle {
        anchors.fill: parent
        radius: bar._radius
        color: Theme.night ? Theme.control : Theme.divider
        border.width: Theme.night ? 1 : 0
        border.color: Theme.strokeControl
    }

    // no categories (yet): the used part in the accent gradient
    Rectangle {
        visible: bar._segments.length === 0 && bar._usedWidth > 0
        x: bar.LayoutMirroring.enabled ? bar.width - width : 0
        width: Math.max(bar._usedWidth, height)
        height: bar.height
        radius: bar._radius
        gradient: Gradient {
            orientation: Gradient.Horizontal
            GradientStop { position: 0; color: bar.warn ? Theme.caution : Theme.accent }
            GradientStop { position: 1; color: bar.warn ? Theme.critical : Theme.accentGlowEnd }
        }
    }

    // one piece per category: the first and the last are rounded on
    // their outer end, a hair of space between pieces
    Repeater {
        model: bar._segments
        Item {
            id: piece
            required property var modelData
            required property int index
            readonly property bool first: index === 0
            readonly property bool last: index === bar._segments.length - 1
            readonly property real gap: last ? 0 : Math.min(1.5, modelData.w / 3)
            readonly property real start: modelData.x
            readonly property real w: Math.max(1, modelData.w - gap)
            x: bar.LayoutMirroring.enabled ? bar.width - start - w : start
            width: w
            height: bar.height

            readonly property color fill: bar.colorOf(modelData.id)
            // the rounded end
            Rectangle {
                visible: piece.first || piece.last
                width: Math.max(parent.width, height)
                x: (piece.last && !piece.first) !== bar.LayoutMirroring.enabled ? parent.width - width : 0
                height: parent.height
                radius: bar._radius
                color: piece.fill
            }
            // the square end, over the rounded one where pieces meet
            Rectangle {
                visible: !(piece.first && piece.last)
                height: parent.height
                width: piece.first || piece.last ? Math.max(0, parent.width - bar._radius) : parent.width
                x: {
                    if (!piece.first && !piece.last)
                        return 0
                    // the square half sits on the inner side
                    const innerOnRight = piece.first !== bar.LayoutMirroring.enabled
                    return innerOnRight ? parent.width - width : 0
                }
                color: piece.fill
            }
        }
    }
}
