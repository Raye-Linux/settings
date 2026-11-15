// SPDX-License-Identifier: GPL-3.0-or-later
// The top of Device security: the shield, then "Your device is protected"
// or "Action recommended" and what it is about, like the banner of
// Windows Security, on a soft glow of the accent (or of caution).
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.raye.settings

Item {
    id: banner

    // what Device security asks the user to look at (SystemInfo.actionsNeeded)
    property var actions: []
    readonly property bool protectedNow: actions.length === 0

    Layout.fillWidth: true
    implicitHeight: Math.max(112, row.implicitHeight + 2 * Theme.s7)
    Accessible.role: Accessible.Grouping
    Accessible.name: title.text
    Accessible.description: detail.text

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusOverlay
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder
        clip: true
        // a wash of colour from the shield's side
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop {
                    position: 0
                    color: Qt.alpha(banner.protectedNow ? Theme.accent : Theme.statusColor("caution"), Theme.night ? 0.22 : 0.12)
                }
                GradientStop { position: 0.7; color: "transparent" }
            }
        }
    }

    RowLayout {
        id: row
        anchors.fill: parent
        anchors.leftMargin: Theme.s7
        anchors.rightMargin: Theme.s7
        spacing: Theme.s6

        Item {
            Layout.preferredWidth: 56
            Layout.preferredHeight: 56
            Layout.alignment: Qt.AlignVCenter
            Kirigami.Icon {
                anchors.fill: parent
                source: "org.raye.settings-device-security"
                fallback: "security-high"
            }
            StatusGlyph {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: -2
                size: 20
                status: banner.protectedNow ? "success" : "caution"
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.s1
            Text {
                id: title
                Layout.fillWidth: true
                text: banner.protectedNow ? i18nc("@title", "Your device is protected")
                                          : i18nc("@title", "Action recommended")
                font: Theme.subtitle
                color: Theme.text
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignLeft
                Accessible.ignored: true
            }
            Text {
                id: detail
                Layout.fillWidth: true
                text: banner.protectedNow
                      ? i18nc("@info", "Secure Boot, disk encryption and the firewall are looked at each time you open this page.")
                      : i18ncp("@info", "One thing below needs your attention.", "%1 things below need your attention.",
                               banner.actions.length)
                font: Theme.body
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignLeft
                Accessible.ignored: true
            }
        }
    }
}
