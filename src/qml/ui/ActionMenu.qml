// SPDX-License-Identifier: GPL-3.0-or-later
// A "..." menu: a short list of actions on a flyout, opened under its
// button. It opens with nothing picked; the arrows walk the list (and
// wrap), Enter or a click picks a row.
//
//   ActionMenu {
//       actions: [{ id: "open", text: i18n("Open") },
//                 { id: "remove", text: i18n("Remove"), enabled: false }]
//       onTriggered: (id) => ...
//   }
import QtQuick
import QtQuick.Templates as T

Flyout {
    id: menu

    // [{ id, text, enabled (optional) }]
    property var actions: []
    signal triggered(string id)

    focus: true
    // a row picked the last time keeps its focus otherwise
    onOpened: contentItem.forceActiveFocus()
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    implicitWidth: Math.max(200, _widest + 24 + leftPadding + rightPadding)

    readonly property real _widest: {
        let widest = 0
        for (const action of actions)
            widest = Math.max(widest, metrics.advanceWidth(action.text))
        return Math.ceil(widest)
    }
    FontMetrics { id: metrics; font: Theme.body }

    contentItem: Column {
        focus: true
        Keys.onDownPressed: menu._step(-1, 1)
        Keys.onUpPressed: menu._step(0, -1)

        Repeater {
            id: rows
            model: menu.actions
            delegate: T.ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: menu.availableWidth
                implicitHeight: Theme.popupItemHeight
                leftPadding: 12
                rightPadding: 12
                hoverEnabled: true
                enabled: modelData.enabled !== false
                Accessible.role: Accessible.MenuItem
                Accessible.name: modelData.text
                contentItem: Text {
                    text: row.modelData.text
                    font: Theme.body
                    color: row.enabled ? (row.down ? Theme.textSecondary : Theme.text) : Theme.textDisabled
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    Accessible.ignored: true
                }
                background: Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 4
                    color: !row.enabled ? "transparent"
                         : row.down ? Theme.subtlePressed
                         : row.hovered || row.visualFocus ? Theme.subtleHover : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
                }
                Keys.onDownPressed: menu._step(index, 1)
                Keys.onUpPressed: menu._step(index, -1)
                Keys.onReturnPressed: clicked()
                Keys.onEnterPressed: clicked()
                onClicked: {
                    menu.close()
                    menu.triggered(modelData.id)
                }
            }
        }
    }

    // the next row that can be picked, round the end of the list
    function _step(from, by) {
        const count = rows.count
        for (let i = 1; i <= count; ++i) {
            const row = rows.itemAt((from + by * i + count * i) % count)
            if (row && row.enabled) {
                row.forceActiveFocus(by > 0 ? Qt.TabFocusReason : Qt.BacktabFocusReason)
                return
            }
        }
    }
}
