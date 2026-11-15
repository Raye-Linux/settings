// SPDX-License-Identifier: GPL-3.0-or-later
// The launch screen: who is signed in with the Raye account and Raye
// Update cards beside it, the "Find a setting" pill, the twelve tiles,
// and the "For you" cards. Typing anywhere on it lands in the search.
// The content is at most 1200 px wide, centred.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

FocusScope {
    id: home

    // [{ target, name, summary, icon }] from the layout
    property var tiles: typeof categories !== "undefined" ? categories.tiles : []
    // Enter on the search text: the results page
    signal searchRequested(string text)

    readonly property real margin: width >= 1200 ? 40 : 24
    readonly property real contentWidth: Math.min(1200, width - 2 * margin)
    readonly property int tileColumns: width >= 1200 ? 4 : width >= 900 ? 3 : width >= 600 ? 2 : 1

    Accessible.role: Accessible.Pane
    Accessible.name: i18nc("@title the launch screen of Settings", "Home")

    function focusSearch(text) { search.startTyping(text) }

    function scrollToAnchor(anchor) {
        const target = anchor === "raye-account" ? accountCard
                     : anchor === "update" ? updateCard
                     : anchor === "your-device" || anchor === "make-it-yours" || anchor === "bluetooth-devices"
                       ? findCard(forYou, anchor) : null
        if (!target)
            return false
        const y = target.mapToItem(column, 0, 0).y
        flick.contentY = Math.max(0, Math.min(y - Theme.s7, flick.contentHeight - flick.height))
        target.flash()
        return true
    }

    function findCard(item, id) {
        for (let i = 0; i < item.children.length; ++i) {
            const child = item.children[i]
            if (child.cardId === id)
                return child
            if (child.item && child.item.cardId === id)
                return child.item
        }
        return null
    }

    // Arrow keys in the tile grid: left/right along the reading order,
    // up/down by a row; at the top, Up goes back to the search.
    function moveTile(index, dx, dy) {
        let next = index + dx + dy * tileColumns
        if (dy < 0 && next < 0) {
            search.forceActiveFocus()
            return
        }
        next = Math.max(0, Math.min(tileRepeater.count - 1, next))
        const item = tileRepeater.itemAt(next)
        if (item)
            item.forceActiveFocus(Qt.TabFocusReason)
    }

    Connections {
        target: Router
        function onAnchorRequested(anchor) {
            if (Router.atHome)
                Qt.callLater(home.scrollToAnchor, anchor)
        }
    }

    // a printable key that nothing else took starts a search
    Keys.onPressed: (event) => {
        if (event.text.length === 1 && event.text.trim().length === 1
                && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))) {
            search.startTyping(event.text)
            event.accepted = true
        }
    }

    Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 2 * Theme.s7
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        T.ScrollBar.vertical: ThinScrollBar {}

        ColumnLayout {
            id: column
            x: Math.round((home.width - width) / 2)
            y: Theme.s7
            width: home.contentWidth
            spacing: 0

            // --- hero -----------------------------------------------------
            GridLayout {
                id: hero
                Layout.fillWidth: true
                readonly property bool wide: width >= 860
                columns: wide ? 3 : 1
                columnSpacing: Theme.s4
                rowSpacing: Theme.s5

                UserCard {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    mode: "hero"
                }
                // side by side, or one under the other on a phone-narrow window
                GridLayout {
                    Layout.columnSpan: hero.wide ? 2 : 1
                    Layout.fillWidth: !hero.wide
                    columns: home.width >= 680 ? 2 : 1
                    columnSpacing: Theme.s4
                    rowSpacing: Theme.s4
                    RayeAccountCard {
                        id: accountCard
                        Layout.fillHeight: true
                        Layout.fillWidth: !hero.wide
                        Layout.preferredWidth: hero.wide ? 300 : -1
                    }
                    UpdateCard {
                        id: updateCard
                        Layout.fillHeight: true
                        Layout.fillWidth: !hero.wide
                        Layout.preferredWidth: hero.wide ? 300 : -1
                        onRestartRequested: restartDialog.open()
                    }
                }
            }

            // --- search ---------------------------------------------------
            SearchField {
                id: search
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Theme.s8
                Layout.preferredWidth: Math.min(640, column.width)
                focus: true
                onShowAllResults: (text) => home.searchRequested(text)
                KeyNavigation.down: tileRepeater.count > 0 ? tileRepeater.itemAt(0) : null
            }

            // --- tiles ----------------------------------------------------
            GridLayout {
                id: grid
                Layout.fillWidth: true
                Layout.topMargin: Theme.s8
                columns: home.tileColumns
                columnSpacing: Theme.s4
                rowSpacing: Theme.s4
                readonly property real cellWidth: (width - (columns - 1) * columnSpacing) / columns

                Repeater {
                    id: tileRepeater
                    model: home.tiles
                    delegate: CategoryTile {
                        required property var modelData
                        required property int index
                        Layout.preferredWidth: grid.cellWidth
                        Layout.fillWidth: true
                        entry: modelData
                        tileIndex: index
                        onMoveRequested: (i, dx, dy) => home.moveTile(i, dx, dy)
                    }
                }
            }

            // --- for you --------------------------------------------------
            ForYouCards {
                id: forYou
                Layout.fillWidth: true
                Layout.topMargin: Theme.s8
                columnCount: home.width >= 1000 ? 3 : home.width >= 680 ? 2 : 1
                visible: shownCount > 0
            }
        }
    }

    ContentDialog {
        id: restartDialog
        parent: T.Overlay.overlay
        anchors.centerIn: parent
        title: i18nc("@title:window", "Restart now?")
        text: typeof updates !== "undefined" && updates.state !== "ready"
              ? i18n("Save your work first. Your PC restarts to finish installing updates.")
              : i18n("Save your work first. Raye installs the updates while it restarts.")
        primaryText: i18nc("@action:button", "Restart now")
        closeText: i18nc("@action:button", "Not now")
        onAccepted: if (typeof updates !== "undefined") updates.restartAndInstall()
    }

    ContentDialog {
        id: restartError
        title: i18nc("@title:window", "Couldn't restart")
        closeText: i18nc("@action:button", "Close")
    }

    Connections {
        target: typeof updates !== "undefined" ? updates : null
        function onRestartFailed(reason) {
            restartError.text = reason === "cancelled" ? i18n("The updates weren't set up because the password wasn't entered. Try again.")
                              : reason === "failed" ? i18n("The updates couldn't be set up to install. Your PC wasn't restarted. Try again later.")
                              : i18n("Your PC couldn't be restarted. Save your work and restart it from Start.")
            restartError.open()
        }
    }
}
