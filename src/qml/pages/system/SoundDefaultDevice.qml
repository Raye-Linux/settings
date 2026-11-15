// SPDX-License-Identifier: GPL-3.0-or-later
// The device apps use now, out of a SoundDevices model, as properties
// that follow it: its row, name, volume, mute and balance.
import QtQml
import QtQml.Models

QtObject {
    id: current

    // a SoundDevices model
    property var devices

    readonly property var entry: {
        void revision
        for (let i = 0; i < rows.count; ++i) {
            const o = rows.objectAt(i)
            if (o && o.isDefault)
                return o
        }
        return null
    }
    readonly property int row: entry ? entry.row : -1
    readonly property string name: entry ? entry.name : ""
    readonly property string title: entry ? entry.title : ""
    readonly property int volume: entry ? entry.volume : 0
    readonly property bool muted: entry ? entry.muted : false
    readonly property int balance: entry ? entry.balance : 0
    readonly property bool hasBalance: entry ? entry.hasBalance : false

    property int revision: 0

    readonly property Instantiator rows: Instantiator {
        model: current.devices
        onObjectAdded: current.revision++
        onObjectRemoved: current.revision++
        delegate: QtObject {
            required property int index
            required property bool isDefault
            required property string name
            required property string title
            required property int volume
            required property bool muted
            required property int balance
            required property bool hasBalance
            readonly property int row: index
            onIsDefaultChanged: current.revision++
        }
    }
}
