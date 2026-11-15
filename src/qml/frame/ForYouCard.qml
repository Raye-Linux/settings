// SPDX-License-Identifier: GPL-3.0-or-later
// A card of the "For you" row under the tiles (Your device, Bluetooth
// devices, Make it yours): a title, a "…" button whose menu hides the
// card for good (raye-settingsrc [Home] Hidden=), and the content.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

Item {
    id: card

    property string title
    // the id the card is hidden by, also its search anchor
    property string cardId
    property string searchId: cardId
    default property alias content: body.data

    signal hideRequested()

    function flash() { flashAnimation.restart() }

    Layout.fillWidth: true
    implicitWidth: 360
    implicitHeight: column.implicitHeight + 2 * Theme.s5

    Accessible.role: Accessible.Grouping
    Accessible.name: title

    Rectangle {
        anchors.fill: parent
        radius: Theme.radiusOverlay
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder
    }
    Rectangle {
        id: flashFrame
        anchors.fill: parent
        radius: Theme.radiusOverlay
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: 0
        SequentialAnimation {
            id: flashAnimation
            NumberAnimation { target: flashFrame; property: "opacity"; to: 1; duration: Theme.durationFast }
            PauseAnimation { duration: Math.max(0, Theme.durationFlash - Theme.durationFast - Theme.durationNormal) }
            NumberAnimation { target: flashFrame; property: "opacity"; to: 0; duration: Theme.durationNormal }
        }
    }

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: Theme.s5
        spacing: Theme.s4

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: card.title
                font: Theme.bodyStrong
                color: Theme.text
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignLeft
                Accessible.role: Accessible.Heading
                Accessible.name: text
            }
            T.AbstractButton {
                id: more
                implicitWidth: 32
                implicitHeight: 28
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.role: Accessible.ButtonMenu
                Accessible.name: i18nc("@action:button menu of a card", "More options for %1", card.title)
                onClicked: menu.open()
                Keys.onReturnPressed: menu.open()
                background: Item {
                    Rectangle {
                        anchors.fill: parent
                        radius: Theme.radiusControl
                        color: more.down || menu.visible ? Theme.subtlePressed : more.hovered ? Theme.subtleHover : "transparent"
                    }
                    FocusRing { shown: more.visualFocus }
                }
                contentItem: Item {
                    Glyph { anchors.centerIn: parent; name: "more"; size: 16; color: Theme.textSecondary }
                }

                Flyout {
                    id: menu
                    y: more.height + Theme.s1
                    x: more.width - width
                    padding: Theme.s1
                    focus: true
                    implicitWidth: Math.max(180, hideItem.implicitWidth + 2 * padding)
                    implicitHeight: hideItem.implicitHeight + 2 * padding
                    contentItem: T.AbstractButton {
                        id: hideItem
                        implicitWidth: label.implicitWidth + 2 * Theme.s4
                        implicitHeight: Theme.popupItemHeight + Theme.s1
                        hoverEnabled: true
                        focus: true
                        Accessible.role: Accessible.MenuItem
                        Accessible.name: label.text
                        Keys.onReturnPressed: clicked()
                        onClicked: {
                            menu.close()
                            card.hideRequested()
                        }
                        background: Rectangle {
                            radius: Theme.radiusControl
                            color: hideItem.down ? Theme.subtlePressed : hideItem.hovered || hideItem.activeFocus ? Theme.subtleHover : "transparent"
                        }
                        contentItem: Text {
                            id: label
                            leftPadding: Theme.s4
                            text: i18nc("@action:inmenu", "Hide this card")
                            font: Theme.body
                            color: Theme.text
                            verticalAlignment: Text.AlignVCenter
                            Accessible.ignored: true
                        }
                    }
                }
            }
        }

        ColumnLayout {
            id: body
            Layout.fillWidth: true
            spacing: Theme.s3
        }
    }
}
