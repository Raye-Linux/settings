// SPDX-License-Identifier: GPL-3.0-or-later
// One troubleshooter at work: each check with its state as it runs, the
// fixes Raye proposes (and whether they need an administrator), then
// what was fixed and what to try for the rest. Nothing is changed before
// "Fix problems" is pressed; closing the dialog stops what is running.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

ContentDialog {
    id: dialog

    readonly property alias runner: runner

    function start(name) {
        runner.name = name
        open()
        runner.run()
    }

    title: runner.title
    width: Math.min(Theme.dialogMaxWidth, parent ? parent.width - 2 * Theme.s7 : Theme.dialogMaxWidth)
    // Esc closes it once nothing is running; Cancel stops a run
    closePolicy: runner.busy ? T.Popup.NoAutoClose : T.Popup.CloseOnEscape

    Troubleshooter { id: runner }

    onClosed: if (runner.busy) runner.cancel()

    // the glyph and colour of each state of a step
    function glyphOf(status) {
        switch (status) {
        case "ok": case "fixed": return "success"
        case "problem": return "caution"
        case "notFixed": return "critical"
        case "unknown": return ""
        }
        return "info"
    }

    function headline() {
        switch (runner.state) {
        case "checking": return i18n("Looking for problems…")
        case "found": return i18np("Raye found a problem it can fix.", "Raye found problems it can fix.", runner.remedies.length)
        case "fixing": return i18n("Fixing problems…")
        case "verifying": return i18n("Checking again…")
        case "done": case "failed": return runner.resultTitle
        }
        return ""
    }

    // what the troubleshooter is doing, or how it ended
    RowLayout {
        Layout.fillWidth: true
        spacing: Theme.s4
        StatusGlyph {
            size: 24
            visible: !runner.busy && runner.state !== "idle"
            status: runner.state === "found" ? "caution"
                  : runner.state === "failed" ? "critical"
                  : runner.resultStatus === "ok" || runner.resultStatus === "fixed" ? "success"
                  : runner.resultStatus === "partly" ? "caution" : "critical"
            label: dialog.headline()
        }
        Text {
            Layout.fillWidth: true
            text: dialog.headline()
            font: Theme.bodyStrong
            color: Theme.text
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignLeft
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }

    ProgressBar {
        Layout.fillWidth: true
        indeterminate: true
        visible: runner.busy
    }

    Text {
        Layout.fillWidth: true
        visible: text !== "" && (runner.state === "done" || runner.state === "failed")
        text: runner.resultText
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignLeft
    }

    // the checks, one row each
    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: stepColumn.implicitHeight + 2 * Theme.s1
        visible: runner.steps.length > 0
        radius: Theme.radiusControl
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder

        Column {
            id: stepColumn
            x: 0
            y: Theme.s1
            width: parent.width

            Repeater {
                model: runner.steps
                delegate: Item {
                    id: row
                    required property var modelData
                    required property int index
                    width: stepColumn.width
                    implicitHeight: Math.max(44, rowText.implicitHeight + 2 * Theme.s3)
                    height: implicitHeight
                    Accessible.role: Accessible.StaticText
                    Accessible.name: modelData.title + ". " + modelData.detail

                    Rectangle {
                        visible: row.index > 0
                        width: parent.width - 2 * Theme.s4
                        x: Theme.s4
                        height: 1
                        color: Theme.divider
                    }
                    StatusGlyph {
                        id: mark
                        x: Theme.s4
                        y: Theme.s3 + 2
                        size: 16
                        status: dialog.glyphOf(row.modelData.status)
                        readonly property bool working: row.modelData.status === "running" || row.modelData.status === "fixing"
                        SequentialAnimation on opacity {
                            running: mark.working
                            loops: Animation.Infinite
                            NumberAnimation { from: 1; to: 0.35; duration: 600; easing.type: Easing.InOutSine }
                            NumberAnimation { from: 0.35; to: 1; duration: 600; easing.type: Easing.InOutSine }
                            onRunningChanged: if (!running) mark.opacity = 1
                        }
                    }
                    Column {
                        id: rowText
                        x: mark.x + mark.width + Theme.s4
                        y: Theme.s3
                        width: parent.width - x - Theme.s4
                        spacing: 2
                        Text {
                            width: parent.width
                            text: row.modelData.title
                            font: Theme.body
                            color: Theme.text
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignLeft
                        }
                        Text {
                            width: parent.width
                            text: row.modelData.detail
                            font: Theme.caption
                            color: Theme.textSecondary
                            wrapMode: Text.Wrap
                            horizontalAlignment: Text.AlignLeft
                        }
                    }
                }
            }
        }
    }

    // before anything changes: what "Fix problems" will do
    ColumnLayout {
        Layout.fillWidth: true
        visible: runner.state === "found"
        spacing: Theme.s2

        Text {
            Layout.fillWidth: true
            text: i18n("Raye will:")
            font: Theme.body
            color: Theme.text
            horizontalAlignment: Text.AlignLeft
        }
        Repeater {
            model: runner.state === "found" ? runner.remedies : []
            delegate: Text {
                required property var modelData
                Layout.fillWidth: true
                Layout.leftMargin: Theme.s3
                text: "• " + modelData.text
                font: Theme.body
                color: Theme.text
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignLeft
            }
        }
        Text {
            Layout.fillWidth: true
            visible: runner.needsAdmin
            text: runner.remedies.length === 1 ? i18n("This needs root access.")
                                               : i18n("Some of these need root access.")
            font: Theme.caption
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignLeft
        }
    }

    footer: Item {
        implicitHeight: buttons.implicitHeight + 2 * Theme.s7
        Item {
            anchors.fill: parent
            clip: true
            Rectangle {
                width: parent.width
                height: parent.height + radius
                y: -radius
                radius: Theme.radiusOverlay
                color: Theme.flyoutFooter
                border.width: 1
                border.color: Theme.strokeSurface
            }
        }
        RowLayout {
            id: buttons
            anchors.fill: parent
            anchors.margins: Theme.s7
            spacing: Theme.s3
            Button {
                Layout.fillWidth: true
                visible: runner.state === "found"
                accent: true
                text: i18ncp("@action:button", "Fix problem", "Fix problems", runner.remedies.length)
                onClicked: runner.fix()
            }
            Button {
                Layout.fillWidth: true
                visible: runner.state === "failed"
                accent: true
                text: i18nc("@action:button", "Try again")
                onClicked: runner.run()
            }
            Button {
                Layout.fillWidth: true
                visible: runner.busy
                text: i18nc("@action:button", "Cancel")
                onClicked: dialog.close()
            }
            Button {
                Layout.fillWidth: true
                visible: !runner.busy
                accent: runner.state === "done"
                text: i18nc("@action:button", "Close")
                onClicked: dialog.close()
            }
        }
    }
}
