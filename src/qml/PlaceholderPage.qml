// SPDX-License-Identifier: GPL-3.0-or-later
// Shown where a page is named in the layout but its file is not built
// yet. It says so plainly instead of leaving an empty frame.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.raye.settings

Item {
    id: page

    property string pageName: ""
    property string summary: ""
    property string iconName: "preferences-system"

    readonly property color accent: Theme.accent
    readonly property color glow: Theme.accent

    Accessible.role: Accessible.StaticText
    Accessible.name: page.pageName
    Accessible.description: i18n("This page is on its way.")

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.contentPadding, 420)
        spacing: Theme.s2

        Item {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 96
            implicitHeight: 96

            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusOverlay * 2
                gradient: Gradient {
                    GradientStop { position: 0.0; color: Qt.rgba(page.accent.r, page.accent.g, page.accent.b, 0.22) }
                    GradientStop { position: 1.0; color: Qt.rgba(page.glow.r, page.glow.g, page.glow.b, 0.10) }
                }
                border.width: 1
                border.color: Qt.rgba(page.glow.r, page.glow.g, page.glow.b, 0.35)
            }

            Kirigami.Icon {
                anchors.centerIn: parent
                width: 48
                height: 48
                // Raye's page glyphs are drawn in colour: show them as they are
                source: page.iconName
            }
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s3
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font: Theme.subtitle
            color: Theme.text
            text: page.pageName
            visible: text.length > 0
        }

        Text {
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font: Theme.body
            color: Theme.textSecondary
            text: page.summary
            visible: text.length > 0
        }

        Text {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s2
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            font: Theme.caption
            color: Theme.textTertiary
            text: i18n("This page is on its way. It will be here in the next update.")
        }
    }
}
