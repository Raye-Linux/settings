// SPDX-License-Identifier: GPL-3.0-or-later
// The navigation pane beside every page: who is signed in, "Find a
// setting", Home, then the categories. The chosen item has a soft fill
// and the accent pill, which slides to the next item instead of
// jumping. "expanded" is 300 px wide; "compact" shows the icons only
// (56 px) with the name as a tooltip, and its search icon asks the
// window to open the full pane.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

FocusScope {
    id: pane

    // "expanded" or "compact"
    property string mode: "expanded"
    property var model: typeof categories !== "undefined" ? categories : null
    readonly property bool compact: mode === "compact"
    // "home" or a category id
    readonly property string currentId: Router.atHome ? "home" : Router.categoryId
    readonly property alias searchField: search

    // Enter on the search text, and the compact pane's search icon
    signal searchRequested(string text)
    signal expandRequested()
    // an item was chosen (the overlay pane closes then)
    signal navigated()

    implicitWidth: compact ? 56 : 300

    Accessible.role: Accessible.Pane
    Accessible.name: i18nc("@title the list of categories", "Navigation")

    function focusSearch(text) {
        if (compact)
            expandRequested()
        else
            search.startTyping(text)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: Theme.s3
        spacing: Theme.s3

        UserCard {
            Layout.fillWidth: !pane.compact
            Layout.leftMargin: pane.compact ? (pane.width - width) / 2 : Theme.s3
            Layout.rightMargin: pane.compact ? 0 : Theme.s3
            mode: pane.compact ? "icon" : "pane"
            KeyNavigation.down: pane.compact ? searchButton : search
        }

        SearchField {
            id: search
            visible: !pane.compact
            compact: true
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s4
            Layout.rightMargin: Theme.s4
            Layout.bottomMargin: Theme.s2
            onShowAllResults: (text) => pane.searchRequested(text)
            onChosen: pane.navigated()
            KeyNavigation.down: list
        }

        NavButton {
            id: searchButton
            visible: pane.compact
            Layout.fillWidth: true
            label: i18n("Find a setting")
            glyph: "search"
            onClicked: pane.expandRequested()
            KeyNavigation.down: list
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: Theme.s1
            activeFocusOnTab: true
            keyNavigationEnabled: true
            boundsBehavior: Flickable.StopAtBounds
            currentIndex: {
                for (let i = 0; i < entries.length; ++i) {
                    if (entries[i].id === pane.currentId)
                        return i
                }
                return -1
            }
            T.ScrollBar.vertical: ThinScrollBar {}
            Accessible.role: Accessible.List
            Accessible.name: i18nc("@title the list of categories", "Categories")

            // Home, then the categories of the layout
            readonly property var entries: {
                const list = [{ id: "home", name: i18nc("@title the launch screen of Settings", "Home"),
                                icon: "org.raye.settings-home", fallback: "go-home" }]
                if (pane.model) {
                    for (let i = 0; i < pane.model.rowCount(); ++i) {
                        const index = pane.model.index(i, 0)
                        // CategoryModel roles: id, name, icon
                        list.push({
                            id: pane.model.data(index, Qt.UserRole + 1),
                            name: pane.model.data(index, Qt.UserRole + 2),
                            icon: pane.model.data(index, Qt.UserRole + 3),
                            fallback: "preferences-system"
                        })
                    }
                }
                return list
            }
            model: entries
            delegate: NavButton {
                required property var modelData
                readonly property string itemId: modelData.id
                width: ListView.view.width
                label: modelData.name
                iconName: modelData.icon
                iconFallback: modelData.fallback
                selected: pane.currentId === itemId
                onClicked: {
                    if (itemId === "home")
                        Router.goHome()
                    else
                        Router.navigate(itemId)
                    pane.navigated()
                }
            }

            // the pill: one for the list, it slides from item to item
            AccentPill {
                id: pill
                parent: list.contentItem
                z: 2
                x: Theme.s3 + 1
                readonly property Item target: list.currentItem
                visible: target !== null
                y: target ? target.y + (target.height - height) / 2 : 0
                Behavior on y {
                    enabled: pill.visible
                    NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter }
                }
            }
        }
    }

    // One item of the pane: 40 high, 6 px in from both sides, radius 6,
    // a 20 px glyph and the name (the name as a tooltip when compact).
    component NavButton: T.AbstractButton {
        id: button
        property string label
        property string iconName
        property string iconFallback
        property string glyph
        property bool selected: false

        implicitHeight: 40
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        leftInset: Theme.s3 - 2
        rightInset: Theme.s3 - 2
        Accessible.role: Accessible.PageTab
        Accessible.name: label
        Accessible.selected: selected
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        ToolTip {
            // beside the narrow list, not over the entry above
            x: button.width + 4
            y: Math.round((button.height - height) / 2)
            visible: pane.compact && button.hovered
            text: button.label
        }

        background: Item {
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusControl
                color: button.down ? Theme.subtlePressed
                     : button.selected ? Theme.subtleHover
                     : button.hovered ? Theme.subtleHover : "transparent"
                Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
            }
            FocusRing { shown: button.visualFocus }
        }
        contentItem: Item {
            Kirigami.Icon {
                id: icon
                visible: button.glyph === ""
                x: pane.compact ? (parent.width - width) / 2 : Theme.s4
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.iconSize
                height: Theme.iconSize
                source: button.iconName
                fallback: button.iconFallback
            }
            Glyph {
                visible: button.glyph !== ""
                name: button.glyph
                size: 16
                x: pane.compact ? (parent.width - width) / 2 : Theme.s4 + 2
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.text
            }
            Text {
                visible: !pane.compact
                x: Theme.s4 + Theme.iconSize + Theme.s4
                width: parent.width - x - Theme.s3
                anchors.verticalCenter: parent.verticalCenter
                text: button.label
                font: Theme.body
                color: Theme.text
                elide: Text.ElideRight
                horizontalAlignment: Text.AlignLeft
                Accessible.ignored: true
            }
        }
    }
}
