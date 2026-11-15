// SPDX-License-Identifier: GPL-3.0-or-later
// Recovery › Go back to an earlier state: choose a restore point, read
// what it changes, then the system goes back and the computer restarts.
// Reading the restore points already asks for the administrator's
// password, the way System Restore does; it is not asked twice.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

Item {
    id: root

    required property Recovery recovery

    // only its dialogs show, over the window; the item takes no room
    visible: false
    property int chosen: -1
    property string chosenTitle: ""
    property string chosenWhen: ""

    function open() {
        chosen = -1
        recovery.loadSnapshots()
        choose.open()
    }

    Connections {
        target: root.recovery
        // the password window was closed: nothing to show
        function onSnapshotsChanged() {
            if (root.recovery.snapshotsState === "cancelled")
                choose.close()
        }
        function onCreateChanged() {
            const state = root.recovery.createState
            if (state === "done" || state === "cancelled") {
                making.close()
                root.recovery.resetCreate()
                choose.open()
            }
        }
        function onGoBackChanged() {
            if (root.recovery.goBackState === "cancelled")
                working.close()
        }
    }

    ContentDialog {
        id: choose
        objectName: "goBackChoose"
        readonly property string state_: root.recovery.snapshotsState
        title: i18nc("@title:window", "Go back to an earlier state")
        primaryText: state_ === "ready" && root.recovery.snapshots.length > 0 ? i18nc("@action:button", "Next") : ""
        primaryEnabled: root.chosen > 0
        secondaryText: state_ === "ready" || state_ === "error" ? i18nc("@action:button", "Create a restore point") : ""
        onAccepted: confirm.open()
        onSecondaryClicked: {
            pointName.text = ""
            naming.open()
        }

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 440
            font: Theme.body
            color: Theme.text
            wrapMode: Text.Wrap
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.bodyLineHeight
            text: choose.state_ === "loading" ? i18nc("@info", "Looking for restore points…")
                : choose.state_ === "error" ? i18nc("@info", "Raye couldn't read the restore points. Try again later.")
                : root.recovery.snapshots.length === 0 ? i18nc("@info", "There are no restore points yet. Raye makes one each time it installs updates or apps.")
                : i18nc("@info", "Choose the moment to go back to. Raye makes a restore point each time it installs updates or apps.")
        }

        ProgressBar {
            Layout.fillWidth: true
            visible: choose.state_ === "loading"
            indeterminate: true
        }

        ListView {
            id: list
            objectName: "restorePoints"
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(contentHeight, 5 * 56 + 4 * spacing)
            visible: choose.state_ === "ready" && count > 0
            clip: true
            spacing: Theme.s2
            model: choose.state_ === "ready" ? root.recovery.snapshots : []
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: ThinScrollBar {}
            delegate: T.ItemDelegate {
                id: row
                required property var modelData
                required property int index
                width: ListView.view.width
                implicitHeight: 56
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                checkable: true
                checked: root.chosen === modelData.number
                padding: Theme.s4
                Accessible.role: Accessible.RadioButton
                Accessible.name: modelData.title + ", " + modelData.when
                Accessible.checked: checked
                onClicked: {
                    root.chosen = modelData.number
                    root.chosenTitle = modelData.title
                    root.chosenWhen = modelData.when
                }
                Keys.onSpacePressed: clicked()
                background: Rectangle {
                    radius: Theme.radiusControl
                    color: row.checked ? Theme.subtleSelected
                         : row.down ? Theme.subtlePressed : row.hovered ? Theme.subtleHover : Theme.card
                    border.width: 1
                    border.color: row.checked ? Qt.alpha(Theme.accent, 0.5) : Theme.cardBorder
                    FocusRing { shown: row.visualFocus }
                }
                contentItem: RowLayout {
                    spacing: Theme.s4
                    RadioIndicator {
                        checked: row.checked
                        hovered: row.hovered
                        pressed: row.down
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.title
                            textFormat: Text.PlainText
                            font: Theme.body
                            color: Theme.text
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: row.modelData.when
                            textFormat: Text.PlainText
                            font: Theme.caption
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }

    ContentDialog {
        id: naming
        objectName: "restorePointName"
        title: i18nc("@title:window", "Create a restore point")
        primaryText: i18nc("@action:button", "Create")
        primaryEnabled: pointName.text.trim().length > 0
        onAccepted: {
            root.recovery.createRestorePoint(pointName.text)
            making.open()
        }
        onRejected: choose.open()

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 440
            font: Theme.body
            color: Theme.text
            wrapMode: Text.Wrap
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.bodyLineHeight
            text: i18nc("@info", "Type a name that helps you find it later. The date and time are added for you.")
        }
        TextField {
            id: pointName
            objectName: "restorePointNameField"
            Layout.fillWidth: true
            maximumLength: 60
            placeholderText: i18nc("@info:placeholder", "For example: before the new graphics driver")
            Keys.onReturnPressed: if (naming.primaryEnabled) naming.accept()
        }
    }

    ContentDialog {
        id: making
        readonly property bool failed: root.recovery.createState === "error"
        title: failed ? i18nc("@title:window", "Raye couldn't create the restore point") : i18nc("@title:window", "Creating a restore point…")
        closeText: failed ? i18nc("@action:button", "Close") : ""
        closePolicy: failed ? T.Popup.CloseOnEscape : T.Popup.NoAutoClose
        onClosed: if (failed) root.recovery.resetCreate()

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 400
            font: Theme.body
            color: Theme.text
            wrapMode: Text.Wrap
            visible: making.failed
            text: i18nc("@info", "Nothing was changed. There may not be enough free space.")
        }
        ProgressBar {
            Layout.fillWidth: true
            visible: !making.failed
            indeterminate: true
        }
    }

    ContentDialog {
        id: confirm
        objectName: "goBackConfirm"
        title: i18nc("@title:window", "Go back to this moment?")
        primaryText: i18nc("@action:button", "Go back and restart")
        onAccepted: {
            root.recovery.goBack(root.chosen)
            working.open()
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 440
            spacing: 0
            // what the snapshot is called comes from the system: plain text only
            Text {
                Layout.fillWidth: true
                text: root.chosenTitle
                textFormat: Text.PlainText
                font: Theme.bodyStrong
                color: Theme.text
                wrapMode: Text.Wrap
            }
            Text {
                Layout.fillWidth: true
                text: root.chosenWhen
                textFormat: Text.PlainText
                font: Theme.caption
                color: Theme.textSecondary
            }
        }
        Text {
            Layout.fillWidth: true
            font: Theme.body
            color: Theme.text
            wrapMode: Text.Wrap
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.bodyLineHeight
            text: i18nc("@info", "Your files in your home folder stay. Apps and system settings go back to that moment. The computer restarts, so save your work first.")
        }
    }

    ContentDialog {
        id: working
        objectName: "goBackWorking"
        readonly property bool failed: root.recovery.goBackState === "error"
        title: failed ? i18nc("@title:window", "Raye couldn't go back") : i18nc("@title:window", "Going back…")
        closeText: failed ? i18nc("@action:button", "Close") : ""
        closePolicy: failed ? T.Popup.CloseOnEscape : T.Popup.NoAutoClose
        onClosed: if (failed) root.recovery.resetGoBack()

        Text {
            Layout.fillWidth: true
            Layout.preferredWidth: 400
            font: Theme.body
            color: Theme.text
            wrapMode: Text.Wrap
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.bodyLineHeight
            text: working.failed ? i18nc("@info", "Nothing was changed. Try another restore point, or repair Raye.")
                                 : i18nc("@info", "Don't turn off your PC. It restarts by itself.")
        }
        ProgressBar {
            Layout.fillWidth: true
            visible: !working.failed
            indeterminate: true
        }
    }
}
