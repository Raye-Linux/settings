// SPDX-License-Identifier: GPL-3.0-or-later
// A small card at the top of the System page (Raye Update, Storage):
// glyph, title, one line, an optional bar; the whole card opens its page.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

T.AbstractButton {
    id: card

    property string iconName
    property string title
    // the line under the title is the button's own text
    property string searchId
    // 0 to 1 for a bar under the text, below 0 for none
    property real progress: -1

    implicitHeight: Math.max(72, content.implicitHeight + 2 * Theme.cardPadding)
    implicitWidth: 260
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.description: text

    Keys.onReturnPressed: card.click()
    Keys.onEnterPressed: card.click()

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusOverlay
            color: card.down ? Theme.cardPressed : card.hovered ? Theme.cardHover : Theme.card
            border.width: 1
            border.color: Theme.cardBorder
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
        FocusRing { shown: card.visualFocus; radius: Theme.radiusOverlay }
    }

    contentItem: Item {
        RowLayout {
            id: content
            anchors.fill: parent
            anchors.margins: Theme.cardPadding
            spacing: Theme.s4

            Kirigami.Icon {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                Layout.alignment: Qt.AlignVCenter
                source: card.iconName
                Accessible.ignored: true
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.s1
                Text {
                    Layout.fillWidth: true
                    font: Theme.bodyStrong
                    color: Theme.text
                    elide: Text.ElideRight
                    text: card.title
                }
                Text {
                    Layout.fillWidth: true
                    visible: text !== ""
                    font: Theme.caption
                    color: Theme.textSecondary
                    elide: Text.ElideRight
                    text: card.text
                }
                ProgressBar {
                    Layout.fillWidth: true
                    Layout.topMargin: Theme.s1
                    visible: card.progress >= 0
                    from: 0
                    to: 1
                    value: Math.max(0, card.progress)
                    status: card.progress > 0.9 ? "caution" : ""
                }
            }
        }
    }
}
