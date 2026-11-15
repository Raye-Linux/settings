// SPDX-License-Identifier: GPL-3.0-or-later
// "Add a device", as Windows asks it: first the kind of device
// (Bluetooth, or everything else, which is printers and cameras), then
// the devices found nearby with placeholders while it looks, then
// connecting and the result in plain words. A code to compare or a PIN
// is asked by the desktop's Bluetooth helper, in a notification.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

ContentDialog {
    id: dialog

    property var bluetooth: null
    // "choose", "scan", "pairing", "done" or "failed"
    property string step: "choose"
    property string deviceName: ""
    property string deviceAddress: ""
    property string failure: ""

    title: i18nc("@title:window", "Add a device")
    // as wide as Windows' own, whatever step it is at
    width: Theme.dialogMaxWidth
    closeText: step === "done" ? i18nc("@action:button", "Done") : i18nc("@action:button", "Cancel")
    primaryText: step === "failed" ? i18nc("@action:button", "Try again") : ""
    onAccepted: {
        // "Try again": back to the list, still looking
        step = "scan"
        open()
    }
    onAboutToShow: if (step !== "scan") step = "choose"
    onClosed: if (bluetooth && step !== "scan") bluetooth.stopScan()
    onRejected: {
        if (bluetooth)
            bluetooth.stopScan()
        step = "choose"
    }

    onStepChanged: {
        if (!bluetooth)
            return
        if (step === "scan")
            bluetooth.startScan()
    }

    Connections {
        target: dialog.bluetooth
        function onPairingFinished(address, ok, message) {
            if (address !== dialog.deviceAddress)
                return
            dialog.failure = message
            dialog.step = ok ? "done" : "failed"
        }
    }

    // --- the kind of device ---
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.step === "choose"
        spacing: Theme.s3
        Text {
            Layout.fillWidth: true
            text: i18nc("@info", "Choose the kind of device you want to add.")
            font: Theme.body
            color: Theme.text
            wrapMode: Text.Wrap
        }
        KindButton {
            iconName: "preferences-system-bluetooth"
            title: i18nc("@title a kind of device", "Bluetooth")
            text: i18nc("@info", "Mice, keyboards, pens, or audio and other kinds of Bluetooth devices")
            enabled: dialog.bluetooth !== null && dialog.bluetooth.available
            onClicked: {
                if (!dialog.bluetooth.powered)
                    dialog.bluetooth.setPowered(true)
                dialog.step = "scan"
            }
        }
        KindButton {
            iconName: "printer"
            title: i18nc("@title a kind of device", "Everything else")
            text: i18nc("@info", "Printers and scanners, cameras and more")
            onClicked: {
                dialog.close()
                Router.navigate("devices/printers")
            }
        }
    }

    // --- devices found nearby ---
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.step === "scan"
        spacing: Theme.s3
        Text {
            Layout.fillWidth: true
            text: i18nc("@info", "Make sure your device is turned on and can be found. Select a device below to connect.")
            font: Theme.body
            color: Theme.text
            wrapMode: Text.Wrap
        }
        ListView {
            id: nearby
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(Math.max(contentHeight, 3 * 56), 280)
            clip: true
            model: dialog.bluetooth ? dialog.bluetooth.nearbyDevices : null
            boundsBehavior: Flickable.StopAtBounds
            T.ScrollBar.vertical: ThinScrollBar {}
            spacing: Theme.s1
            delegate: T.ItemDelegate {
                id: item
                required property var model
                width: ListView.view.width
                implicitHeight: 56
                leftPadding: Theme.s4
                rightPadding: Theme.s4
                hoverEnabled: true
                Accessible.role: Accessible.Button
                Accessible.name: model.name
                onClicked: {
                    dialog.deviceName = model.name
                    dialog.deviceAddress = model.address
                    dialog.step = "pairing"
                    if (!dialog.bluetooth.pair(model.address)) {
                        dialog.failure = ""
                        dialog.step = "failed"
                    }
                }
                contentItem: RowLayout {
                    spacing: Theme.s4
                    Kirigami.Icon {
                        Layout.preferredWidth: 24
                        Layout.preferredHeight: 24
                        source: item.model.iconName
                        fallback: "preferences-system-bluetooth"
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
                background: Rectangle {
                    radius: Theme.radiusControl
                    color: item.down ? Theme.subtlePressed : item.hovered ? Theme.subtleHover : Theme.control
                    FocusRing { shown: item.visualFocus }
                }
            }
            // placeholders while nothing is found yet
            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                visible: nearby.count === 0
                spacing: Theme.s1
                Repeater {
                    model: 3
                    Shimmer {
                        width: parent.width
                        height: 56
                    }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            text: dialog.bluetooth && dialog.bluetooth.scanning ? i18nc("@info", "Looking for devices…") : ""
            font: Theme.caption
            color: Theme.textSecondary
        }
    }

    // --- connecting, then the result ---
    ColumnLayout {
        Layout.fillWidth: true
        visible: dialog.step === "pairing" || dialog.step === "done" || dialog.step === "failed"
        spacing: Theme.s4
        RowLayout {
            spacing: Theme.s4
            StatusGlyph {
                visible: dialog.step !== "pairing"
                status: dialog.step === "done" ? "success" : "critical"
                size: 24
            }
            Text {
                Layout.fillWidth: true
                text: dialog.deviceName
                font: Theme.bodyStrong
                color: Theme.text
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
        }
        ProgressBar {
            Layout.fillWidth: true
            visible: dialog.step === "pairing"
            indeterminate: true
        }
        Text {
            Layout.fillWidth: true
            text: dialog.step === "pairing" ? i18nc("@info", "Connecting… If your device shows a code or asks for a PIN, check the notification on this PC.")
                : dialog.step === "done" ? i18nc("@info", "Your device is ready to go!")
                : (dialog.failure !== "" ? dialog.failure : i18nc("@info", "That didn't work. Make sure your device is still ready to connect, then try again."))
            font: Theme.body
            color: Theme.text
            wrapMode: Text.Wrap
        }
    }

    // a choice of the first step
    component KindButton: T.AbstractButton {
        id: kind
        property string iconName
        property string title
        Layout.fillWidth: true
        implicitHeight: Math.max(72, implicitContentHeight + topPadding + bottomPadding)
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        padding: Theme.cardPadding
        Accessible.role: Accessible.Button
        Accessible.name: title
        Accessible.description: text
        background: Item {
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusControl
                color: kind.down ? Theme.controlPressed : kind.hovered ? Theme.controlHover : Theme.control
                border.width: 1
                border.color: Theme.strokeControl
            }
            FocusRing { shown: kind.visualFocus }
        }
        contentItem: RowLayout {
            spacing: Theme.s5
            opacity: kind.enabled ? 1 : 0.4
            Kirigami.Icon {
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                source: kind.iconName
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text {
                    text: kind.title
                    font: Theme.bodyStrong
                    color: Theme.text
                }
                Text {
                    Layout.fillWidth: true
                    text: kind.text
                    font: Theme.caption
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
