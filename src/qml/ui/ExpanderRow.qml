// SPDX-License-Identifier: GPL-3.0-or-later
// One row inside an Expander: the text lines up with the header's
// text (58 px), the control with the header's control, and a thin line
// separates it from the row above. Clickable rows highlight.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

T.AbstractButton {
    id: row

    property string title
    property string description
    property string searchId
    property bool clickable: false
    property string actionGlyph: ""
    // for the gallery: "hover", "pressed" or "focus" shown at rest
    property string previewState: ""
    default property alias content: trailing.data

    readonly property bool showHover: clickable && enabled && (hovered || previewState === "hover")
    readonly property bool showPressed: clickable && enabled && (down || previewState === "pressed")
    readonly property real actionWidth: actionGlyph !== "" ? Theme.actionIconSize + Theme.cardActionGap : 0
    // controls end where the header's do, before its chevron
    readonly property real controlRight: Theme.cardPadding + Theme.actionIconSize + Theme.cardActionGap

    Layout.fillWidth: true
    implicitWidth: 480
    implicitHeight: Math.max(Theme.expanderRowHeight,
                             Math.max(texts.implicitHeight, trailing.implicitHeight) + 2 * Theme.s4)
    hoverEnabled: clickable
    focusPolicy: clickable ? Qt.StrongFocus : Qt.NoFocus
    padding: 0

    Accessible.role: clickable ? Accessible.Button : Accessible.Grouping
    Accessible.name: title
    Accessible.description: description

    Keys.onReturnPressed: if (clickable) row.clicked()
    Keys.onEnterPressed: if (clickable) row.clicked()

    function flash() { flashAnimation.restart() }

    function _place(left, itemWidth, areaWidth) {
        return mirrored ? areaWidth - left - itemWidth : left
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            color: row.showPressed ? Theme.subtlePressed : row.showHover ? Theme.subtleHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.divider
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
        FocusRing {
            anchors.margins: 1
            shown: row.clickable && (row.visualFocus || row.previewState === "focus")
            radius: 2
        }
    }

    contentItem: Item {
        Column {
            id: texts
            readonly property real textX: Theme.cardTextLeft
            x: row._place(textX, width, parent.width)
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, parent.width - textX - row.controlRight - trailing.width
                            - (trailing.width > 0 ? Theme.cardHeaderGap : 0))
            Text {
                width: parent.width
                text: row.title
                font: Theme.body
                horizontalAlignment: Text.AlignLeft
                color: !row.enabled ? Theme.textDisabled : row.showPressed ? Theme.textSecondary : Theme.text
                wrapMode: Text.Wrap
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.bodyLineHeight
                verticalAlignment: Text.AlignVCenter
                Accessible.ignored: true
            }
            Text {
                width: parent.width
                visible: row.description !== ""
                text: row.description
                font: Theme.caption
                horizontalAlignment: Text.AlignLeft
                color: row.enabled ? Theme.textSecondary : Theme.textDisabled
                wrapMode: Text.Wrap
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.captionLineHeight
                Accessible.ignored: true
            }
        }
        Row {
            id: trailing
            spacing: Theme.s3
            x: row._place(parent.width - row.controlRight - width, width, parent.width)
            anchors.verticalCenter: parent.verticalCenter
        }
        Glyph {
            visible: row.actionGlyph !== ""
            name: row.actionGlyph
            size: Theme.actionIconSize
            color: row.enabled ? Theme.textSecondary : Theme.textDisabled
            x: row._place(parent.width - Theme.cardPadding - width, width, parent.width)
            anchors.verticalCenter: parent.verticalCenter
        }
    }
}
