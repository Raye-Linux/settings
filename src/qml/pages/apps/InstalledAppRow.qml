// SPDX-License-Identifier: GPL-3.0-or-later
// One app in Apps > Installed apps: icon 32, name, where it comes from ·
// version · date, its size, and the "..." menu. Reads the model's roles.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

T.Control {
    id: row

    required property string appKey
    required property string name
    required property string iconName
    required property url iconSource
    required property bool removable
    required property string desktopFile
    required property string publisher
    required property string source
    required property string version
    required property var installed
    required property var size
    property string detail
    property string sizeText
    property bool busy: false
    property int percent: 0

    signal openRequested()
    signal advancedRequested()
    signal uninstallRequested(Item anchor)

    implicitHeight: Theme.cardMinHeight
    hoverEnabled: true
    Accessible.role: Accessible.ListItem
    Accessible.name: name
    Accessible.description: detail

    background: Rectangle {
        radius: Theme.radiusControl
        color: row.hovered ? Theme.cardHover : Theme.card
        border.width: 1
        border.color: Theme.cardBorder
    }

    contentItem: RowLayout {
        spacing: 0
        Kirigami.Icon {
            Layout.leftMargin: Theme.cardPadding
            Layout.preferredWidth: 32
            Layout.preferredHeight: 32
            source: row.iconSource.toString() !== "" ? row.iconSource : row.iconName
            fallback: "application-x-executable"
        }
        ColumnLayout {
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s4
            spacing: 0
            Text {
                Layout.fillWidth: true
                text: row.name
                font: Theme.body
                color: Theme.text
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                Layout.fillWidth: true
                visible: !row.busy
                text: row.detail
                font: Theme.caption
                color: Theme.textSecondary
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            RowLayout {
                visible: row.busy
                Layout.fillWidth: true
                spacing: Theme.s3
                Text {
                    text: i18nc("@info an app is being removed", "Uninstalling…")
                    font: Theme.caption
                    color: Theme.textSecondary
                }
                ProgressBar {
                    Layout.preferredWidth: 160
                    indeterminate: row.percent <= 0
                    value: row.percent / 100
                }
            }
        }
        Text {
            Layout.leftMargin: Theme.s4
            visible: row.sizeText !== ""
            text: row.sizeText
            font: Theme.body
            color: Theme.textSecondary
        }
        MoreButton {
            id: more
            Layout.leftMargin: Theme.s3
            Layout.rightMargin: Theme.s3
            accessibleName: i18nc("@action:button %1 is an app", "More options for %1", row.name)
            enabled: !row.busy
            onClicked: menu.open()
            InstalledAppMenu {
                id: menu
                y: more.height + 2
                x: more.width - width
                actions: [{ id: "open", text: i18nc("@action:inmenu", "Open"), enabled: row.desktopFile !== "" },
                          { id: "advanced", text: i18nc("@action:inmenu", "Advanced options") },
                          { id: "uninstall", text: i18nc("@action:inmenu", "Uninstall"), enabled: row.removable }]
                onTriggered: (id) => {
                    if (id === "open")
                        row.openRequested()
                    else if (id === "advanced")
                        row.advancedRequested()
                    else if (id === "uninstall")
                        row.uninstallRequested(more)
                }
            }
        }
    }

    // the "..." button
    component MoreButton: T.AbstractButton {
        id: button
        property string accessibleName
        implicitWidth: 36
        implicitHeight: 32
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.role: Accessible.Button
        Accessible.name: accessibleName
        background: Item {
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusControl
                color: button.down ? Theme.subtlePressed : button.hovered ? Theme.subtleHover : "transparent"
            }
            FocusRing { shown: button.visualFocus }
        }
        contentItem: Item {
            Glyph {
                anchors.centerIn: parent
                name: "more"
                size: 16
                color: button.enabled ? Theme.text : Theme.textDisabled
            }
        }
    }
}
