// SPDX-License-Identifier: GPL-3.0-or-later
// The Settings root. On the launch screen it is the launch screen
// alone; anywhere else, the navigation pane on the left and the page on
// the layer card beside it. Below 1008 px the pane shows its icons only,
// below 641 px it hides behind a menu button and opens over the page.
// The window itself is drawn over the blurred wallpaper when the
// compositor can blur, on the plain Raye background otherwise.
import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Templates as T
import org.raye.settings

QQC2.ApplicationWindow {
    id: root

    title: i18n("Settings")
    width: 1120
    height: 760
    minimumWidth: 520
    minimumHeight: 480
    visible: true
    color: "transparent"
    font: Theme.body

    // "expanded", "compact" or "minimal", Windows' breakpoints
    readonly property string paneMode: width >= 1008 ? "expanded" : width >= 641 ? "compact" : "minimal"
    property bool overlayOpen: false

    // Called with what a second raye-settings asked for. The router has
    // already moved for a page it knows; a module it does not know goes
    // to KDE's own window, as kcmshell6 would have shown it.
    function openModule(target) {
        const id = String(target || "")
        if (id === "" || Router.resolve(id) !== "")
            return
        if (typeof moduleHost !== "undefined")
            moduleHost.openExternal(id)
    }

    // the launch screen shows until a search asks for its results page
    readonly property bool showingHome: Router.atHome && content.searchQuery === ""

    function showSearchResults(text) {
        overlayOpen = false
        content.searchQuery = text
    }

    function focusSearch(text) {
        if (showingHome) {
            home.focusSearch(text)
        } else if (paneMode === "expanded") {
            pane.focusSearch(text)
        } else {
            overlayOpen = true
            overlayPane.searchField.startTyping(text)
        }
    }

    function goBack() {
        if (overlayOpen) {
            overlayOpen = false
            return
        }
        // the results page goes back to where the search started
        if (content.searchQuery !== "") {
            content.searchQuery = ""
            return
        }
        Router.back()
    }

    Component.onCompleted: {
        FrameSettings.restoreWindow(root)
        if (typeof startModule !== "undefined")
            openModule(startModule)
    }
    onClosing: FrameSettings.saveWindow(root)

    WindowEffects {
        id: effects
        window: root
        blurBehind: true
    }

    background: Rectangle {
        color: effects.translucent ? Theme.backdrop : Theme.backdropSolid
    }

    // --- shortcuts ----------------------------------------------------------
    Shortcut { sequences: [StandardKey.Find, "Ctrl+E", "F3"]; onActivated: root.focusSearch("") }
    Shortcut { sequence: "Alt+Left"; onActivated: root.goBack() }
    Shortcut { sequence: "Alt+Right"; onActivated: Router.forward() }
    Shortcut { sequences: ["Ctrl+W", StandardKey.Quit]; onActivated: root.close() }
    Shortcut {
        sequence: "Backspace"
        // only when no field is taking the key
        enabled: !(root.activeFocusItem && root.activeFocusItem.cursorPosition !== undefined)
        onActivated: root.goBack()
    }
    Shortcut { sequence: "Esc"; enabled: root.overlayOpen; onActivated: root.overlayOpen = false }

    // the mouse's back and forward buttons
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.BackButton | Qt.ForwardButton
        z: 100
        propagateComposedEvents: true
        onPressed: (mouse) => {
            if (mouse.button === Qt.BackButton)
                root.goBack()
            else if (mouse.button === Qt.ForwardButton)
                Router.forward()
        }
    }

    // --- the launch screen ------------------------------------------------
    HomeView {
        id: home
        anchors.fill: parent
        visible: opacity > 0
        opacity: root.showingHome ? 1 : 0
        enabled: root.showingHome
        focus: root.showingHome
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingFast } }
        onSearchRequested: (text) => root.showSearchResults(text)
    }

    // --- pane and page --------------------------------------------------------
    Item {
        id: twoColumns
        anchors.fill: parent
        visible: opacity > 0
        opacity: root.showingHome ? 0 : 1
        enabled: !root.showingHome
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingFast } }

        NavPane {
            id: pane
            visible: root.paneMode !== "minimal"
            mode: root.paneMode === "compact" ? "compact" : "expanded"
            width: implicitWidth
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            onSearchRequested: (text) => root.showSearchResults(text)
            onExpandRequested: {
                root.overlayOpen = true
                overlayPane.searchField.startTyping("")
            }
            Behavior on width { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }
        }

        // the menu button that stands for the pane on a narrow window
        T.AbstractButton {
            id: menuButton
            visible: root.paneMode === "minimal"
            x: Theme.s2
            y: Theme.s2
            width: 40
            height: 36
            z: 3
            hoverEnabled: true
            focusPolicy: Qt.StrongFocus
            Accessible.role: Accessible.Button
            Accessible.name: i18nc("@action:button show the list of categories", "Open navigation")
            onClicked: root.overlayOpen = true
            background: Item {
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radiusControl
                    color: menuButton.down ? Theme.subtlePressed : menuButton.hovered ? Theme.subtleHover : "transparent"
                }
                FocusRing { shown: menuButton.visualFocus }
            }
            contentItem: Item {
                Column {
                    anchors.centerIn: parent
                    spacing: 4
                    Repeater {
                        model: 3
                        Rectangle { width: 16; height: 1.5; radius: 1; color: Theme.text }
                    }
                }
            }
        }

        // the layer card the pages sit on
        Item {
            id: layer
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.right: parent.right
            anchors.left: pane.visible ? pane.right : parent.left
            anchors.topMargin: root.paneMode === "minimal" ? menuButton.y + menuButton.height : 0

            Rectangle {
                anchors.fill: parent
                // the top-left corner is round, the others meet the window edge
                anchors.rightMargin: -radius
                anchors.bottomMargin: -radius
                radius: root.paneMode === "minimal" ? 0 : Theme.radiusOverlay
                color: effects.translucent ? Theme.layer : Theme.layerSolid
                border.width: root.paneMode === "minimal" ? 0 : 1
                border.color: Theme.strokeSurface
            }

            ContentHost {
                id: content
                anchors.fill: parent
                clip: true
                focus: !root.showingHome
            }
        }
    }

    // --- the pane over the page on a narrow window ----------------------------
    Rectangle {
        id: smoke
        anchors.fill: parent
        z: 10
        color: Theme.smoke
        visible: opacity > 0
        opacity: root.overlayOpen ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationFast } }
        MouseArea {
            anchors.fill: parent
            enabled: root.overlayOpen
            onClicked: root.overlayOpen = false
        }
    }
    Rectangle {
        id: overlaySurface
        z: 11
        width: 300
        height: parent.height
        x: root.overlayOpen ? 0 : -width - 16
        visible: x > -width - 16
        color: Theme.flyout
        Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }
        Glow { radius: 0; spread: 16; color: Qt.alpha(Theme.shadow, Theme.night ? 0.5 : 0.18); visible: root.overlayOpen }
        Rectangle { anchors.fill: parent; color: Theme.flyout }

        NavPane {
            id: overlayPane
            anchors.fill: parent
            mode: "expanded"
            focus: root.overlayOpen
            onSearchRequested: (text) => root.showSearchResults(text)
            onNavigated: root.overlayOpen = false
        }
    }
}
