// SPDX-License-Identifier: GPL-3.0-or-later
// A value to read, not to change ("Device name", "Processor"), with a
// Copy button. The button says "Copied" for a moment after a click.
import QtQuick

Card {
    id: card

    property string value
    property bool copyable: true

    signal copied()

    function copy() {
        _clipboard.text = card.value
        _clipboard.selectAll()
        _clipboard.copy()
        _clipboard.text = ""
        _copiedTimer.restart()
        card.copied()
    }

    Text {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        text: card.value
        font: Theme.body
        color: card.enabled ? Theme.textSecondary : Theme.textDisabled
        textFormat: Text.PlainText
        Accessible.role: Accessible.StaticText
        Accessible.name: card.title + ": " + card.value
    }
    Button {
        id: copyButton
        visible: card.copyable
        enabled: card.enabled && card.value !== ""
        text: _copiedTimer.running ? i18nc("@action:button the value is on the clipboard", "Copied")
                                  : i18nc("@action:button", "Copy")
        Accessible.description: card.title
        onClicked: card.copy()
    }

    // the clipboard is reached through an off-screen text editor
    readonly property TextEdit _clipboard: TextEdit {
        visible: false
        textFormat: TextEdit.PlainText
    }
    readonly property Timer _copiedTimer: Timer {
        interval: 2000
    }
}
