// SPDX-License-Identifier: GPL-3.0-or-later
// "Network profile type": public or private. On a public network the PC
// stays hidden; on a private one the devices of that network may find it
// and connect (sharing, phones, printers, games). raye-network-profile
// turns the choice into firewall rules.
//
//   NetworkProfileChoice { network: netStatus; uuid: "…"; inExpander: true }
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

Item {
    id: choice

    required property var network
    property string uuid
    // a row of an Expander: a divider on top, text lined up with the header
    property bool inExpander: false
    property string profile: "private"

    Layout.fillWidth: true
    implicitWidth: 480
    implicitHeight: column.implicitHeight + 2 * Theme.s4

    function reload() {
        if (uuid !== "")
            profile = network.networkProfile(uuid)
    }
    function choose(id) {
        if (id === profile || uuid === "")
            return
        network.setNetworkProfile(uuid, id)
        reload()
    }
    Component.onCompleted: reload()
    onUuidChanged: reload()
    Connections {
        target: choice.network
        function onChanged() { choice.reload() }
    }

    Rectangle {
        visible: choice.inExpander
        width: parent.width
        height: 1
        color: Theme.divider
    }

    ColumnLayout {
        id: column
        x: choice.inExpander ? Theme.cardTextLeft - Theme.s2 - 20 - Theme.s3 : Theme.cardPadding
        y: Theme.s4
        width: parent.width - x - Theme.cardPadding
        spacing: Theme.s2

        Text {
            Layout.fillWidth: true
            // over the options' titles, past the radio marks
            Layout.leftMargin: Theme.s2 + 20 + Theme.s3
            text: i18nc("@label", "Network profile type")
            font: Theme.body
            color: choice.enabled ? Theme.text : Theme.textDisabled
            wrapMode: Text.Wrap
            Accessible.ignored: true
        }

        Repeater {
            model: [
                { id: "public",
                  title: i18nc("@option:radio", "Public network"),
                  text: i18nc("@info", "This PC stays hidden on the network: no sharing, and no answer to other devices. Best for cafés, hotels and airports.") },
                { id: "private",
                  title: i18nc("@option:radio", "Private network"),
                  text: i18nc("@info", "Devices on this network can find this PC and connect to it, for sharing, phones, printers and games. Use it at home or with people you trust.") }
            ]
            delegate: T.AbstractButton {
                id: option
                required property var modelData
                Layout.fillWidth: true
                implicitWidth: implicitContentWidth + leftPadding + rightPadding
                implicitHeight: implicitContentHeight + topPadding + bottomPadding
                checkable: true
                checked: choice.profile === modelData.id
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                padding: Theme.s2
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.title
                Accessible.description: modelData.text
                Accessible.checked: checked
                onClicked: {
                    choice.choose(modelData.id)
                    checked = Qt.binding(() => choice.profile === option.modelData.id)
                }
                Keys.onSpacePressed: clicked()
                background: Rectangle {
                    radius: Theme.radiusControl
                    color: option.down ? Theme.subtlePressed : option.hovered ? Theme.subtleHover : "transparent"
                    FocusRing { shown: option.visualFocus }
                }
                contentItem: RowLayout {
                    spacing: Theme.s3
                    RadioIndicator {
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 1
                        enabled: option.enabled
                        checked: option.checked
                        hovered: option.hovered
                        pressed: option.down
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: option.modelData.title
                            textFormat: Text.PlainText
                            font: Theme.body
                            color: option.enabled ? Theme.text : Theme.textDisabled
                            wrapMode: Text.Wrap
                        }
                        Text {
                            Layout.fillWidth: true
                            text: option.modelData.text
                            textFormat: Text.PlainText
                            font: Theme.caption
                            color: option.enabled ? Theme.textSecondary : Theme.textDisabled
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
