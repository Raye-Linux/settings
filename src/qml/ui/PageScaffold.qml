// SPDX-License-Identifier: GPL-3.0-or-later
// The frame of every page: the title as a breadcrumb (with a back arrow
// when there is somewhere to go back to), an optional banner, then the
// cards in a scrolling column, 4 px apart, at most 1000 px wide and
// left aligned. A link to "page#anchor" scrolls to the row whose
// searchId is "anchor" and flashes it.
//
//   PageScaffold {
//       crumbs: [{ text: i18n("System"), target: "system" }, { text: i18n("Display") }]
//       SectionHeader { text: i18n("Brightness & color"); first: true }
//       SliderCard { title: i18n("Brightness"); searchId: "brightness" }
//   }
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

Item {
    id: page

    // [{ text, target }], the last being this page; or just a title
    property var crumbs: []
    property string title
    // show the back arrow before the title
    property bool showBack: Router.canGoBack
    // a band under the title (device banner, connection status)
    property Component banner
    // follow the router's #anchor requests
    property bool followAnchors: true
    property alias flickable: flick
    default property alias content: column.data

    // a parent crumb was clicked: by default the router goes there
    signal crumbActivated(string target)
    signal backClicked()

    readonly property var _crumbs: crumbs.length > 0 ? crumbs : [{ text: title }]

    Accessible.role: Accessible.Pane
    Accessible.name: _crumbs[_crumbs.length - 1].text

    onCrumbActivated: (target) => { if (target !== "") Router.navigate(target) }
    onBackClicked: Router.back()

    // Scroll the row with this searchId into view and flash it.
    // Returns false when the page has no such row.
    //
    // It searches `inner`, not `column`: the banner is a sibling of the
    // column, so a row inside it (AboutBanner's "Rename this PC") was
    // never found and the search hit did nothing at all.
    function scrollToAnchor(anchor) {
        const row = findRow(inner, anchor)
        if (!row)
            return false
        // A closed Expander keeps its rows in the tree -- its body is an
        // Item of height 0 with clip: true -- so findRow finds a row
        // nobody can see, the page scrolls to the closed card and flashes
        // something invisible. Open the groups above the row first.
        _anchoredRow = row
        if (openGroupsAbove(row, anchor))
            settle.restart() // their height animates: scroll again after
        scrollToRow(row)
        if (typeof row.flash === "function")
            row.flash()
        return true
    }

    // Open every closed group between this row and the page. An Expander
    // publishes its own searchId through its header, which is its child:
    // asking for the group itself must not open it.
    function openGroupsAbove(row, anchor) {
        let opened = false
        for (let p = row.parent; p && p !== inner; p = p.parent) {
            if (p.expanded === false && p.searchId !== anchor) {
                p.expanded = true
                opened = true
            }
        }
        return opened
    }

    function scrollToRow(row) {
        const y = row.mapToItem(inner, 0, 0).y
        const target = Math.max(0, Math.min(y - Theme.s7, flick.contentHeight - flick.height))
        scrollAnimation.to = Math.max(0, target)
        scrollAnimation.restart()
    }

    // The row the last scrollToAnchor aimed at, so the second pass knows
    // where to go once the groups have finished opening.
    property Item _anchoredRow: null

    Timer {
        id: settle
        interval: Theme.durationNormal + 30
        onTriggered: if (page._anchoredRow !== null) page.scrollToRow(page._anchoredRow)
    }

    function findRow(item, anchor) {
        if (!item || anchor === "")
            return null
        if (item.searchId !== undefined && item.searchId === anchor)
            return item
        for (let i = 0; i < item.children.length; ++i) {
            const found = findRow(item.children[i], anchor)
            if (found)
                return found
        }
        return null
    }

    // every searchId on the page, in order (the search index checks them)
    function searchIds() {
        const ids = []
        function walk(item) {
            if (item.searchId !== undefined && item.searchId !== "")
                ids.push(item.searchId)
            for (let i = 0; i < item.children.length; ++i)
                walk(item.children[i])
        }
        walk(column)
        return ids
    }

    Connections {
        target: page.followAnchors ? Router : null
        function onAnchorRequested(anchor) { Qt.callLater(page.scrollToAnchor, anchor) }
    }

    Row {
        id: headerRow
        anchors.left: parent.left
        anchors.leftMargin: Theme.contentPadding
        y: Theme.s7
        width: Math.min(Theme.contentMaxWidth, page.width - 2 * Theme.contentPadding)
        height: Theme.titleLineHeight
        spacing: Theme.s3

        T.AbstractButton {
            id: backButton
            visible: page.showBack
            width: 40
            height: 36
            anchors.verticalCenter: parent.verticalCenter
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: i18nc("@action:button go to the previous page", "Back")
            onClicked: page.backClicked()
            background: Item {
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusControl
                    color: backButton.down ? Theme.subtlePressed : backButton.hovered ? Theme.subtleHover : "transparent"
                }
                FocusRing { shown: backButton.visualFocus }
            }
            contentItem: Item {
                Glyph {
                    anchors.centerIn: parent
                    name: "arrow-left"
                    size: 16
                    rotation: page.LayoutMirroring.enabled ? 180 : 0
                    color: Theme.text
                }
            }
        }
        Breadcrumb {
            id: breadcrumb
            width: parent.width - (backButton.visible ? backButton.width + parent.spacing : 0)
            height: parent.height
            crumbs: page._crumbs
            onActivated: (target) => page.crumbActivated(target)
        }
    }

    Flickable {
        id: flick
        WheelScroll { target: flick }
        anchors.fill: parent
        anchors.topMargin: headerRow.y + headerRow.height + Theme.s7
        contentWidth: width
        contentHeight: inner.implicitHeight + Theme.s8
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        T.ScrollBar.vertical: ThinScrollBar {}

        NumberAnimation {
            id: scrollAnimation
            target: flick
            property: "contentY"
            duration: Theme.durationNormal
            easing.type: Theme.easingEnter
        }

        ColumnLayout {
            id: inner
            anchors.left: parent.left
            anchors.leftMargin: Theme.contentPadding
            width: Math.min(Theme.contentMaxWidth, flick.width - 2 * Theme.contentPadding)
            spacing: Theme.cardSpacing

            Loader {
                Layout.fillWidth: true
                Layout.bottomMargin: active ? Theme.s7 - Theme.cardSpacing : 0
                active: page.banner !== null
                sourceComponent: page.banner
                visible: active
            }
            ColumnLayout {
                id: column
                Layout.fillWidth: true
                spacing: Theme.cardSpacing
            }
        }
    }
}
