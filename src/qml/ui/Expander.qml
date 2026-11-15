// SPDX-License-Identifier: GPL-3.0-or-later
// A card that opens to show more rows under it (Windows' expander):
// the header is a card with a chevron that turns over in 167 ms, the
// rows below are ExpanderRow items, separated by thin lines. The body
// opens and closes in 250 ms.
//
//   Expander {
//       title: i18n("Screen and sleep")
//       ExpanderRow { title: i18n("Turn my screen off after"); ComboBox { ... } }
//   }
import QtQuick
import QtQuick.Layouts

Item {
    id: expander

    property alias title: header.title
    property alias description: header.description
    property alias iconName: header.iconName
    property alias iconFallback: header.iconFallback
    property alias iconSource: header.iconSource
    property alias searchId: header.searchId
    property alias previewState: header.previewState
    // a control in the header itself (a summary, a switch)
    property alias headerContent: header.content
    property bool expanded: false
    default property alias rows: body.data

    signal toggled()

    Layout.fillWidth: true
    implicitWidth: header.implicitWidth
    implicitHeight: header.implicitHeight + bodyClip.height

    Accessible.role: Accessible.Grouping
    Accessible.name: header.title

    function flash() { header.flash() }

    Card {
        id: header
        width: parent.width
        clickable: true
        actionGlyph: "chevron-down"
        actionRotation: expander.expanded ? 180 : 0
        squareBottom: expander.expanded || bodyClip.height > 0
        Accessible.role: Accessible.Button
        Accessible.description: expander.expanded ? i18n("Expanded") : i18n("Collapsed")
        onClicked: {
            expander.expanded = !expander.expanded
            expander.toggled()
        }
    }

    // the body: the rows on the card colour, rounded at the bottom only
    Item {
        id: bodyClip
        y: header.height
        width: parent.width
        height: expander.expanded ? body.implicitHeight : 0
        clip: true
        visible: height > 0
        opacity: expander.expanded ? 1 : 0
        Behavior on height { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal } }

        Item {
            anchors.fill: parent
            clip: true
            Rectangle {
                y: -radius
                width: parent.width
                height: parent.height + radius
                radius: Theme.radiusControl
                color: Theme.card
                border.width: 1
                border.color: Theme.cardBorder
            }
        }

        ColumnLayout {
            id: body
            width: parent.width
            spacing: 0
        }
    }
}
