// SPDX-License-Identifier: GPL-3.0-or-later
// Clean up (Windows' "Temporary files"): what can go, what it frees,
// a box to tick for each. The trash and the restore points are not
// ticked to start with: they are the only things here a person may
// still want. System files ask for an administrator's password.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    // Storage's cleanupItems: [{ id, bytes, count, system }]
    property var items: []
    property bool estimating: false
    property var formatSize: (bytes) => ""
    // the ticked items, by id
    property var chosen: ({ thumbnails: true, apt: true, logs: true })

    signal cleanRequested(var items)

    readonly property var _ticked: items.filter(item => chosen[item.id] === true)
    readonly property real _total: _ticked.reduce((sum, item) => sum + Math.max(0, item.bytes), 0)
    readonly property bool _system: _ticked.some(item => item.system)

    title: i18nc("@title:window", "Clean up")
    primaryText: i18nc("@action:button", "Clean up")
    primaryEnabled: !estimating && _ticked.length > 0
    closeText: i18nc("@action:button", "Cancel")

    onAccepted: cleanRequested(_ticked.map(item => item.id))

    function nameOf(id) {
        switch (id) {
        case "trash": return i18nc("@option:check clean up", "Trash")
        case "thumbnails": return i18nc("@option:check clean up", "Thumbnails")
        case "apt": return i18nc("@option:check clean up", "Downloaded update files")
        case "logs": return i18nc("@option:check clean up", "Old system logs")
        case "snapshots": return i18nc("@option:check clean up", "Old restore points")
        }
        return id
    }

    function descriptionOf(id) {
        switch (id) {
        case "trash": return i18nc("@info clean up", "Files you deleted. They can't be brought back after this.")
        case "thumbnails": return i18nc("@info clean up", "Small previews of your pictures and videos. They come back when needed.")
        case "apt": return i18nc("@info clean up", "Copies of the updates already installed on this PC.")
        case "logs": return i18nc("@info clean up", "Records of what happened on this PC, kept for two weeks.")
        case "snapshots": return i18nc("@info clean up", "Every restore point except the newest one. You can't go back to them after this.")
        }
        return ""
    }

    function sizeOf(item) {
        if (item.bytes >= 0)
            return formatSize(item.bytes)
        if (item.id === "snapshots" && item.count >= 0)
            return i18ncp("@info number of restore points", "%1 restore point", "%1 restore points", item.count)
        return i18nc("@info a size that cannot be known yet", "Size unknown")
    }

    function toggle(id) {
        const next = Object.assign({}, chosen)
        next[id] = !next[id]
        chosen = next
    }

    Text {
        Layout.fillWidth: true
        Layout.preferredWidth: 452
        text: dialog.estimating ? i18nc("@info", "Looking for files you can delete…")
            : dialog.items.length === 0 ? i18nc("@info", "There's nothing to clean up right now.")
            : i18nc("@info %1 is a size", "Choose what to delete. You'll free up %1.", dialog.formatSize(dialog._total))
        font: Theme.body
        color: Theme.text
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignLeft
        lineHeightMode: Text.FixedHeight
        lineHeight: Theme.bodyLineHeight
    }

    ProgressBar {
        Layout.fillWidth: true
        visible: dialog.estimating
        indeterminate: true
    }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        visible: !dialog.estimating
        Repeater {
            model: dialog.items
            Item {
                id: row
                required property var modelData
                required property int index
                Layout.fillWidth: true
                implicitHeight: Math.max(56, texts.implicitHeight + 2 * Theme.s3)

                Rectangle {
                    visible: row.index > 0
                    width: parent.width
                    height: 1
                    color: Theme.divider
                }
                TapHandler { onTapped: box.toggle() }

                StorageCheckBox {
                    id: box
                    x: row.mirrored ? parent.width - width : 0
                    y: texts.y + Math.round((Theme.bodyLineHeight - 20) / 2) - (height - 20) / 2
                    text: ""
                    width: 20
                    spacing: 0
                    checked: dialog.chosen[row.modelData.id] === true
                    onToggled: dialog.toggle(row.modelData.id)
                    Accessible.name: dialog.nameOf(row.modelData.id)
                    Accessible.description: size.text
                }
                readonly property bool mirrored: LayoutMirroring.enabled
                ColumnLayout {
                    id: texts
                    x: row.mirrored ? size.width + Theme.s5 : 20 + Theme.s4
                    width: parent.width - 20 - Theme.s4 - size.width - Theme.s5
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: dialog.nameOf(row.modelData.id)
                        font: Theme.body
                        color: Theme.text
                        horizontalAlignment: Text.AlignLeft
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: dialog.descriptionOf(row.modelData.id)
                        font: Theme.caption
                        color: Theme.textSecondary
                        horizontalAlignment: Text.AlignLeft
                        wrapMode: Text.Wrap
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Theme.captionLineHeight
                    }
                }
                Text {
                    id: size
                    x: row.mirrored ? 0 : parent.width - width
                    y: texts.y
                    text: dialog.sizeOf(row.modelData)
                    font: Theme.bodyStrong
                    color: Theme.text
                    Accessible.ignored: true
                }
            }
        }
    }

    // a quiet line about the password, only when it will be asked
    RowLayout {
        Layout.fillWidth: true
        visible: !dialog.estimating && dialog._system
        spacing: Theme.s3
        StatusGlyph {
            Layout.alignment: Qt.AlignTop
            status: "info"
        }
        Text {
            Layout.fillWidth: true
            text: i18nc("@info", "System files need root access.")
            font: Theme.caption
            color: Theme.textSecondary
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignLeft
        }
    }
}
