// SPDX-License-Identifier: GPL-3.0-or-later
// "Add an app" of Apps > Startup: the installed apps with a search box;
// the one picked starts when you sign in. The list is read when the
// dialog opens, not with the page.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

ContentDialog {
    id: dialog

    signal picked(string desktopFile)

    property string chosen: ""

    title: i18nc("@title:window", "Add an app to startup")
    primaryText: i18nc("@action:button", "Add")
    primaryEnabled: chosen !== ""
    onAboutToShow: {
        chosen = ""
        filterField.text = ""
        loader.active = true
    }
    onAccepted: if (chosen !== "") picked(chosen)

    TextField {
        id: filterField
        Layout.fillWidth: true
        placeholderText: i18nc("@info:placeholder", "Search apps")
        Accessible.name: i18nc("@info:placeholder", "Search apps")
        onTextChanged: if (loader.item) loader.item.searchText = text
    }

    Loader {
        id: loader
        active: false
        sourceComponent: InstalledApps {
            // Windows programs start through their own launchers
            filter: "all"
        }
    }

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.preferredHeight: 320
        Layout.topMargin: Theme.s3
        clip: true
        model: loader.item
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: ThinScrollBar {}
        delegate: T.ItemDelegate {
            id: item
            required property var model
            width: ListView.view.width
            visible: model.desktopFile !== "" && model.source !== "windows"
            height: visible ? 44 : 0
            leftPadding: 12
            rightPadding: 12
            hoverEnabled: true
            readonly property bool picked: dialog.chosen === model.desktopFile
            Accessible.role: Accessible.RadioButton
            Accessible.name: model.name
            Accessible.checked: picked
            onClicked: dialog.chosen = model.desktopFile
            contentItem: RowLayout {
                spacing: Theme.s4
                Kirigami.Icon {
                    Layout.preferredWidth: 24
                    Layout.preferredHeight: 24
                    source: item.model.iconName
                    fallback: "application-x-executable"
                }
                Text {
                    Layout.fillWidth: true
                    text: item.model.name
                    font: Theme.body
                    color: Theme.text
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
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
            }
        }
        Text {
            anchors.centerIn: parent
            visible: loader.item !== null && loader.item.loading
            text: i18nc("@info", "Looking for apps…")
            font: Theme.body
            color: Theme.textSecondary
        }
    }
}
