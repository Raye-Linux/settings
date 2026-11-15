// SPDX-License-Identifier: GPL-3.0-or-later
// One network in "Show available networks": signal, name, how it is
// secured. Selected, it opens in place as in Windows: "Connect
// automatically" and Connect, then the password under it, or
// Disconnect for the network in use.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

T.AbstractButton {
    id: row

    required property string ssid
    required property int bars
    required property bool secured
    required property bool canJoin
    required property bool connected
    required property bool joining
    required property bool known
    required property string security

    // the WifiNetworks model and the page's NetworkStatus
    required property var networks
    required property var network
    // the name of the open row, shared by all the rows of the list
    property string selected
    signal select(string ssid)

    readonly property bool open: selected === ssid
    property string step: "start" // start, password
    property string error: ""

    Layout.fillWidth: true
    implicitHeight: Math.max(Theme.expanderRowHeight + 4, head.implicitHeight + 2 * Theme.s4)
                    + (open ? details.implicitHeight + Theme.s4 : 0)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: 0
    Accessible.role: Accessible.Button
    Accessible.name: ssid
    Accessible.description: status.text

    // a click on the open part (around the field, the buttons) keeps it open
    onClicked: if (!open || pressY < Theme.expanderRowHeight + 4) select(open ? "" : ssid)
    onOpenChanged: {
        if (!open) {
            step = "start"
            error = ""
            password.clear()
        }
    }

    function connectNow() {
        error = ""
        if (step === "start" && networks.needsPassword(ssid)) {
            step = "password"
            Qt.callLater(password.forceActiveFocus)
            return
        }
        const problem = networks.join(ssid, step === "password" ? password.text : "", automatic.checked)
        if (problem !== "")
            error = problem
        else
            password.clear()
    }

    Connections {
        target: row.networks
        function onJoinFailed(name, wrongPassword, message) {
            if (name !== row.ssid)
                return
            row.select(row.ssid)
            row.error = message
            if (wrongPassword || row.secured) {
                row.step = "password"
                Qt.callLater(password.forceActiveFocus)
            }
        }
        function onJoined(name) {
            if (name === row.ssid && row.open)
                row.select("")
        }
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            anchors.margins: 1
            color: row.open ? Theme.subtleSelected
                 : row.down ? Theme.subtlePressed : row.hovered ? Theme.subtleHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
        Rectangle {
            width: parent.width
            height: 1
            color: Theme.divider
        }
        FocusRing { anchors.margins: 1; shown: row.visualFocus; radius: 2 }
    }

    contentItem: Item {
        NetworkGlyph {
            id: glyph
            x: row.mirrored ? parent.width - Theme.cardIconLeft - width : Theme.cardIconLeft
            y: Math.round((Theme.expanderRowHeight + 4 - height) / 2)
            size: Theme.iconSize
            bars: row.bars
            locked: row.secured
            color: row.connected ? Theme.accentText : Theme.text
        }
        Column {
            id: head
            x: row.mirrored ? parent.width - Theme.cardTextLeft - width : Theme.cardTextLeft
            y: Math.round((Theme.expanderRowHeight + 4 - height) / 2)
            width: parent.width - Theme.cardTextLeft - Theme.cardPadding
            Text {
                width: parent.width
                text: row.ssid
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font: row.connected ? Theme.bodyStrong : Theme.body
                color: Theme.text
                horizontalAlignment: Text.AlignLeft
            }
            Text {
                id: status
                width: parent.width
                textFormat: Text.PlainText
                text: row.joining ? i18nc("@info Wi-Fi network", "Connecting…")
                    : row.connected ? (row.secured ? i18nc("@info Wi-Fi network", "Connected, secured")
                                                   : i18nc("@info Wi-Fi network", "Connected"))
                    : !row.canJoin ? i18nc("@info Wi-Fi network", "Secured, needs a sign-in")
                    : row.secured ? i18nc("@info Wi-Fi network", "Secured")
                    : i18nc("@info Wi-Fi network", "Open")
                font: Theme.caption
                color: row.connected || row.joining ? Theme.accentText : Theme.textSecondary
                horizontalAlignment: Text.AlignLeft
            }
        }

        ColumnLayout {
            id: details
            visible: row.open
            x: head.x
            y: Theme.expanderRowHeight + 4
            width: head.width
            spacing: Theme.s3

            // the network in use
            RowLayout {
                visible: row.connected
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: i18nc("@action:button", "Properties")
                    onClicked: Router.navigate("network/wifi#connection-properties")
                }
                Button {
                    text: i18nc("@action:button", "Disconnect")
                    onClicked: {
                        row.networks.disconnectFrom(row.ssid)
                        row.select("")
                    }
                }
            }

            // 802.1X and the like: the KDE editor
            Text {
                visible: !row.connected && !row.canJoin
                Layout.fillWidth: true
                text: i18nc("@info", "This network needs a sign-in that Raye can't set up here.")
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font: Theme.caption
                color: Theme.textSecondary
            }
            RowLayout {
                visible: !row.connected && !row.canJoin
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: i18nc("@action:button", "More options")
                    onClicked: row.network.openMoreOptions("")
                }
            }

            // the password, asked in place
            Text {
                visible: !row.connected && row.canJoin && row.step === "password"
                Layout.fillWidth: true
                text: i18nc("@label", "Enter the network security key")
                textFormat: Text.PlainText
                font: Theme.body
                color: Theme.text
            }
            PasswordField {
                id: password
                visible: !row.connected && row.canJoin && row.step === "password"
                Layout.fillWidth: true
                placeholderText: i18nc("@info:placeholder", "Network security key")
                enabled: !row.joining
                onAccepted: row.connectNow()
                Keys.onEscapePressed: row.select("")
            }
            Text {
                visible: row.error !== ""
                Layout.fillWidth: true
                text: row.error
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                font: Theme.caption
                color: Theme.critical
            }
            RowLayout {
                visible: !row.connected && row.canJoin
                Layout.fillWidth: true
                spacing: Theme.s3
                NetworkCheckBox {
                    id: automatic
                    visible: row.step === "start"
                    checked: true
                    text: i18nc("@option:check", "Connect automatically")
                }
                Item { Layout.fillWidth: true }
                Button {
                    accent: true
                    enabled: !row.joining && (row.step === "start" || password.length > 0)
                    text: row.step === "password" ? i18nc("@action:button", "Next") : i18nc("@action:button", "Connect")
                    onClicked: row.connectNow()
                }
                Button {
                    visible: row.step === "password"
                    text: i18nc("@action:button", "Cancel")
                    onClicked: row.select("")
                }
            }
        }
    }
}
