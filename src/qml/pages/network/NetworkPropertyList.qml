// SPDX-License-Identifier: GPL-3.0-or-later
// A card of read-only properties ("IPv4 address", "Physical address
// (MAC)") in two columns, with one Copy button for all of them, as the
// properties of a network read in Windows.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

Rectangle {
    id: list

    // [{ label, value }]
    property var entries: []
    property string searchId
    readonly property string _text: {
        let lines = []
        for (const entry of entries)
            lines.push(i18nc("@label a property name before its value", "%1:", entry.label) + " " + entry.value)
        return lines.join("\n")
    }

    function flash() { flashFrame.flash() }

    Layout.fillWidth: true
    implicitHeight: content.implicitHeight + 2 * Theme.cardPadding
    radius: Theme.radiusControl
    color: Theme.card
    border.width: 1
    border.color: Theme.cardBorder
    Accessible.role: Accessible.Grouping

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.cardPadding
        anchors.leftMargin: Theme.cardTextLeft
        spacing: Theme.cardHeaderGap

        GridLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            columns: 2
            columnSpacing: Theme.s7
            rowSpacing: Theme.s3
            Repeater {
                model: list.entries.length * 2
                Text {
                    required property int index
                    readonly property var entry: list.entries[Math.floor(index / 2)]
                    readonly property bool isLabel: index % 2 === 0
                    Layout.fillWidth: !isLabel
                    Layout.alignment: Qt.AlignTop
                    text: isLabel ? i18nc("@label a property name before its value", "%1:", entry.label) : entry.value
                    textFormat: Text.PlainText
                    font: Theme.body
                    color: isLabel ? Theme.textSecondary : Theme.text
                    wrapMode: isLabel ? Text.NoWrap : Text.WrapAnywhere
                    horizontalAlignment: Text.AlignLeft
                }
            }
        }
        Button {
            id: copyButton
            Layout.alignment: Qt.AlignTop
            enabled: list.entries.length > 0
            text: copiedTimer.running ? i18nc("@action:button the value is on the clipboard", "Copied")
                                      : i18nc("@action:button", "Copy")
            Accessible.description: i18nc("@info", "Copy all these properties")
            onClicked: {
                clipboard.text = list._text
                clipboard.selectAll()
                clipboard.copy()
                clipboard.text = ""
                copiedTimer.restart()
            }
        }
    }
    TextEdit { id: clipboard; visible: false }
    Timer { id: copiedTimer; interval: 2000 }

    Rectangle {
        id: flashFrame
        anchors.fill: parent
        radius: Theme.radiusControl
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: 0
        function flash() { flashAnimation.restart() }
        SequentialAnimation {
            id: flashAnimation
            NumberAnimation { target: flashFrame; property: "opacity"; to: 1; duration: Theme.durationFast }
            PauseAnimation { duration: 600 }
            NumberAnimation { target: flashFrame; property: "opacity"; to: 0; duration: Theme.durationNormal }
        }
    }
}
