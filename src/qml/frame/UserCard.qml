// SPDX-License-Identifier: GPL-3.0-or-later
// Who is signed in: the picture, the name, and the Raye account's email
// (or the kind of local account). On the launch screen ("hero") it is the
// big 72 px picture with a "Manage your account" link, to the account's
// website when signed in to a Raye account, else to Your info; at the top
// of the navigation pane ("pane") a 56 px picture and the whole block
// opens Your info; in the narrow pane ("icon") only the picture.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

T.AbstractButton {
    id: card

    // "hero", "pane" or "icon"
    property string mode: "hero"
    readonly property string target: "accounts/your-info"
    readonly property real pictureSize: mode === "hero" ? 72 : mode === "pane" ? 56 : 32

    hoverEnabled: mode !== "hero"
    focusPolicy: mode === "hero" ? Qt.NoFocus : Qt.StrongFocus
    padding: mode === "hero" ? 0 : Theme.s3
    implicitWidth: mode === "icon" ? pictureSize + 2 * padding : row.implicitWidth + 2 * padding
    implicitHeight: pictureSize + 2 * padding

    Accessible.role: mode === "hero" ? Accessible.Grouping : Accessible.Link
    readonly property string secondLine: AccountIdentity.signedIn ? AccountIdentity.email : CurrentUser.accountLabel

    Accessible.name: AccountIdentity.name
    Accessible.description: secondLine

    onClicked: if (mode !== "hero") Router.navigate(target)
    Keys.onReturnPressed: card.clicked()
    Keys.onEnterPressed: card.clicked()

    ToolTip {
        visible: card.mode === "icon" && card.hovered
        text: AccountIdentity.name
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusControl
            visible: card.mode !== "hero"
            color: card.down ? Theme.subtlePressed : card.hovered ? Theme.subtleHover : "transparent"
            Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
        }
        FocusRing { shown: card.visualFocus }
    }

    contentItem: RowLayout {
        id: row
        spacing: card.mode === "hero" ? Theme.s6 : Theme.s4

        PersonPicture {
            Layout.alignment: Qt.AlignVCenter
            size: card.pictureSize
            source: AccountIdentity.picture
            name: AccountIdentity.name
        }

        ColumnLayout {
            visible: card.mode !== "icon"
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
            spacing: card.mode === "hero" ? Theme.s1 : 0

            Text {
                Layout.fillWidth: true
                text: AccountIdentity.name
                font: card.mode === "hero" ? Theme.subtitle : Theme.bodyStrong
                color: Theme.text
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignLeft
                Accessible.ignored: true
            }
            Row {
                Layout.fillWidth: true
                spacing: Theme.s2
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: card.secondLine
                    font: Theme.caption
                    color: Theme.textSecondary
                    Accessible.ignored: true
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: card.mode === "hero"
                    text: "·"
                    font: Theme.caption
                    color: Theme.textSecondary
                    Accessible.ignored: true
                }
                HyperlinkButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: card.mode === "hero"
                    font: Theme.captionStrong
                    text: i18nc("@action:button opens Accounts > Your info", "Manage your account")
                    onClicked: AccountIdentity.signedIn ? Qt.openUrlExternally(AccountIdentity.manageUrl)
                                                        : Router.navigate(card.target)
                }
            }
        }
    }
}
