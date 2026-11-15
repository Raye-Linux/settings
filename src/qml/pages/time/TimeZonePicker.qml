// SPDX-License-Identifier: GPL-3.0-or-later
// Every time zone, by offset, under a search box that finds a zone by
// its city, region, name or "UTC+1". Opens over the time zone field;
// Up/Down move, Enter takes the highlighted zone, Esc closes.
import QtQuick
import QtQuick.Templates as T
import org.raye.settings

Flyout {
    id: picker

    // the zone in use ("Europe/Paris")
    property string current
    // the field it opens from
    property Item anchorItem

    signal picked(string id)

    parent: T.Overlay.overlay
    modal: true
    focus: true
    dim: false
    closePolicy: T.Popup.CloseOnEscape | T.Popup.CloseOnPressOutside
    width: Math.min(440, parent ? parent.width - 2 * Theme.s5 : 440)
    height: Math.min(460, parent ? parent.height - 2 * Theme.s5 : 460)
    padding: Theme.s2
    // under the field when there is room, else above it
    x: {
        if (!anchorItem || !parent)
            return 0
        const p = anchorItem.mapToItem(parent, 0, 0)
        return Math.max(Theme.s5, Math.min(p.x + anchorItem.width - width, parent.width - width - Theme.s5))
    }
    y: {
        if (!anchorItem || !parent)
            return 0
        const p = anchorItem.mapToItem(parent, 0, 0)
        const below = p.y + anchorItem.height + 4
        return below + height <= parent.height - Theme.s5 ? below : Math.max(Theme.s5, p.y - height - 4)
    }

    onAboutToShow: {
        search.text = ""
        zones.filter = ""
        list.currentIndex = Math.max(0, zones.indexOf(picker.current))
        list.positionViewAtIndex(list.currentIndex, ListView.Center)
        search.forceActiveFocus()
    }

    function choose(row) {
        if (row < 0 || row >= list.count)
            return
        const id = zones.data(zones.index(row, 0), Qt.UserRole + 1)
        picker.close()
        if (id !== picker.current)
            picker.picked(id)
    }

    TimeZoneModel { id: zones }

    // a short pause after typing, so the list does not jump at every key
    Timer {
        id: searchDelay
        interval: 120
        onTriggered: {
            zones.filter = search.text
            list.currentIndex = zones.count > 0 ? 0 : -1
            list.positionViewAtBeginning()
        }
    }

    contentItem: Column {
        spacing: Theme.s2
        Accessible.role: Accessible.Dialog
        Accessible.name: i18n("Time zone")

        TextField {
            id: search
            width: parent.width
            placeholderText: i18n("Search for a city or a time zone")
            Accessible.name: i18n("Search for a city or a time zone")
            onTextEdited: searchDelay.restart()
            Keys.onDownPressed: list.incrementCurrentIndex()
            Keys.onUpPressed: list.decrementCurrentIndex()
            Keys.onReturnPressed: { searchDelay.stop(); zones.filter = search.text; picker.choose(Math.max(0, list.currentIndex)) }
            Keys.onEnterPressed: { searchDelay.stop(); zones.filter = search.text; picker.choose(Math.max(0, list.currentIndex)) }
        }

        Item {
            width: parent.width
            height: parent.height - search.height - parent.spacing

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: zones
                boundsBehavior: Flickable.StopAtBounds
                highlightMoveDuration: 0
                keyNavigationEnabled: true
                T.ScrollBar.vertical: ThinScrollBar {}
                Accessible.role: Accessible.List
                Accessible.name: i18n("Time zones")

                delegate: T.ItemDelegate {
                    id: row
                    required property int index
                    required property string zoneId
                    required property string label
                    required property string description
                    readonly property bool chosen: zoneId === picker.current
                    width: ListView.view.width
                    implicitHeight: description !== "" ? 52 : Theme.popupItemHeight
                    leftPadding: Theme.s4
                    rightPadding: Theme.s3
                    hoverEnabled: true
                    highlighted: ListView.isCurrentItem
                    Accessible.role: Accessible.ListItem
                    Accessible.name: label + (description !== "" ? ", " + description : "")
                    Accessible.selected: chosen
                    onClicked: picker.choose(index)

                    contentItem: Column {
                        spacing: 2
                        Text {
                            width: parent.width
                            text: row.label
                            font: row.chosen ? Theme.bodyStrong : Theme.body
                            color: Theme.text
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                        Text {
                            width: parent.width
                            visible: row.description !== ""
                            text: row.description
                            font: Theme.caption
                            color: Theme.textSecondary
                            elide: Text.ElideRight
                            Accessible.ignored: true
                        }
                    }
                    background: Item {
                        Rectangle {
                            anchors.fill: parent
                            anchors.margins: 2
                            radius: 4
                            color: row.down ? Theme.subtlePressed
                                 : row.highlighted || row.hovered ? Theme.subtleHover
                                 : row.chosen ? Theme.subtleSelected : "transparent"
                        }
                        AccentPill {
                            visible: row.chosen
                            x: 4
                            anchors.verticalCenter: parent.verticalCenter
                            height: 16
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.s5
                visible: zones.count === 0
                text: i18n("No time zone matches. Try the name of a city near you.")
                font: Theme.body
                color: Theme.textSecondary
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
    }
}
