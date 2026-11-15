// SPDX-License-Identifier: GPL-3.0-or-later
// A Plasma settings module shown inside a Raye page (no page of the
// shipped layout uses one any more; a test keeps it that way): the Raye
// breadcrumb on top, never the module's own KDE title, the module on a
// card, and an Apply / Cancel bar that slides up when there is
// something to save. A module made of widgets opens in its own window;
// the page says so and can open it again.
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.raye.settings

Item {
    id: host

    // the module to show ("kcm_gamecontroller")
    property string kcm
    // [{ text, target }], the last being this page
    property var crumbs: []
    property var modules: typeof moduleHost !== "undefined" ? moduleHost : null

    readonly property bool external: modules !== null && modules.external && modules.current === kcm
    readonly property bool failed: modules !== null && modules.error !== "" && !external && !modules.content

    Accessible.role: Accessible.Pane
    Accessible.name: crumbs.length > 0 ? crumbs[crumbs.length - 1].text : ""

    Component.onCompleted: if (modules && kcm !== "") modules.open(kcm)
    Component.onDestruction: if (modules && modules.current === kcm) modules.close()

    Row {
        id: headerRow
        anchors.left: parent.left
        anchors.leftMargin: Theme.contentPadding
        y: Theme.s7
        width: parent.width - 2 * Theme.contentPadding
        height: Theme.titleLineHeight
        spacing: Theme.s3

        T.AbstractButton {
            id: backButton
            visible: Router.canGoBack
            width: 40
            height: 36
            anchors.verticalCenter: parent.verticalCenter
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: i18nc("@action:button go to the previous page", "Back")
            onClicked: Router.back()
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
                    rotation: host.LayoutMirroring.enabled ? 180 : 0
                    color: Theme.text
                }
            }
        }
        Breadcrumb {
            width: parent.width - (backButton.visible ? backButton.width + parent.spacing : 0)
            height: parent.height
            crumbs: host.crumbs
            onActivated: (target) => { if (target !== "") Router.navigate(target) }
        }
    }

    // the module, on a card of the layer colour
    Rectangle {
        id: surface
        anchors.top: headerRow.bottom
        anchors.topMargin: Theme.s7
        anchors.left: parent.left
        anchors.leftMargin: Theme.contentPadding
        anchors.right: parent.right
        anchors.rightMargin: Theme.contentPadding
        anchors.bottom: applyBar.top
        anchors.bottomMargin: Theme.s4
        radius: Theme.radiusOverlay
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder
        visible: !host.external && !host.failed
        clip: true

        Item {
            id: holder
            anchors.fill: parent
            anchors.margins: 1

            function attach() {
                const ui = host.modules ? host.modules.content : null
                if (!ui || host.modules.current !== host.kcm)
                    return
                ui.parent = holder
                ui.anchors.fill = holder
                // the module's own title belongs to KDE's window: the
                // breadcrumb above says where the user is
                if (ui.title !== undefined)
                    ui.title = ""
            }
            Connections {
                target: host.modules
                function onContentChanged() { holder.attach() }
            }
            Component.onCompleted: attach()
        }
    }

    // a module made of widgets: it opened in a window of its own
    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.contentPadding, 480)
        visible: host.external
        iconName: "org.raye.settings-status-info"
        title: i18n("These settings opened in their own window")
        message: i18n("Look for the window on the taskbar.")
        Button {
            text: i18nc("@action:button", "Open again")
            onClicked: host.modules.openExternal(host.kcm)
        }
    }

    EmptyState {
        anchors.centerIn: parent
        width: Math.min(parent.width - 2 * Theme.contentPadding, 480)
        visible: host.failed
        iconName: "org.raye.settings-status-warning"
        title: i18n("This page can't be shown")
        message: host.modules ? host.modules.error : ""
    }

    // Apply / Cancel: slides up from the bottom when something changed
    Item {
        id: applyBar
        readonly property bool shown: host.modules !== null && host.modules.needsSave
                                      && host.modules.current === host.kcm
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: shown ? barRow.implicitHeight + 2 * Theme.s4 : 0
        clip: true
        Behavior on height { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }

        Rectangle {
            anchors.fill: parent
            color: Theme.flyoutFooter
            Rectangle { width: parent.width; height: 1; color: Theme.divider }
        }
        RowLayout {
            id: barRow
            anchors.right: parent.right
            anchors.rightMargin: Theme.contentPadding
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.s3
            Text {
                text: i18n("You have changes that are not applied yet.")
                font: Theme.caption
                color: Theme.textSecondary
                Layout.rightMargin: Theme.s4
            }
            Button {
                text: i18nc("@action:button undo the changes", "Cancel")
                onClicked: host.modules.reset()
            }
            Button {
                accent: true
                text: i18nc("@action:button", "Apply")
                onClicked: host.modules.save()
            }
        }
    }
}
