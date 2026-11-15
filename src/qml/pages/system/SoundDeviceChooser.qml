// SPDX-License-Identifier: GPL-3.0-or-later
// "Choose where to play sound" / "Choose a device for speaking or
// recording": every device as a card with a radio mark, side by side.
// Picking one makes it the one apps use, at once.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ColumnLayout {
    id: chooser

    // a SoundDevices model
    property var devices
    property string searchId
    property string emptyText

    function flash() {
        if (repeater.count > 0)
            repeater.itemAt(0).flash()
    }

    Layout.fillWidth: true
    spacing: Theme.cardSpacing

    Flow {
        Layout.fillWidth: true
        spacing: Theme.s3
        visible: repeater.count > 0
        readonly property int columns: width >= 700 ? 3 : width >= 440 ? 2 : 1
        readonly property real cardWidth: (width - (columns - 1) * spacing) / columns

        Repeater {
            id: repeater
            model: chooser.devices
            delegate: RadioCard {
                id: deviceCard
                required property int index
                required property var model
                width: parent.cardWidth
                autoExclusive: false
                checked: model.isDefault
                iconName: model.iconName
                description: model.subtitle
                title: model.title
                onClicked: {
                    // the server says which one is the default now
                    checked = Qt.binding(() => deviceCard.model.isDefault)
                    chooser.devices.setDefault(index)
                }
            }
        }
    }

    Card {
        visible: repeater.count === 0
        title: chooser.emptyText
        iconName: "org.raye.settings-status-info"
    }
}
