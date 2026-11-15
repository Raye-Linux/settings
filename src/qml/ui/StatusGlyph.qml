// SPDX-License-Identifier: GPL-3.0-or-later
// A round status mark: a check for good, "!" for a warning, "x" for an
// error, "i" for information, a dot for anything else.
import QtQuick

Item {
    id: mark

    // "success", "caution", "critical", "info", or "" (neutral)
    property string status: "info"
    property real size: 16
    // "Up to date", "Needs attention": read out by screen readers
    property string label

    implicitWidth: size
    implicitHeight: size
    Accessible.role: Accessible.Graphic
    Accessible.name: label
    Accessible.ignored: label === ""

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: Theme.statusColor(mark.status)
    }
    Glyph {
        anchors.centerIn: parent
        size: mark.size * 0.75
        weight: 2
        // dark ink on the light marks at night, white on the deep ones by day
        color: Theme.night ? Theme.backdropSolid : "#ffffff"
        name: {
            switch (mark.status) {
            case "success": return "check"
            case "caution": case "warning": return "exclamation"
            case "critical": case "error": return "close"
            case "info": case "informational": return "info"
            }
            return "dot"
        }
    }
}
