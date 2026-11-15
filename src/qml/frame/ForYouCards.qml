// SPDX-License-Identifier: GPL-3.0-or-later
// The cards under the tiles of the launch screen, the useful part of
// Windows' Home page without the ads: Your device, Bluetooth devices
// and Make it yours. Each one can be hidden from its "…" menu. Three
// side by side on a wide window, one per line on a narrow one.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.raye.settings

GridLayout {
    id: cards

    property int columnCount: 3
    readonly property var hidden: FrameSettings.hiddenCards
    readonly property bool hasBluetooth: Router.has("bluetooth")
    readonly property int shownCount: (hidden.indexOf("your-device") < 0 ? 1 : 0)
                                      + (hasBluetooth && hidden.indexOf("bluetooth-devices") < 0 ? 1 : 0)
                                      + (hidden.indexOf("make-it-yours") < 0 ? 1 : 0)

    columns: Math.max(1, Math.min(columnCount, shownCount))
    // the same width for every card of a line
    readonly property real cellWidth: (width - (columns - 1) * columnSpacing) / columns
    columnSpacing: Theme.s4
    rowSpacing: Theme.s4

    // --- Your device ---------------------------------------------------
    ForYouCard {
        id: deviceCard
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: cards.cellWidth
        visible: cards.hidden.indexOf(cardId) < 0
        cardId: "your-device"
        searchId: "your-device"
        title: i18nc("@title card on the launch screen", "Your device")
        onHideRequested: FrameSettings.hideCard(cardId)

        readonly property var device: FrameSettings.device

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s4
            Kirigami.Icon {
                Layout.alignment: Qt.AlignTop
                implicitWidth: 64
                implicitHeight: 64
                source: "org.raye.settings-chassis-" + (deviceCard.device.chassis || "desktop")
                fallback: "computer"
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.s1
                Text {
                    Layout.fillWidth: true
                    text: deviceCard.device.model || i18nc("@label no model name known", "This PC")
                    font: Theme.bodyStrong
                    color: Theme.text
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    horizontalAlignment: Text.AlignLeft
                }
                Text {
                    Layout.fillWidth: true
                    text: [deviceCard.device.processor, deviceCard.device.memory].filter(s => s && s.length > 0).join(" · ")
                    visible: text.length > 0
                    font: Theme.caption
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    horizontalAlignment: Text.AlignLeft
                }
            }
        }
        ProgressBar {
            Layout.fillWidth: true
            visible: deviceCard.device.storageTotal > 0
            from: 0
            to: Math.max(1, deviceCard.device.storageTotal || 1)
            value: (deviceCard.device.storageTotal || 0) - (deviceCard.device.storageFree || 0)
            status: deviceCard.device.storageTotal > 0
                    && deviceCard.device.storageFree / deviceCard.device.storageTotal < 0.1 ? "caution" : ""
            Accessible.name: storageText.text
        }
        Text {
            id: storageText
            Layout.fillWidth: true
            visible: text.length > 0
            text: deviceCard.device.storageText || ""
            font: Theme.caption
            color: Theme.textSecondary
            textFormat: Text.PlainText
            horizontalAlignment: Text.AlignLeft
        }
        HyperlinkButton {
            font: Theme.captionStrong
            text: i18nc("@action:button opens System > About", "View details")
            onClicked: Router.navigate("system/about")
        }
    }

    // --- Bluetooth devices: its own file, so a missing Bluetooth stack
    // costs this card only ------------------------------------------------
    Loader {
        id: bluetoothLoader
        Layout.alignment: Qt.AlignTop
        Layout.fillWidth: true
        Layout.preferredWidth: cards.cellWidth
        visible: active && status === Loader.Ready
        active: cards.hasBluetooth && cards.hidden.indexOf("bluetooth-devices") < 0
        source: Qt.resolvedUrl("BluetoothDevicesCard.qml")
        Connections {
            target: bluetoothLoader.item
            ignoreUnknownSignals: true
            function onHideRequested() { FrameSettings.hideCard("bluetooth-devices") }
        }
    }

    // --- Make it yours ---------------------------------------------------
    ForYouCard {
        id: looksCard
        Layout.alignment: Qt.AlignTop
        Layout.preferredWidth: cards.cellWidth
        visible: cards.hidden.indexOf(cardId) < 0
        cardId: "make-it-yours"
        searchId: "make-it-yours"
        title: i18nc("@title card on the launch screen", "Make it yours")
        onHideRequested: FrameSettings.hideCard(cardId)

        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s3
            Repeater {
                model: FrameSettings.looks
                delegate: LookThumbnail {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.maximumWidth: 160
                    name: modelData.name
                    preview: modelData.preview
                    current: modelData.current
                    night: modelData.id.indexOf("night") >= 0
                    onClicked: FrameSettings.applyLook(modelData.id)
                }
            }
        }
        HyperlinkButton {
            font: Theme.captionStrong
            text: i18nc("@action:button opens Personalization", "Browse colors and backgrounds")
            onClicked: Router.navigate("personalization")
        }
    }
}
