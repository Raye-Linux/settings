// SPDX-License-Identifier: GPL-3.0-or-later
// One of the two cards at the top right of the launch screen (Raye
// account, Raye Update): 300 × 96, radius 8, a 32 px picture on the
// left, a title and a line under it. Anything put inside sits under
// the text (a link, a button).
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

T.AbstractButton {
    id: card

    property string title
    property string message
    property string iconName
    property string iconFallback
    // a picture drawn in QML instead of an icon
    property Component glyph
    property string searchId
    default property alias extra: extraRow.data

    readonly property bool showHover: clickable && hovered
    property bool clickable: true

    function flash() { flashAnimation.restart() }

    implicitWidth: 300
    implicitHeight: Math.max(96, content.implicitHeight + topPadding + bottomPadding)
    padding: Theme.s5
    hoverEnabled: clickable
    focusPolicy: clickable ? Qt.StrongFocus : Qt.NoFocus

    Accessible.role: clickable ? Accessible.Button : Accessible.Grouping
    Accessible.name: title
    Accessible.description: message
    Keys.onReturnPressed: if (clickable) card.clicked()
    Keys.onEnterPressed: if (clickable) card.clicked()

    transform: Translate {
        y: card.showHover && !card.down ? -1 : 0
        Behavior on y { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingFast } }
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusOverlay
            color: card.down && card.clickable ? Theme.cardPressed : card.showHover ? Theme.cardHover : Theme.card
            border.width: 1
            border.color: card.showHover ? Qt.alpha(Theme.accent, Theme.night ? 0.5 : 0.35) : Theme.cardBorder
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
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
        FocusRing { shown: card.visualFocus; radius: Theme.radiusOverlay }
    }

    contentItem: RowLayout {
        id: content
        spacing: Theme.s4
        Item {
            Layout.alignment: Qt.AlignTop
            implicitWidth: 32
            implicitHeight: 32
            Kirigami.Icon {
                anchors.fill: parent
                visible: card.glyph === null
                source: card.iconName
                fallback: card.iconFallback
            }
            Loader {
                anchors.fill: parent
                active: card.glyph !== null
                sourceComponent: card.glyph
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Theme.s1
            Text {
                Layout.fillWidth: true
                text: card.title
                font: Theme.bodyStrong
                color: Theme.text
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignLeft
                Accessible.ignored: true
            }
            Text {
                Layout.fillWidth: true
                text: card.message
                font: Theme.caption
                color: Theme.textSecondary
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                textFormat: Text.PlainText
                horizontalAlignment: Text.AlignLeft
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.captionLineHeight
                Accessible.ignored: true
            }
            Row {
                id: extraRow
                Layout.topMargin: children.length > 0 ? Theme.s1 : 0
                spacing: Theme.s3
                visible: children.length > 0
            }
        }
    }
}
