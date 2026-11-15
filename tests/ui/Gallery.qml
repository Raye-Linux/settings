// SPDX-License-Identifier: GPL-3.0-or-later
// Every Settings component in every state, on the content layer of the
// window, for the eye and for test_ui_gallery. Pages:
//   0 cards and expanders   1 controls   2 everything else
// "mode" is night or day, "rtl" mirrors everything. Hover, pressed and focus are shown at rest
// through each component's previewState.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

Rectangle {
    id: gallery

    property string mode: "night"
    property int page: 0
    property bool dialogOpen: false
    // right to left, as an Arabic or Hebrew session draws it
    property bool rtl: false

    LayoutMirroring.enabled: rtl
    LayoutMirroring.childrenInherit: true

    width: 1280
    height: 800
    color: Theme.backdropSolid

    Component.onCompleted: Theme.forceMode = mode
    onModeChanged: Theme.forceMode = mode

    readonly property int columnWidth: Math.min(600, (width - 3 * 32) / 2)

    // the content layer, as beside the navigation pane
    Rectangle {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.topMargin: 8
        radius: Theme.radiusOverlay
        color: Theme.layer
        border.width: 1
        border.color: Theme.strokeSurface
    }

    Flickable {
        anchors.fill: parent
        anchors.margins: 32
        contentHeight: pages.height
        clip: true

        Item {
            id: pages
            width: parent.width
            height: Math.max(page0.height, page1.height, page2.height)

            // ---------------------------------------------------- cards
            GridLayout {
                id: page0
                visible: gallery.page === 0
                width: parent.width
                columns: 2
                columnSpacing: 32
                rowSpacing: 4

                ColumnLayout {
                    Layout.preferredWidth: gallery.columnWidth
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.cardSpacing

                    SectionHeader { text: "Cards"; first: true }
                    Card {
                        objectName: "cardDesc"
                        iconName: "preferences-desktop-display"
                        title: "Card with a description"
                        description: "Body text on top, caption under it"
                        searchId: "card-desc"
                        Button { text: "Action" }
                    }
                    Card {
                        objectName: "cardPlain"
                        iconName: "preferences-desktop-display"
                        title: "Card without a description"
                        ToggleSwitch { objectName: "toggleInCard"; text: "Plain" }
                    }
                    NavCard { objectName: "navCard"; iconName: "preferences-system"; title: "Navigation card"; description: "Rest" }
                    NavCard { iconName: "preferences-system"; title: "Navigation card"; description: "Hover"; previewState: "hover" }
                    NavCard { iconName: "preferences-system"; title: "Navigation card"; description: "Pressed"; previewState: "pressed" }
                    NavCard { iconName: "preferences-system"; title: "Navigation card"; description: "Keyboard focus"; previewState: "focus" }
                    NavCard { iconName: "preferences-system"; title: "Navigation card"; description: "Disabled"; enabled: false }
                    LinkCard { iconName: "help-browser"; title: "Link card"; description: "Opens outside Settings" }
                    ToggleCard { objectName: "toggleCard"; iconName: "notifications"; title: "Toggle card"; description: "Click anywhere"; checked: true }
                    ToggleCard { iconName: "notifications"; title: "Toggle card"; description: "Off, hover"; previewState: "hover" }
                    ToggleCard { iconName: "notifications"; title: "Toggle card"; description: "Disabled"; enabled: false; checked: true }
                    ComboCard {
                        objectName: "comboCard"
                        iconName: "preferences-desktop-theme"
                        title: "Combo card"
                        description: "Pick one"
                        model: ["Raye Night", "Raye Day", "Automatic"]
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: gallery.columnWidth
                    Layout.alignment: Qt.AlignTop
                    spacing: Theme.cardSpacing

                    SectionHeader { text: "More cards"; first: true }
                    ButtonCard { iconName: "system-reboot"; title: "Button card"; description: "Accent button"; buttonText: "Restart now"; accent: true }
                    ButtonCard { iconName: "edit-rename"; title: "Button card"; buttonText: "Rename" }
                    SliderCard { objectName: "sliderCard"; iconName: "brightness-high"; title: "Slider card"; from: 0; to: 100; value: 70
                                 textFromValue: (v) => Math.round(v) + " %" }
                    InfoCard { objectName: "infoCard"; iconName: "computer"; title: "Device name"; value: "raye-laptop" }
                    Card {
                        objectName: "cardWrapped"
                        Layout.fillWidth: false
                        Layout.preferredWidth: 400
                        iconName: "preferences-desktop-display"
                        title: "Narrow card"
                        description: "Below 476 px the control goes under the text"
                        ComboBox { model: ["Wraps under"] }
                    }
                    Card {
                        title: "No icon, a link in the description"
                        description: "Text starts at 16 px. <a href=\"https://example.org\">Learn more</a>"
                        ToggleSwitch { checked: true; text: "No icon" }
                    }

                    SectionHeader { text: "Expanders" }
                    Expander {
                        iconName: "preferences-system-power"
                        title: "Expander, closed"
                        description: "Chevron down"
                    }
                    Expander {
                        objectName: "expander"
                        iconName: "preferences-system-power"
                        title: "Expander, open"
                        description: "Rows indented to the text"
                        expanded: true
                        ExpanderRow {
                            objectName: "expanderRow"
                            title: "When plugged in, turn off my screen after"
                            ComboBox { model: ["5 minutes", "10 minutes", "Never"] }
                        }
                        ExpanderRow {
                            title: "A clickable row"
                            description: "Hover"
                            clickable: true
                            actionGlyph: "chevron-right"
                            previewState: "hover"
                        }
                        ExpanderRow {
                            title: "Lid closed"
                            ToggleSwitch { text: "Lid" }
                        }
                    }
                }
            }

            // ------------------------------------------------- controls
            GridLayout {
                id: page1
                visible: gallery.page === 1
                width: parent.width
                columns: 2
                columnSpacing: 32
                rowSpacing: 4

                ColumnLayout {
                    Layout.preferredWidth: gallery.columnWidth
                    Layout.alignment: Qt.AlignTop
                    spacing: 12

                    SectionHeader { text: "Toggle switches: off, on × rest, hover, pressed, focus, disabled"; first: true }
                    Row {
                        spacing: 20
                        Repeater {
                            model: ["", "hover", "pressed", "focus", "disabled"]
                            ToggleSwitch {
                                required property string modelData
                                required property int index
                                objectName: index === 0 ? "toggleOff" : ""
                                text: "Off " + modelData
                                previewState: modelData === "disabled" ? "" : modelData
                                enabled: modelData !== "disabled"
                            }
                        }
                    }
                    Row {
                        spacing: 20
                        Repeater {
                            model: ["", "hover", "pressed", "focus", "disabled"]
                            ToggleSwitch {
                                required property string modelData
                                checked: true
                                text: "On " + modelData
                                previewState: modelData === "disabled" ? "" : modelData
                                enabled: modelData !== "disabled"
                            }
                        }
                    }

                    SectionHeader { text: "Buttons" }
                    Row {
                        spacing: 12
                        Repeater {
                            model: ["", "hover", "pressed", "focus", "disabled"]
                            Button {
                                required property string modelData
                                required property int index
                                objectName: index === 0 ? "button" : ""
                                text: modelData === "" ? "Rest" : modelData
                                previewState: modelData === "disabled" ? "" : modelData
                                enabled: modelData !== "disabled"
                            }
                        }
                    }
                    Row {
                        spacing: 12
                        Repeater {
                            model: ["", "hover", "pressed", "focus", "disabled"]
                            Button {
                                required property string modelData
                                accent: true
                                text: modelData === "" ? "Accent" : modelData
                                previewState: modelData === "disabled" ? "" : modelData
                                enabled: modelData !== "disabled"
                            }
                        }
                    }
                    Row {
                        spacing: 16
                        Repeater {
                            model: ["", "hover", "pressed", "focus", "disabled"]
                            HyperlinkButton {
                                required property string modelData
                                text: modelData === "" ? "View details" : modelData
                                previewState: modelData === "disabled" ? "" : modelData
                                enabled: modelData !== "disabled"
                            }
                        }
                    }

                    SectionHeader { text: "Combo boxes" }
                    Row {
                        spacing: 12
                        Repeater {
                            model: ["", "hover", "focus", "disabled"]
                            ComboBox {
                                required property string modelData
                                required property int index
                                objectName: index === 0 ? "combo" : ""
                                model: ["Best fit", "Fill", "Stretch"]
                                previewState: modelData === "disabled" ? "" : modelData
                                enabled: modelData !== "disabled"
                            }
                        }
                    }

                    SectionHeader { text: "Radio cards" }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        RadioCard { Layout.fillWidth: true; Layout.fillHeight: true; title: "Best power efficiency"; description: "Longer battery"; objectName: "radioCard" }
                        RadioCard { Layout.fillWidth: true; Layout.fillHeight: true; title: "Balanced"; description: "Recommended"; checked: true }
                        RadioCard { Layout.fillWidth: true; Layout.fillHeight: true; title: "Best performance"; description: "Hover"; previewState: "hover" }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        RadioCard {
                            Layout.fillWidth: true
                            title: "Raye Night"
                            checked: true
                            Rectangle { width: parent.width; height: 72; radius: 4; color: "#130e1f"; border.color: "#7b3cff" }
                        }
                        RadioCard {
                            Layout.fillWidth: true
                            title: "Raye Day"
                            Rectangle { width: parent.width; height: 72; radius: 4; color: "#f6f3fb" }
                        }
                        RadioCard { Layout.fillWidth: true; Layout.fillHeight: true; title: "Keyboard focus"; previewState: "focus" }
                        RadioCard { Layout.fillWidth: true; Layout.fillHeight: true; title: "Disabled"; enabled: false }
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: gallery.columnWidth
                    Layout.alignment: Qt.AlignTop
                    spacing: 12

                    SectionHeader { text: "Text fields"; first: true }
                    TextField { objectName: "textField"; Layout.fillWidth: true; placeholderText: "Find a setting" }
                    TextField { Layout.fillWidth: true; placeholderText: "Hover"; previewState: "hover" }
                    TextField { Layout.fillWidth: true; placeholderText: "Name"; text: "Focused, with text"; previewState: "focus" }
                    TextField { Layout.fillWidth: true; placeholderText: "Disabled"; enabled: false }
                    PasswordField { objectName: "passwordField"; Layout.fillWidth: true; placeholderText: "Password"; text: "not a real one" }

                    SectionHeader { text: "Sliders" }
                    Repeater {
                        model: ["", "hover", "pressed", "focus", "disabled"]
                        Slider {
                            required property string modelData
                            from: 0; to: 100; value: 30 + 10 * Math.max(0, ["", "hover", "pressed", "focus", "disabled"].indexOf(modelData))
                            previewState: modelData === "disabled" ? "" : modelData
                            enabled: modelData !== "disabled"
                            Accessible.name: "Slider " + modelData
                        }
                    }

                    SectionHeader { text: "Progress" }
                    ProgressBar { objectName: "progress"; Layout.fillWidth: true; value: 0.6 }
                    ProgressBar { Layout.fillWidth: true; indeterminate: true }
                    ProgressBar { Layout.fillWidth: true; value: 0.4; status: "caution" }
                    ProgressBar { Layout.fillWidth: true; value: 0.8; status: "critical" }
                }
            }

            // -------------------------------------------- everything else
            GridLayout {
                id: page2
                visible: gallery.page === 2
                width: parent.width
                columns: 2
                columnSpacing: 32
                rowSpacing: 4

                ColumnLayout {
                    Layout.preferredWidth: gallery.columnWidth
                    Layout.alignment: Qt.AlignTop
                    spacing: 8

                    SectionHeader { text: "Info bars"; first: true }
                    InfoBar { severity: "informational"; title: "Informational"; message: "Short message on one line." }
                    InfoBar { severity: "success"; title: "Success"; message: "Your device is up to date."; closable: false }
                    InfoBar {
                        severity: "warning"
                        title: "Restart needed"
                        message: "Restart your device to finish installing updates. It takes a minute, and your open apps come back."
                        Button { text: "Restart now"; accent: true }
                    }
                    InfoBar { severity: "error"; title: "Couldn't connect"; message: "Check the cable and try again." }

                    SectionHeader { text: "Tiles" }
                    GridLayout {
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 12
                        Tile { objectName: "tile"; Layout.fillWidth: true; iconName: "preferences-system"; title: "System"; summary: "Display, sound, notifications, power" }
                        Tile { Layout.fillWidth: true; iconName: "network-wireless"; title: "Network & internet"; summary: "Wi-Fi, airplane mode, VPN"; previewState: "hover" }
                        Tile { Layout.fillWidth: true; iconName: "preferences-desktop-theme"; title: "Personalization"; summary: "Pressed"; previewState: "pressed" }
                        Tile { Layout.fillWidth: true; iconName: "preferences-desktop-accessibility"; title: "Accessibility"; summary: "Keyboard focus"; previewState: "focus" }
                    }
                }

                ColumnLayout {
                    Layout.preferredWidth: gallery.columnWidth
                    Layout.alignment: Qt.AlignTop
                    spacing: 8

                    SectionHeader { text: "Breadcrumb"; first: true }
                    Breadcrumb {
                        objectName: "crumbsWide"
                        Layout.fillWidth: true
                        crumbs: [{ text: "System", target: "system" }, { text: "Display" }]
                    }
                    Breadcrumb {
                        objectName: "crumbsNarrow"
                        Layout.preferredWidth: 360
                        crumbs: [{ text: "Bluetooth & devices", target: "devices" }, { text: "Mouse", target: "devices/mouse" }, { text: "Additional settings" }]
                    }

                    SectionHeader { text: "People and status" }
                    Row {
                        spacing: 16
                        PersonPicture { name: "Alex Martin"; size: 72 }
                        PersonPicture { name: "Raye"; size: 56 }
                        Column {
                            spacing: 8
                            anchors.verticalCenter: parent.verticalCenter
                            Row {
                                spacing: 8
                                StatusGlyph { status: "success"; label: "Good" }
                                StatusGlyph { status: "caution"; label: "Warning" }
                                StatusGlyph { status: "critical"; label: "Error" }
                                StatusGlyph { status: "info"; label: "Information" }
                                StatusGlyph { status: ""; label: "Neutral" }
                            }
                            Row {
                                spacing: 8
                                StatusGlyph { status: "success"; size: 20 }
                                StatusGlyph { status: "caution"; size: 20 }
                                StatusGlyph { status: "critical"; size: 20 }
                                StatusGlyph { status: "info"; size: 20 }
                            }
                        }
                    }

                    SectionHeader { text: "Loading and empty" }
                    Shimmer { Layout.preferredWidth: 220 }
                    Shimmer { Layout.preferredWidth: 340; implicitHeight: 12 }
                    EmptyState {
                        iconName: "preferences-system-bluetooth"
                        title: "No devices yet"
                        message: "Turn on your Bluetooth device and make it visible, then add it here."
                        Button { text: "Add device"; accent: true }
                    }

                    SectionHeader { text: "Page frame" }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 230
                        color: Theme.layer
                        radius: Theme.radiusOverlay
                        border.color: Theme.strokeSurface
                        PageScaffold {
                            objectName: "scaffold"
                            anchors.fill: parent
                            showBack: true
                            crumbs: [{ text: "System", target: "system" }, { text: "Power" }]
                            InfoBar { severity: "success"; title: "Plugged in"; closable: false }
                            ToggleCard { title: "Energy saver"; checked: false }
                            Card { title: "Screen and sleep"; searchId: "demo-anchor" }
                            Card { title: "One more row" }
                        }
                    }
                }
            }
        }
    }

    ContentDialog {
        objectName: "dialog"
        visible: gallery.dialogOpen
        title: "Restart now?"
        text: "Save your work first. Your device restarts in a moment and your apps come back after you sign in."
        primaryText: "Restart now"
        secondaryText: "Later"
    }
}
