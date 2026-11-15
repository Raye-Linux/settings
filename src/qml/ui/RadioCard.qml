// SPDX-License-Identifier: GPL-3.0-or-later
// A big choice among a few (theme, power mode, output device): a card
// with an optional picture on top, a radio mark, a title and a line.
// Cards side by side in the same parent are exclusive; put them in a
// ButtonGroup when they are not.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

T.AbstractButton {
    id: card

    property string title
    property string description
    property string iconName
    property string searchId
    // for the gallery: "hover", "pressed" or "focus" shown at rest
    property string previewState: ""
    // a picture over the text (a theme preview); empty: text only
    default property alias preview: previewArea.data

    readonly property bool showHover: enabled && (hovered || previewState === "hover")
    readonly property bool showPressed: enabled && (down || previewState === "pressed")
    readonly property bool hasPreview: previewArea.children.length > 0

    function flash() { flashAnimation.restart() }

    checkable: true
    autoExclusive: true
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    padding: Theme.s4
    implicitWidth: 220
    implicitHeight: (hasPreview ? previewArea.implicitHeight + Theme.s4 : 0)
                    + Math.max(Theme.cardMinHeightCompact - 2 * Theme.s4, texts.implicitHeight)
                    + topPadding + bottomPadding

    Accessible.role: Accessible.RadioButton
    Accessible.name: title
    Accessible.description: description
    Accessible.checkable: true
    Accessible.checked: checked

    Keys.onReturnPressed: card.click()
    Keys.onEnterPressed: card.click()

    background: Item {
        Glow {
            visible: card.checked && card.enabled
            radius: Theme.radiusOverlay
            spread: 10
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusOverlay
            color: card.showPressed ? Theme.cardPressed : card.showHover ? Theme.cardHover : Theme.card
            border.width: 1
            border.color: Theme.cardBorder
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusOverlay
            visible: card.checked
            color: Theme.subtleSelected
            border.width: 2
            border.color: card.enabled ? Theme.accent : Theme.accentDisabled
        }
        Rectangle {
            id: flashFrame
            anchors.fill: parent
            radius: Theme.radiusOverlay
            color: "transparent"
            border.width: 2
            border.color: Theme.accentGlowEnd
            opacity: 0
            SequentialAnimation {
                id: flashAnimation
                NumberAnimation { target: flashFrame; property: "opacity"; to: 1; duration: Theme.durationFast }
                PauseAnimation { duration: Math.max(0, Theme.durationFlash - Theme.durationFast - Theme.durationNormal) }
                NumberAnimation { target: flashFrame; property: "opacity"; to: 0; duration: Theme.durationNormal }
            }
        }
        FocusRing {
            shown: card.visualFocus || card.previewState === "focus"
            radius: Theme.radiusOverlay
        }
    }

    contentItem: ColumnLayout {
        spacing: 0
        Item {
            id: previewArea
            visible: card.hasPreview
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.s4
            implicitHeight: childrenRect.height
        }
        RowLayout {
            id: texts
            spacing: Theme.s4
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            RadioIndicator {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 0
                checked: card.checked
                hovered: card.showHover
                pressed: card.showPressed
                enabled: card.enabled
            }
            Kirigami.Icon {
                visible: card.iconName !== ""
                source: card.iconName
                Layout.preferredWidth: Theme.iconSize
                Layout.preferredHeight: Theme.iconSize
                Layout.alignment: Qt.AlignTop
            }
            ColumnLayout {
                spacing: 0
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                Text {
                    Layout.fillWidth: true
                    text: card.title
                    font: Theme.bodyStrong
                    horizontalAlignment: Text.AlignLeft
                    color: card.enabled ? Theme.text : Theme.textDisabled
                    wrapMode: Text.Wrap
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.bodyLineHeight
                    Accessible.ignored: true
                }
                Text {
                    Layout.fillWidth: true
                    visible: card.description !== ""
                    text: card.description
                    font: Theme.caption
                    horizontalAlignment: Text.AlignLeft
                    color: card.enabled ? Theme.textSecondary : Theme.textDisabled
                    wrapMode: Text.Wrap
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.captionLineHeight
                    Accessible.ignored: true
                }
            }
        }
        // cards side by side share a height: the text stays at the top
        Item { Layout.fillHeight: true }
    }
}
