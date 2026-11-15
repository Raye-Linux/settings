// SPDX-License-Identifier: GPL-3.0-or-later
// A drop-down list, 32 px high like a button. The list opens over the
// field with the chosen item where the field is (as in Windows), on a
// raised surface with a shadow; the chosen item carries the accent pill.
import QtQuick
import QtQuick.Templates as T

T.ComboBox {
    id: control

    // for the gallery: "hover", "pressed" or "focus" shown at rest
    property string previewState: ""

    readonly property bool showHover: enabled && (hovered || previewState === "hover")
    readonly property bool showPressed: enabled && (pressed || popup.visible || previewState === "pressed")

    implicitWidth: Math.max(Theme.controlMinWidth,
                            implicitContentWidth + leftPadding + rightPadding,
                            control._widest + leftPadding + rightPadding)
    implicitHeight: Theme.controlHeight
    leftPadding: 11
    rightPadding: 11 + 12 + Theme.s3
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.ComboBox
    Accessible.name: displayText

    // the widest item, so the field does not jump when the choice changes
    readonly property real _widest: {
        let widest = 0
        for (let i = 0; i < count; ++i)
            widest = Math.max(widest, metrics.advanceWidth(textAt(i)))
        return Math.ceil(widest)
    }
    FontMetrics { id: metrics; font: Theme.body }

    contentItem: Text {
        text: control.displayText
        font: Theme.body
        color: !control.enabled ? Theme.textDisabled
             : control.showPressed ? Theme.textSecondary : Theme.text
        horizontalAlignment: Text.AlignLeft
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    indicator: Glyph {
        name: "chevron-down"
        size: 12
        color: control.enabled ? Theme.textSecondary : Theme.textDisabled
        x: control.mirrored ? 11 : control.width - width - 11
        y: Math.round((control.height - height) / 2) + (control.showPressed ? 1 : 0)
        Behavior on y { NumberAnimation { duration: Theme.durationFaster } }
    }

    background: Item {
        implicitWidth: Theme.controlMinWidth
        implicitHeight: Theme.controlHeight
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: !control.enabled ? Theme.controlDisabled
                 : control.showPressed ? Theme.controlPressed
                 : control.showHover ? Theme.controlHover : Theme.control
            border.width: 1
            border.color: Theme.strokeControl
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
        Item {
            width: parent.width
            height: 1
            y: parent.height - 1
            clip: true
            visible: control.enabled && !control.showPressed
            Rectangle {
                width: parent.width
                height: Theme.radiusControl * 2
                y: 1 - height
                radius: Theme.radiusControl
                color: "transparent"
                border.width: 1
                border.color: Theme.strokeControlBottom
            }
        }
        FocusRing {
            shown: control.visualFocus || control.previewState === "focus"
        }
    }

    delegate: T.ItemDelegate {
        id: item
        required property int index
        width: ListView.view ? ListView.view.width : implicitWidth
        implicitWidth: label.implicitWidth + leftPadding + rightPadding
        implicitHeight: Theme.popupItemHeight
        leftPadding: 12
        rightPadding: 12
        hoverEnabled: true
        highlighted: control.highlightedIndex === index
        readonly property bool current: control.currentIndex === index

        Accessible.role: Accessible.ListItem
        Accessible.name: label.text
        Accessible.selected: current

        contentItem: Text {
            id: label
            text: control.textAt(item.index)
            font: Theme.body
            color: item.enabled ? (item.down ? Theme.textSecondary : Theme.text) : Theme.textDisabled
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            Accessible.ignored: true
        }
        background: Item {
            Rectangle {
                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                anchors.topMargin: 2
                anchors.bottomMargin: 2
                radius: 4
                color: item.down ? Theme.subtlePressed
                     : item.highlighted || item.hovered ? Theme.subtleHover
                     : item.current ? Theme.subtleSelected : "transparent"
            }
            AccentPill {
                visible: item.current
                x: control.mirrored ? parent.width - width - 4 : 4
                anchors.verticalCenter: parent.verticalCenter
                height: 16
            }
        }
    }

    popup: Flyout {
        id: list
        leftPadding: 0
        rightPadding: 0
        readonly property real itemHeight: Theme.popupItemHeight
        // the chosen item sits on the field
        y: -topPadding - Math.max(0, control.currentIndex) * itemHeight
           + Math.round((control.height - itemHeight) / 2)
        width: Math.max(control.width, contentItem.implicitWidth + leftPadding + rightPadding)
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 12 * itemHeight + 8)
        topPadding: 4
        bottomPadding: 4
        margins: 8

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            implicitWidth: control._widest + 24
            model: control.delegateModel
            currentIndex: control.highlightedIndex
            highlightMoveDuration: 0
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollIndicator.vertical: T.ScrollIndicator {}
        }
    }
}
