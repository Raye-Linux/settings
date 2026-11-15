// SPDX-License-Identifier: GPL-3.0-or-later
// The flyout under "Uninstall" (Windows: "This app and its related info
// will be uninstalled." and one button). Nothing happens until the
// button in it is pressed; Esc or a click elsewhere closes it.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

Flyout {
    id: flyout

    property string appId
    property string appName
    signal confirmed(string id)

    width: 300
    padding: Theme.cardPadding
    focus: true
    modal: false
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside

    contentItem: ColumnLayout {
        spacing: Theme.s4
        Text {
            Layout.fillWidth: true
            text: i18nc("@info", "This app and its related info will be uninstalled.")
            font: Theme.body
            color: Theme.text
            wrapMode: Text.Wrap
        }
        Button {
            Layout.alignment: Qt.AlignRight
            accent: true
            focus: true
            text: i18nc("@action:button", "Uninstall")
            Accessible.description: i18nc("@info %1 is an app", "Uninstall %1", flyout.appName)
            onClicked: {
                flyout.close()
                flyout.confirmed(flyout.appId)
            }
        }
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durationFast; easing.type: Theme.easingFast }
        NumberAnimation { property: "y"; from: flyout.y - 8; to: flyout.y; duration: Theme.durationNormal; easing.type: Theme.easingEnter }
    }
}
