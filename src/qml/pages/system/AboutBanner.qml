// SPDX-License-Identifier: GPL-3.0-or-later
// The top of About and of System: the picture of the PC by its shape,
// its name, its model, and "Rename this PC".
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.raye.settings

Item {
    id: banner

    property DeviceInfo device
    property string name
    property bool busy: false
    // the picture size: 96 in About, 72 on the System page
    property int pictureSize: 96
    property bool showRename: true
    // a button on the right (About), else a link under the model (System)
    property bool renameButton: true

    // search lands on the banner for "rename"
    property string searchId: "rename-pc"
    function flash() { flashAnimation.restart() }

    signal renameClicked()

    implicitHeight: Math.max(pictureSize, texts.implicitHeight)
    implicitWidth: 480

    Accessible.role: Accessible.Grouping
    Accessible.name: name

    Rectangle {
        id: flashRing
        anchors.fill: parent
        anchors.margins: -Theme.s3
        radius: Theme.radiusOverlay
        color: "transparent"
        border.width: 2
        border.color: Theme.accent
        opacity: 0
        SequentialAnimation on opacity {
            id: flashAnimation
            running: false
            NumberAnimation { to: 1; duration: Theme.durationFast }
            PauseAnimation { duration: 900 }
            NumberAnimation { to: 0; duration: Theme.durationNormal }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: Theme.s5

        Kirigami.Icon {
            Layout.preferredWidth: banner.pictureSize
            Layout.preferredHeight: banner.pictureSize
            Layout.alignment: Qt.AlignVCenter
            source: "org.raye.settings-chassis-" + (banner.device && banner.device.ready ? banner.device.chassis : "desktop")
            fallback: "computer"
            Accessible.ignored: true
        }
        ColumnLayout {
            id: texts
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: Theme.s1

            Text {
                Layout.fillWidth: true
                font: Theme.title
                color: Theme.text
                elide: Text.ElideRight
                text: banner.name
            }
            Text {
                Layout.fillWidth: true
                font: Theme.body
                color: Theme.textSecondary
                elide: Text.ElideRight
                visible: text !== ""
                text: banner.device && banner.device.model !== "" ? banner.device.model : ""
            }
            HyperlinkButton {
                visible: banner.showRename && !banner.renameButton
                enabled: !banner.busy
                text: i18nc("@action:button rename this PC", "Rename")
                onClicked: banner.renameClicked()
            }
        }
        Button {
            Layout.alignment: Qt.AlignVCenter
            visible: banner.showRename && banner.renameButton
            enabled: !banner.busy
            text: i18nc("@action:button", "Rename this PC")
            onClicked: banner.renameClicked()
        }
    }
}
