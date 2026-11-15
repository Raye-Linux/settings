// SPDX-License-Identifier: GPL-3.0-or-later
// One app in the grid view of Apps > Installed apps: a bigger icon, the
// name, its size, and the same "..." menu as the list.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

T.Control {
    id: tile

    required property string appKey
    required property string name
    required property string iconName
    required property url iconSource
    required property bool removable
    required property string desktopFile
    required property string source
    required property var size
    property string detail
    property bool busy: false

    signal openRequested()
    signal advancedRequested()
    signal uninstallRequested(Item anchor)

    implicitHeight: 132
    hoverEnabled: true
    padding: Theme.cardPadding
    Accessible.role: Accessible.ListItem
    Accessible.name: name
    Accessible.description: detail

    background: Rectangle {
        radius: Theme.radiusOverlay
        color: tile.hovered ? Theme.cardHover : Theme.card
        border.width: 1
        border.color: Theme.cardBorder
    }

    contentItem: Item {
        Kirigami.Icon {
            id: icon
            width: 48
            height: 48
            source: tile.iconSource.toString() !== "" ? tile.iconSource : tile.iconName
            fallback: "application-x-executable"
        }
        InstalledAppRow.MoreButton {
            id: more
            anchors.right: parent.right
            anchors.top: parent.top
            accessibleName: i18nc("@action:button %1 is an app", "More options for %1", tile.name)
            enabled: !tile.busy
            onClicked: menu.open()
            InstalledAppMenu {
                id: menu
                y: more.height + 2
                x: more.width - width
                actions: [{ id: "open", text: i18nc("@action:inmenu", "Open"), enabled: tile.desktopFile !== "" },
                          { id: "advanced", text: i18nc("@action:inmenu", "Advanced options") },
                          { id: "uninstall", text: i18nc("@action:inmenu", "Uninstall"), enabled: tile.removable }]
                onTriggered: (id) => {
                    if (id === "open")
                        tile.openRequested()
                    else if (id === "advanced")
                        tile.advancedRequested()
                    else if (id === "uninstall")
                        tile.uninstallRequested(more)
                }
            }
        }
        ColumnLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: tile.name
                font: Theme.bodyStrong
                color: Theme.text
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                Layout.fillWidth: true
                text: tile.busy ? i18nc("@info an app is being removed", "Uninstalling…") : tile.detail
                font: Theme.caption
                color: Theme.textSecondary
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
        }
    }
}
