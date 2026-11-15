// SPDX-License-Identifier: GPL-3.0-or-later
// One line of a status page, drawn as a Settings card: icon, what it
// is, how it stands (the line under the title, with a status mark on
// the right), and an optional button. The card sits inside a plain item:
// a card is a button, and its own `action` is taken.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

Item {
    id: row

    property string iconName
    property string title
    property string detail
    property bool good: true
    property string action: ""
    signal triggered()

    Layout.fillWidth: true
    // the pages that still use it put it straight in a column
    Layout.leftMargin: Theme.s5
    Layout.rightMargin: Theme.s5
    implicitWidth: card.implicitWidth
    implicitHeight: card.implicitHeight
    Accessible.role: Accessible.Grouping
    Accessible.name: title + ", " + detail

    Card {
        id: card
        width: parent.width
        iconName: row.iconName
        title: row.title
        description: row.detail

        Button {
            visible: row.action.length > 0
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: row.action
            onClicked: row.triggered()
        }
        StatusGlyph {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            status: row.good ? "success" : "caution"
            size: 16
        }
    }
}
