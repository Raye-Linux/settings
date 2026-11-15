// SPDX-License-Identifier: GPL-3.0-or-later
// A category tile of the launch screen: a 40 px glyph, the name and a
// one-line summary ("Display, sound, notifications, power"). On hover
// it lifts by 2 px; pressed, it gives a little.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

T.AbstractButton {
    id: tile

    property string title
    property string summary
    property string iconName
    property string iconFallback
    // a page or category id; empty: the launch screen handles clicked()
    property string target
    // for the gallery: "hover", "pressed" or "focus" shown at rest
    property string previewState: ""

    readonly property bool showHover: enabled && (hovered || previewState === "hover")
    readonly property bool showPressed: enabled && (down || previewState === "pressed")

    implicitWidth: Theme.tileMinWidth
    implicitHeight: Theme.tileMinHeight
    padding: Theme.s5
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: title
    Accessible.description: summary

    Keys.onReturnPressed: tile.clicked()
    Keys.onEnterPressed: tile.clicked()
    onClicked: if (target !== "") Router.navigate(target)

    scale: showPressed ? 0.98 : 1
    Behavior on scale { NumberAnimation { duration: Theme.durationFaster } }
    transform: Translate {
        y: tile.showHover && !tile.showPressed ? -2 : 0
        Behavior on y { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingFast } }
    }

    background: Item {
        Glow {
            visible: tile.showHover
            radius: Theme.radiusOverlay
            spread: 14
            offsetY: 4
            color: Qt.alpha(Theme.shadow, Theme.night ? 0.35 : 0.12)
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusOverlay
            color: tile.showPressed ? Theme.cardPressed : tile.showHover ? Theme.cardHover : Theme.card
            border.width: 1
            border.color: tile.showHover ? Qt.alpha(Theme.accent, Theme.night ? 0.55 : 0.4) : Theme.cardBorder
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
        }
        FocusRing {
            shown: tile.visualFocus || tile.previewState === "focus"
            radius: Theme.radiusOverlay
        }
    }

    contentItem: RowLayout {
        spacing: Theme.s4
        Kirigami.Icon {
            source: tile.iconName
            fallback: tile.iconFallback
            Layout.preferredWidth: Theme.tileIconSize
            Layout.preferredHeight: Theme.tileIconSize
            Layout.alignment: Qt.AlignVCenter
            opacity: tile.enabled ? 1 : 0.4
        }
        ColumnLayout {
            spacing: Theme.s1
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            Text {
                Layout.fillWidth: true
                text: tile.title
                font: Theme.tileTitle
                horizontalAlignment: Text.AlignLeft
                color: tile.enabled ? Theme.text : Theme.textDisabled
                elide: Text.ElideRight
                Accessible.ignored: true
            }
            Text {
                Layout.fillWidth: true
                text: tile.summary
                font: Theme.caption
                horizontalAlignment: Text.AlignLeft
                color: tile.enabled ? Theme.textSecondary : Theme.textDisabled
                elide: Text.ElideRight
                maximumLineCount: 1
                Accessible.ignored: true
            }
        }
    }
}
