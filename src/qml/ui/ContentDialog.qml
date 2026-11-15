// SPDX-License-Identifier: GPL-3.0-or-later
// A question that needs an answer before going on (Windows'
// ContentDialog): title, text, and the buttons at the bottom right,
// the first one in the accent colour. The window behind dims; Esc is
// the close button. It opens with a short zoom and fade.
//
//   ContentDialog {
//       title: i18n("Restart now?")
//       text: i18n("Save your work first.")
//       primaryText: i18n("Restart now")
//       closeText: i18n("Cancel")
//       onAccepted: power.restart()
//   }
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T

T.Dialog {
    id: dialog

    // the body when it is only text
    property string text
    property string primaryText
    property string secondaryText
    property string closeText: i18nc("@action:button", "Cancel")
    property bool primaryEnabled: true
    default property alias body: bodyColumn.data

    signal secondaryClicked()

    TextMetrics { id: _titleWidth; font: Theme.subtitle; text: dialog.title }

    parent: T.Overlay.overlay
    anchors.centerIn: parent
    modal: true
    focus: true
    closePolicy: T.Popup.CloseOnEscape
    // 320 to 548 wide as Windows', a long text wraps at 548
    width: Math.min(Theme.dialogMaxWidth,
                    Math.max(Theme.dialogMinWidth, implicitContentWidth + leftPadding + rightPadding,
                             _titleWidth.advanceWidth + 2 * Theme.s7, implicitFooterWidth),
                    parent ? parent.width - 2 * Theme.s7 : Theme.dialogMaxWidth)
    // the template leaves the size to the style: title, body and buttons
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding
                             + (implicitHeaderHeight > 0 ? implicitHeaderHeight + spacing : 0)
                             + (implicitFooterHeight > 0 ? implicitFooterHeight + spacing : 0))
    padding: Theme.s7
    topPadding: title !== "" ? Theme.s4 : Theme.s7
    bottomPadding: Theme.s7

    T.Overlay.modal: Rectangle {
        color: Theme.smoke
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easingFast }
        NumberAnimation { property: "scale"; from: 1.05; to: 1; duration: Theme.durationFast; easing.type: Theme.easingFast }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durationFaster }
        NumberAnimation { property: "scale"; from: 1; to: 1.05; duration: Theme.durationFaster; easing.type: Theme.easingExit }
    }

    background: Item {
        Glow {
            radius: Theme.radiusOverlay
            spread: 28
            offsetY: 10
            color: Qt.alpha(Theme.shadow, Theme.night ? 0.5 : 0.22)
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusOverlay
            color: Theme.flyout
            border.width: 1
            border.color: Theme.strokeSurface
        }
    }

    header: Text {
        text: dialog.title
        visible: text !== ""
        leftPadding: Theme.s7
        rightPadding: Theme.s7
        topPadding: Theme.s7
        font: Theme.subtitle
        horizontalAlignment: Text.AlignLeft
        color: Theme.text
        wrapMode: Text.Wrap
        lineHeightMode: Text.FixedHeight
        lineHeight: Theme.subtitleLineHeight
        Accessible.role: Accessible.Heading
        Accessible.name: text
    }

    contentItem: ColumnLayout {
        id: bodyColumn
        spacing: Theme.s4
        // keys reach the dialog here: Enter answers with the accent button
        focus: true
        Keys.onReturnPressed: dialog._acceptByKey()
        Keys.onEnterPressed: dialog._acceptByKey()
        Text {
            Layout.fillWidth: true
            visible: dialog.text !== ""
            text: dialog.text
            font: Theme.body
            horizontalAlignment: Text.AlignLeft
            color: Theme.text
            wrapMode: Text.Wrap
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.bodyLineHeight
            textFormat: Text.StyledText
            linkColor: Theme.accentText
            onLinkActivated: (link) => Qt.openUrlExternally(link)
        }
    }

    // the buttons sit on a band of their own, like Windows'
    footer: Item {
        implicitHeight: buttons.implicitHeight + 2 * Theme.s7
        Item {
            anchors.fill: parent
            clip: true
            Rectangle {
                width: parent.width
                height: parent.height + radius
                y: -radius
                radius: Theme.radiusOverlay
                color: Theme.flyoutFooter
                border.width: 1
                border.color: Theme.strokeSurface
            }
        }
        RowLayout {
            id: buttons
            anchors.fill: parent
            anchors.margins: Theme.s7
            spacing: Theme.s3
            // Windows shares the width between the buttons
            Button {
                id: primaryButton
                visible: dialog.primaryText !== ""
                text: dialog.primaryText
                accent: true
                enabled: dialog.primaryEnabled
                Layout.fillWidth: true
                onClicked: dialog.accept()
            }
            Button {
                visible: dialog.secondaryText !== ""
                text: dialog.secondaryText
                Layout.fillWidth: true
                onClicked: {
                    dialog.secondaryClicked()
                    dialog.close()
                }
            }
            Button {
                visible: dialog.closeText !== ""
                text: dialog.closeText
                Layout.fillWidth: true
                onClicked: dialog.reject()
            }
        }
    }

    // Enter answers with the accent button, as it would in Windows
    function _acceptByKey() {
        if (primaryButton.visible && primaryButton.enabled)
            dialog.accept()
    }
}
