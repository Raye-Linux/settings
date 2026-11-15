// SPDX-License-Identifier: GPL-3.0-or-later
// The list that drops under the search box while typing: up to eight
// settings (icon, name with the typed part in a heavier weight, where it
// is), then "Show all results". The box keeps the keyboard: it moves the
// current row with move() and asks for it with currentId.
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

Flyout {
    id: flyout

    property SearchModel model
    // -1: none; model.count: the "Show all results" row
    property int currentIndex: -1
    readonly property bool showAllCurrent: model !== null && currentIndex === model.count && model.count > 0
    readonly property string currentId: model && currentIndex >= 0 ? model.entryIdAt(currentIndex) : ""

    signal chosen(string entryId)
    signal showAll()

    // step through the rows with the arrow keys; past either end, back
    // to the box
    function move(step) {
        if (!model)
            return
        const last = model.count > 0 ? model.count : -1
        let next = currentIndex + step
        if (next > last)
            next = -1
        else if (next < -1)
            next = last
        currentIndex = next
        if (currentIndex >= 0 && currentIndex < model.count)
            list.positionViewAtIndex(currentIndex, ListView.Contain)
    }

    padding: Theme.s2
    margins: Theme.s3
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutsideParent
    // the search box keeps the keyboard focus
    focus: false
    modal: false
    implicitWidth: 400
    implicitHeight: contentItem.implicitHeight + topPadding + bottomPadding
    shadowStrength: Theme.night ? 0.55 : 0.1

    Connections {
        target: flyout.model
        function onCountChanged() { flyout.currentIndex = -1 }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Easing.OutCubic }
        NumberAnimation { property: "y"; from: flyout.y - 4; to: flyout.y; duration: Theme.durationNormal; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFaster }
    }

    contentItem: Column {
        spacing: 0
        Accessible.role: Accessible.List
        Accessible.name: i18n("Suggestions")

        ListView {
            id: list
            width: parent.width
            height: contentHeight
            interactive: false
            model: flyout.model
            visible: count > 0

            delegate: T.ItemDelegate {
                id: row
                required property int index
                required property string entryId
                required property string label
                // "highlighted" is a property of the delegate itself
                required property var model
                required property string breadcrumb
                required property string iconName
                required property string iconFallback

                readonly property bool current: flyout.currentIndex === index

                width: ListView.view.width
                height: breadcrumb !== "" ? 52 : 40
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                Accessible.role: Accessible.ListItem
                Accessible.name: breadcrumb !== "" ? label + ", " + breadcrumb : label
                Accessible.selected: current
                onClicked: flyout.chosen(entryId)

                background: Rectangle {
                    radius: Theme.radiusControl
                    color: row.current ? Theme.subtleSelected
                           : row.down ? Theme.subtlePressed
                           : row.hovered ? Theme.subtleHover : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
                    AccentPill {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        visible: row.current
                    }
                }

                Kirigami.Icon {
                    id: icon
                    x: Theme.s4
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.iconSize
                    height: Theme.iconSize
                    source: row.iconName
                    fallback: row.iconFallback
                }
                Column {
                    anchors.left: icon.right
                    anchors.leftMargin: Theme.s4
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.s4
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0
                    Text {
                        width: parent.width
                        textFormat: Text.StyledText
                        // the index escapes the label; only <b> is ours
                        text: row.model.highlighted
                        font: Theme.body
                        color: Theme.text
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                    Text {
                        width: parent.width
                        visible: row.breadcrumb !== ""
                        text: row.breadcrumb
                        font: Theme.caption
                        color: Theme.textSecondary
                        elide: Text.ElideRight
                        Accessible.ignored: true
                    }
                }
            }
        }

        // nothing found: said once the search has run
        Text {
            width: parent.width
            visible: flyout.model !== null && flyout.model.count === 0 && !flyout.model.pending
            topPadding: Theme.s5
            bottomPadding: Theme.s5
            leftPadding: Theme.s4
            rightPadding: Theme.s4
            text: i18n("No results. Try other words.")
            font: Theme.body
            color: Theme.textSecondary
            wrapMode: Text.Wrap
        }

        Rectangle {
            width: parent.width
            height: 1
            visible: list.count > 0
            color: Theme.divider
        }

        T.ItemDelegate {
            id: showAllRow
            width: parent.width
            height: 40
            visible: list.count > 0
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            Accessible.role: Accessible.Button
            Accessible.name: i18n("Show all results")
            onClicked: flyout.showAll()

            background: Rectangle {
                anchors.fill: parent
                anchors.topMargin: Theme.s2
                radius: Theme.radiusControl
                color: flyout.showAllCurrent ? Theme.subtleSelected
                       : showAllRow.down ? Theme.subtlePressed
                       : showAllRow.hovered ? Theme.subtleHover : "transparent"
                AccentPill {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    visible: flyout.showAllCurrent
                }
            }
            Glyph {
                id: allGlyph
                x: Theme.s4 + 2
                y: (parent.height + Theme.s2 - height) / 2
                name: "search"
                size: 16
                color: Theme.accentText
            }
            Text {
                anchors.left: allGlyph.right
                anchors.leftMargin: Theme.s4 + 2
                y: (parent.height + Theme.s2 - height) / 2
                text: i18n("Show all results")
                font: Theme.bodyStrong
                color: Theme.accentText
            }
        }
    }
}
