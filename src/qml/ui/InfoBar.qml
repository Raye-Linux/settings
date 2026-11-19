// SPDX-License-Identifier: GPL-3.0-or-later
// A message at the top of a page (Windows' InfoBar): a status mark, a
// short bold title, the message, an optional action, and a close
// button. Title and message share a line when they fit.
//
//   InfoBar {
//       severity: "warning"
//       title: i18n("Restart needed")
//       message: i18n("Restart to finish installing updates.")
//       Button { text: i18n("Restart now"); accent: true }
//   }
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

Item {
    id: bar

    // "informational", "success", "warning" or "error"
    property string severity: "informational"
    property string title
    property string message
    property bool closable: true
    property bool open: true
    default property alias actions: actionRow.data

    signal closed()

    readonly property string _status: severity === "success" ? "success"
                                    : severity === "warning" ? "caution"
                                    : severity === "error" ? "critical" : "info"
    readonly property real _textWidth: width - 2 * Theme.cardPadding - 16 - Theme.s4
                                       - (closable ? 32 : 0)
    readonly property bool _inline: titleMetrics.advanceWidth + messageMetrics.advanceWidth + Theme.s4
                                    <= _textWidth - (actionRow.width > 0 ? actionRow.width + Theme.s4 : 0)

    Layout.fillWidth: true
    implicitWidth: 480
    implicitHeight: open ? content.implicitHeight + 2 * Theme.s4 : 0
    visible: open || height > 0
    clip: true
    Behavior on implicitHeight { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }

    Accessible.role: Accessible.AlertMessage
    Accessible.name: title
    Accessible.description: message

    TextMetrics { id: titleMetrics; font: Theme.bodyStrong; text: bar.title }
    TextMetrics { id: messageMetrics; font: Theme.body; text: bar.message }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusControl
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            color: Qt.alpha(Theme.statusColor(bar._status), bar.severity === "informational" ? 0.08 : Theme.night ? 0.13 : 0.1)
        }
    }

    RowLayout {
        id: content
        x: Theme.cardPadding
        y: Theme.s4
        width: parent.width - Theme.cardPadding - (bar.closable ? Theme.s2 : Theme.cardPadding)
        spacing: Theme.s4

        StatusGlyph {
            status: bar._status
            size: 16
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 2
        }
        // one line when it fits, else the title over the message
        Flow {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.s4
            // the metrics, not implicitWidth: a wrapping Text's implicitWidth
            // follows its width, a binding loop when the bar changes size
            Text {
                width: bar._inline ? Math.ceil(titleMetrics.advanceWidth) + 1 : parent.width
                text: bar.title
                visible: text !== ""
                font: Theme.bodyStrong
                horizontalAlignment: Text.AlignLeft
                color: Theme.text
                wrapMode: Text.Wrap
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.bodyLineHeight
                Accessible.ignored: true
            }
            Text {
                width: bar._inline ? Math.ceil(messageMetrics.advanceWidth) + 1 : parent.width
                text: bar.message
                visible: text !== ""
                font: Theme.body
                horizontalAlignment: Text.AlignLeft
                color: Theme.text
                wrapMode: Text.Wrap
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.bodyLineHeight
                textFormat: Text.StyledText
                linkColor: Theme.accentText
                onLinkActivated: (link) => Qt.openUrlExternally(link)
                Accessible.ignored: true
            }
        }
        Row {
            id: actionRow
            spacing: Theme.s3
            Layout.alignment: Qt.AlignVCenter
        }
        T.AbstractButton {
            id: closeButton
            visible: bar.closable
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: -Theme.s1
            implicitWidth: 32
            implicitHeight: 32
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: i18nc("@action:button close the message", "Close")
            onClicked: {
                bar.open = false
                bar.closed()
            }
            background: Item {
                Rectangle {
                    anchors.fill: parent
                    radius: 4
                    color: closeButton.down ? Theme.subtlePressed : closeButton.hovered ? Theme.subtleHover : "transparent"
                }
                FocusRing { shown: closeButton.visualFocus; radius: 4 }
            }
            contentItem: Item {
                Glyph {
                    anchors.centerIn: parent
                    name: "close"
                    size: 14
                    color: Theme.text
                }
            }
        }
    }
}
