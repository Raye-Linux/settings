// SPDX-License-Identifier: GPL-3.0-or-later
// How one protection stands, on the right of its card: a status mark and
// one word ("On", "Off", "Not available").
import QtQuick
import org.raye.settings

Row {
    id: status

    // "success", "caution", "critical", "info"
    property string state: "success"
    property string text

    spacing: Theme.s3
    Accessible.role: Accessible.StaticText
    Accessible.name: text

    StatusGlyph {
        anchors.verticalCenter: parent.verticalCenter
        status: status.state
        size: 16
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        text: status.text
        font: Theme.body
        color: Theme.text
        Accessible.ignored: true
    }
}
