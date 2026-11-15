// SPDX-License-Identifier: GPL-3.0-or-later
// "How do you want to open .png files from now on?": the apps that can
// open a kind of file or link, the one in use marked, and "Set default".
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

ContentDialog {
    id: dialog

    // [{ id, name, iconName, current }]
    property var choices: []
    property string chosen: ""
    signal chosenApp(string appId)

    primaryText: i18nc("@action:button", "Set default")
    primaryEnabled: chosen !== ""
    onAboutToShow: {
        chosen = ""
        for (const c of choices) {
            if (c.current)
                chosen = c.id
        }
    }
    onAccepted: if (chosen !== "") chosenApp(chosen)

    Text {
        Layout.fillWidth: true
        visible: dialog.choices.length === 0
        text: i18nc("@info", "No app on this PC can open this. Look for one in Raye Store.")
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.preferredHeight: Math.min(contentHeight, 320)
        clip: true
        model: dialog.choices
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: ThinScrollBar {}
        delegate: T.ItemDelegate {
            id: item
            required property var modelData
            width: ListView.view.width
            implicitHeight: 48
            leftPadding: 12
            rightPadding: 12
            hoverEnabled: true
            readonly property bool picked: dialog.chosen === modelData.id
            Accessible.role: Accessible.RadioButton
            Accessible.name: modelData.name
            Accessible.checked: picked
            onClicked: dialog.chosen = modelData.id
            contentItem: RowLayout {
                spacing: Theme.s4
                Kirigami.Icon {
                    Layout.preferredWidth: 32
                    Layout.preferredHeight: 32
                    source: item.modelData.iconName
                    fallback: "application-x-executable"
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: item.modelData.name
                        font: Theme.body
                        color: Theme.text
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                    Text {
                        visible: item.modelData.current
                        text: i18nc("@info the app used today", "In use now")
                        font: Theme.caption
                        color: Theme.textSecondary
                    }
                }
            }
            background: Item {
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: Theme.radiusControl
                    color: item.down ? Theme.subtlePressed : item.picked ? Theme.subtleSelected
                         : item.hovered ? Theme.subtleHover : "transparent"
                }
                AccentPill {
                    visible: item.picked
                    x: 3
                    anchors.verticalCenter: parent.verticalCenter
                    height: 16
                }
                FocusRing { shown: item.visualFocus }
            }
        }
    }
}
