// SPDX-License-Identifier: GPL-3.0-or-later
// One line of the specifications in About: the name of the value in a
// column of its own, the value next to it, selectable, as Windows lays
// them out. A value still unknown is not shown at all.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

Item {
    id: row

    property string label
    property string value
    // a smaller line under the value ("8 cores, 16 threads")
    property string detail
    // the first row has a line above it, the last one more room below
    property bool last: false

    readonly property bool mirrored: LayoutMirroring.enabled
    readonly property real labelWidth: Math.min(200, width * 0.32)

    Layout.fillWidth: true
    visible: value !== ""
    // not `visible`: that is false while the expander is closed
    implicitHeight: value !== "" ? Math.max(labelText.implicitHeight, values.implicitHeight) + Theme.s3 + (last ? Theme.s4 : 0)
                                 : 0

    Accessible.role: Accessible.StaticText
    Accessible.name: label + ": " + value + (detail !== "" ? ", " + detail : "")

    Text {
        id: labelText
        x: row.mirrored ? row.width - Theme.cardTextLeft - width : Theme.cardTextLeft
        y: Theme.s3
        width: row.labelWidth
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        text: row.label
    }
    Column {
        id: values
        x: row.mirrored ? Theme.cardPadding : Theme.cardTextLeft + row.labelWidth + Theme.s5
        y: Theme.s3
        width: row.width - Theme.cardTextLeft - row.labelWidth - Theme.s5 - Theme.cardPadding
        spacing: Theme.s1

        TextEdit {
            width: parent.width
            readOnly: true
            selectByMouse: true
            font: Theme.body
            color: Theme.text
            selectionColor: Theme.accent
            selectedTextColor: Theme.textOnAccent
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.PlainText
            horizontalAlignment: row.mirrored ? TextEdit.AlignRight : TextEdit.AlignLeft
            text: row.value
            Accessible.ignored: true
        }
        Text {
            width: parent.width
            visible: row.detail !== ""
            font: Theme.caption
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            horizontalAlignment: row.mirrored ? Text.AlignRight : Text.AlignLeft
            text: row.detail
        }
    }
}
