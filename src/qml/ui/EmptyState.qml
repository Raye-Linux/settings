// SPDX-License-Identifier: GPL-3.0-or-later
// What a page or a list shows when there is nothing to show yet ("No
// Bluetooth devices", "Nothing to clean up"): a picture, a title, one
// line, and maybe an action, centred.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami

ColumnLayout {
    id: emptyState

    property string iconName
    property string title
    property string message
    default property alias actions: actionRow.data

    Layout.fillWidth: true
    spacing: Theme.s3

    Accessible.role: Accessible.StaticText
    Accessible.name: title
    Accessible.description: message

    Kirigami.Icon {
        visible: emptyState.iconName !== ""
        source: emptyState.iconName
        Layout.preferredWidth: 64
        Layout.preferredHeight: 64
        Layout.alignment: Qt.AlignHCenter
        Layout.bottomMargin: Theme.s3
        opacity: 0.85
    }
    Text {
        Layout.fillWidth: true
        text: emptyState.title
        visible: text !== ""
        font: Theme.subtitle
        color: Theme.text
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        Accessible.ignored: true
    }
    Text {
        Layout.fillWidth: true
        Layout.maximumWidth: 480
        Layout.alignment: Qt.AlignHCenter
        text: emptyState.message
        visible: text !== ""
        font: Theme.body
        color: Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        lineHeightMode: Text.FixedHeight
        lineHeight: Theme.bodyLineHeight
        Accessible.ignored: true
    }
    Row {
        id: actionRow
        spacing: Theme.s3
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: children.length > 0 ? Theme.s3 : 0
    }
}
