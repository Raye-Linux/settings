// SPDX-License-Identifier: GPL-3.0-or-later
// The band at the top of Network & internet: what this PC is connected
// to, in big, with its status and address, "Properties" and the data
// it has moved.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings
import "networktext.js" as Net

Rectangle {
    id: banner

    required property var network

    implicitHeight: Math.max(120, content.implicitHeight + 2 * Theme.s6)
    radius: Theme.radiusOverlay
    color: Theme.layer
    border.width: 1
    border.color: Theme.cardBorder
    Accessible.role: Accessible.Grouping
    Accessible.name: (network.connected ? network.primaryName + ", " : "") + network.statusText

    // the glow behind the glyph, Raye's mark on every banner
    Item {
        x: banner.LayoutMirroring.enabled ? banner.width - width - Theme.s6 : Theme.s6
        anchors.verticalCenter: parent.verticalCenter
        width: 80
        height: 80
        Glow {
            radius: 40
            spread: 14
            visible: banner.network.connected
        }
        Rectangle {
            anchors.fill: parent
            radius: 40
            gradient: Gradient {
                GradientStop { position: 0; color: Qt.alpha(Theme.accent, banner.network.connected ? 1 : 0.25) }
                GradientStop { position: 1; color: Qt.alpha(Theme.accentGlowEnd, banner.network.connected ? 0.85 : 0.18) }
            }
        }
        NetworkGlyph {
            anchors.centerIn: parent
            size: 40
            kind: !banner.network.connected ? (banner.network.hasWifi ? "none" : "ethernet")
                : banner.network.primaryType === "wifi" ? "wifi"
                : banner.network.primaryType === "vpn" ? "vpn" : "ethernet"
            bars: Math.max(1, Math.round(banner.network.signalStrength / 25))
            color: banner.network.connected ? Theme.textOnAccent : Theme.textSecondary
            dimColor: Qt.alpha(color, 0.35)
        }
    }

    RowLayout {
        id: content
        anchors.fill: parent
        anchors.leftMargin: Theme.s6 + 80 + Theme.s6
        anchors.rightMargin: Theme.s6
        spacing: Theme.s6

        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: 2
            Text {
                Layout.fillWidth: true
                text: banner.network.connected ? banner.network.primaryName : i18nc("@info network status", "Not connected")
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font: Theme.subtitle
                color: Theme.text
                horizontalAlignment: Text.AlignLeft
            }
            Text {
                Layout.fillWidth: true
                text: banner.network.connected ? banner.network.statusText
                    : banner.network.wording.offline
                textFormat: Text.PlainText
                elide: Text.ElideRight
                font: Theme.body
                color: banner.network.portal ? Theme.caution : Theme.textSecondary
                horizontalAlignment: Text.AlignLeft
            }
            Text {
                Layout.fillWidth: true
                visible: banner.network.ipv4Address !== ""
                text: i18nc("@info", "IPv4 address: %1", banner.network.ipv4Address)
                textFormat: Text.PlainText
                font: Theme.caption
                color: Theme.textTertiary
                horizontalAlignment: Text.AlignLeft
            }
        }

        ColumnLayout {
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.s2
            visible: banner.network.connected
            BannerButton {
                glyph: "info"
                text: i18nc("@action:button", "Properties")
                detail: banner.network.primaryType === "wifi" ? i18nc("@info", "Wi-Fi") : banner.network.primaryType === "ethernet"
                        ? i18nc("@info", "Ethernet") : ""
                onClicked: Router.navigate(banner.network.primaryPage + (banner.network.primaryType === "wifi" ? "#connection-properties" : ""))
            }
            BannerButton {
                glyph: "dot"
                visible: banner.network.dataUsed !== ""
                interactive: false
                text: i18nc("@label", "Data used")
                detail: i18nc("@info data moved since the connection started", "%1, this connection", banner.network.dataUsed)
            }
        }
    }

    component BannerButton: T.AbstractButton {
        id: button
        property string glyph
        property string detail
        property bool interactive: true
        hoverEnabled: interactive
        focusPolicy: interactive ? Qt.StrongFocus : Qt.NoFocus
        implicitWidth: 220
        implicitHeight: 48
        Accessible.role: interactive ? Accessible.Button : Accessible.StaticText
        Accessible.name: text + (detail !== "" ? ", " + detail : "")
        background: Rectangle {
            radius: Theme.radiusControl
            color: !button.interactive ? "transparent"
                 : button.down ? Theme.subtlePressed : button.hovered ? Theme.subtleHover : "transparent"
            FocusRing { shown: button.visualFocus; radius: Theme.radiusControl }
        }
        contentItem: RowLayout {
            spacing: Theme.s4
            Glyph {
                Layout.leftMargin: Theme.s3
                name: button.glyph
                size: 16
                color: Theme.accentText
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    Layout.fillWidth: true
                    text: button.text
                    font: Theme.body
                    color: Theme.text
                    horizontalAlignment: Text.AlignLeft
                }
                Text {
                    Layout.fillWidth: true
                    visible: button.detail !== ""
                    text: button.detail
                    textFormat: Text.PlainText
                    font: Theme.caption
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                    horizontalAlignment: Text.AlignLeft
                }
            }
            Glyph {
                visible: button.interactive
                Layout.rightMargin: Theme.s3
                name: "chevron-right"
                size: Theme.actionIconSize
                color: Theme.textSecondary
            }
        }
    }
}
