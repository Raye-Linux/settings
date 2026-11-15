// SPDX-License-Identifier: GPL-3.0-or-later
// A link that looks like text in the accent colour: "View details",
// "Manage your account". With `url` set it opens it; otherwise it
// emits clicked().
import QtQuick
import QtQuick.Templates as T

T.AbstractButton {
    id: control

    property url url
    // for the gallery: "hover", "pressed" or "focus" shown at rest
    property string previewState: ""

    readonly property bool showHover: enabled && (hovered || previewState === "hover")
    readonly property bool showPressed: enabled && (down || previewState === "pressed")

    implicitWidth: label.implicitWidth + leftPadding + rightPadding
    implicitHeight: Math.max(label.implicitHeight + topPadding + bottomPadding, 24)
    padding: 0
    font: Theme.bodyStrong
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Link
    Accessible.name: text

    Keys.onReturnPressed: control.clicked()
    Keys.onEnterPressed: control.clicked()
    onClicked: if (url.toString() !== "") Qt.openUrlExternally(url)

    contentItem: Text {
        id: label
        text: control.text
        font.family: control.font.family
        font.pointSize: control.font.pointSize
        font.weight: control.font.weight
        font.underline: control.showHover
        color: !control.enabled ? Theme.textDisabled
             : control.showPressed ? Qt.alpha(Theme.accentText, 0.7)
             : Theme.accentText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        Accessible.ignored: true
    }

    HoverHandler {
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
    }

    background: Item {
        FocusRing {
            shown: control.visualFocus || control.previewState === "focus"
            radius: 4
        }
    }
}
