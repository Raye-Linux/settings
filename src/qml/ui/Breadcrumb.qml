// SPDX-License-Identifier: GPL-3.0-or-later
// The page title as a path: "System › Display". Parents are grey and
// clickable, the page itself is in full colour. When the path is too
// long the first parents fold into "…".
//
//   Breadcrumb { crumbs: [{ text: "System", target: "system" }, { text: "Display" }] }
import QtQuick
import QtQuick.Templates as T

Item {
    id: crumbsBar

    // [{ text, target }]: the last one is the page shown
    property var crumbs: []
    property font font: Theme.title

    // a parent was clicked; its target ("system")
    signal activated(string target)

    readonly property real _chevron: 14
    readonly property real _gap: Theme.s4
    // how many crumbs from the start are folded into "…"
    readonly property int folded: {
        const n = crumbs.length
        let total = 0
        for (let i = 0; i < n; ++i)
            total += metrics.advanceWidth(crumbs[i].text) + (i > 0 ? _chevron + 2 * _gap : 0)
        let hidden = 0
        const ellipsis = metrics.advanceWidth("…") + _chevron + 2 * _gap
        while (hidden < n - 1 && total + (hidden > 0 ? ellipsis : 0) > width) {
            total -= metrics.advanceWidth(crumbs[hidden].text) + _chevron + 2 * _gap
            ++hidden
        }
        return hidden
    }
    readonly property var _shown: {
        const list = []
        if (folded > 0)
            list.push({ text: "…", target: crumbs[folded - 1].target || "", folded: true })
        for (let i = folded; i < crumbs.length; ++i)
            list.push({ text: crumbs[i].text, target: crumbs[i].target || "", folded: false })
        return list
    }

    implicitWidth: row.implicitWidth
    implicitHeight: Theme.titleLineHeight
    Accessible.role: Accessible.Heading
    Accessible.name: crumbs.length > 0 ? crumbs[crumbs.length - 1].text : ""

    FontMetrics { id: metrics; font: crumbsBar.font }

    Row {
        id: row
        // starts at the right in a right-to-left layout
        x: crumbsBar.LayoutMirroring.enabled ? parent.width - width : 0
        height: parent.height
        spacing: crumbsBar._gap

        Repeater {
            model: crumbsBar._shown
            delegate: Row {
                id: step
                required property var modelData
                required property int index
                readonly property bool last: index === crumbsBar._shown.length - 1
                height: parent.height
                spacing: crumbsBar._gap

                Glyph {
                    visible: step.index > 0
                    anchors.verticalCenter: parent.verticalCenter
                    name: crumbsBar.LayoutMirroring.enabled ? "chevron-left" : "chevron-right"
                    size: crumbsBar._chevron
                    weight: 1.75
                    color: Theme.textSecondary
                }
                T.AbstractButton {
                    id: crumb
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: !step.last
                    hoverEnabled: true
                    focusPolicy: step.last ? Qt.NoFocus : Qt.StrongFocus
                    padding: 0
                    implicitWidth: implicitContentWidth
                    implicitHeight: implicitContentHeight
                    Accessible.role: step.last ? Accessible.StaticText : Accessible.Link
                    Accessible.name: step.modelData.folded ? i18nc("@action:button hidden parent pages", "More") : step.modelData.text
                    Keys.onReturnPressed: crumb.clicked()
                    onClicked: crumbsBar.activated(step.modelData.target)
                    contentItem: Text {
                        text: step.modelData.text
                        font.family: crumbsBar.font.family
                        font.pointSize: crumbsBar.font.pointSize
                        font.weight: crumbsBar.font.weight
                        color: step.last ? Theme.text : crumb.down ? Theme.textTertiary : Theme.textSecondary
                        font.underline: crumb.hovered
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Theme.titleLineHeight
                        verticalAlignment: Text.AlignVCenter
                        Accessible.ignored: true
                    }
                    background: Item {
                        FocusRing { shown: crumb.visualFocus; radius: 4 }
                    }
                    HoverHandler { cursorShape: step.last ? Qt.ArrowCursor : Qt.PointingHandCursor }
                }
            }
        }
    }
}
