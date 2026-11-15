// SPDX-License-Identifier: GPL-3.0-or-later
// "Rename your PC": the name is checked as it is typed, with Windows'
// rules, and the button stays off until it is fine. The administrator
// password is asked by the system when the name is saved.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    required property Hostname hostname

    readonly property string problem: hostname.problemWith(field.text)
    // shown once the user typed something, never for the name as it is
    readonly property bool showProblem: field.text !== "" && problem !== ""

    signal done(bool ok, string error)

    title: i18nc("@title:window", "Rename your PC")
    primaryText: i18nc("@action:button", "Rename")
    primaryEnabled: problem === "" && field.text !== hostname.name && !hostname.busy

    function start() {
        field.text = hostname.name
        open()
        field.selectAll()
        field.forceActiveFocus()
    }

    onAboutToShow: field.forceActiveFocus()
    onAccepted: hostname.rename(field.text)

    Connections {
        target: dialog.hostname
        function onRenamed(ok, error) { dialog.done(ok, error) }
    }

    Text {
        Layout.fillWidth: true
        Layout.preferredWidth: 440
        font: Theme.body
        color: Theme.text
        wrapMode: Text.Wrap
        text: i18nc("@info", "Current PC name: %1", dialog.hostname.name)
    }
    TextField {
        id: field
        Layout.fillWidth: true
        Layout.topMargin: Theme.s3
        maximumLength: 32
        placeholderText: i18nc("@info:placeholder", "New PC name")
        Accessible.name: i18nc("@label", "New PC name")
        Accessible.description: dialog.showProblem ? dialog.hostname.problemText(dialog.problem) : hint.text
        onAccepted: if (dialog.primaryEnabled) dialog.accept()
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s3
        StatusGlyph {
            visible: dialog.showProblem
            status: "critical"
            Layout.alignment: Qt.AlignTop
        }
        Text {
            id: hint
            Layout.fillWidth: true
            Layout.preferredWidth: 420
            font: Theme.caption
            color: dialog.showProblem ? Theme.critical : Theme.textSecondary
            wrapMode: Text.Wrap
            text: dialog.showProblem
                  ? dialog.hostname.problemText(dialog.problem)
                  : i18nc("@info", "You can use a combination of letters, hyphens and numbers, up to 15 characters.")
        }
    }
}
