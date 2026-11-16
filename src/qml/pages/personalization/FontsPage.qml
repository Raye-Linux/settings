// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Fonts: "Add fonts" (drop font files on the card, or
// browse for them), then every font family as a tile with a sample in
// that font; a tile opens the family's page in place (sizes, faces,
// Uninstall for fonts the user installed). The text settings of the
// desktop (UI font, sizes, smoothing) are in "More font settings".
import QtQuick
import QtQuick.Dialogs
import QtQuick.Layouts
import org.raye.settings

Item {
    id: root

    property string openFamily: ""
    property var refusedNames: []

    FontSettings {
        id: fonts
        onRefused: (names) => root.refusedNames = names
    }

    function searchIds() { return listPage.searchIds() }
    function scrollToAnchor(anchor) { openFamily = ""; return listPage.scrollToAnchor(anchor) }
    // Back (mouse, Alt+Left, Backspace) from a font's details: to the fonts
    function goBackInPage() {
        if (openFamily === "")
            return false
        openFamily = ""
        return true
    }

    PageScaffold {
        id: listPage
        anchors.fill: parent
        visible: root.openFamily === ""
        crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
                 { text: i18nc("@title settings page", "Fonts") }]

        InfoBar {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.s3
            open: root.refusedNames.length > 0
            severity: "warning"
            title: i18nc("@info", "Some files are not fonts")
            message: root.refusedNames.join(", ")
            onClosed: root.refusedNames = []
        }

        SectionHeader { text: i18nc("@title:group", "Add fonts"); first: true }

        // the drop zone
        Rectangle {
            id: dropZone
            Layout.fillWidth: true
            implicitHeight: 132
            radius: Theme.radiusControl
            color: drop.containsDrag ? Theme.subtleSelected : Theme.card
            border.width: drop.containsDrag ? 2 : 1
            border.color: drop.containsDrag ? Theme.accent : Theme.cardBorder
            property string searchId: "add-fonts"
            Accessible.role: Accessible.Pane
            Accessible.name: i18nc("@info", "Drag and drop to install")

            Behavior on color { ColorAnimation { duration: Theme.durationFast } }

            DropArea {
                id: drop
                anchors.fill: parent
                keys: ["text/uri-list"]
                onDropped: (event) => {
                    if (event.hasUrls) {
                        fonts.install(event.urls)
                        event.acceptProposedAction()
                    }
                }
            }
            ColumnLayout {
                anchors.centerIn: parent
                spacing: Theme.s3
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: i18nc("@info", "Drag and drop to install")
                    font: Theme.subtitle
                    color: Theme.text
                }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: i18nc("@info", "Drag font files from Files or your desktop and drop them here to install fonts on your PC.")
                    font: Theme.caption
                    color: Theme.textSecondary
                }
                Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: i18nc("@action:button", "Browse and install fonts")
                    onClicked: fontDialog.open()
                }
            }
        }

        SectionHeader { text: i18nc("@title:group", "Available fonts") }

        RowLayout {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.s3
            spacing: Theme.s3
            TextField {
                Layout.preferredWidth: 300
                placeholderText: i18nc("@info:placeholder", "Search fonts")
                Accessible.name: i18nc("@info:placeholder", "Search fonts")
                property string searchId: "search-fonts"
                onTextChanged: fonts.searchText = text
            }
            Item { Layout.fillWidth: true }
            Text {
                text: i18ncp("@info", "%1 font family found", "%1 font families found", fonts.count)
                font: Theme.caption
                color: Theme.textSecondary
            }
        }

        EmptyState {
            Layout.fillWidth: true
            visible: !fonts.loading && fonts.count === 0
            iconName: "org.raye.settings-fonts"
            title: i18nc("@info", "No results. Try other words.")
        }

        Flow {
            id: tiles
            Layout.fillWidth: true
            spacing: Theme.cardSpacing
            readonly property int perRow: Math.max(1, Math.floor((width + spacing) / (220 + spacing)))
            readonly property real tileWidth: Math.floor((width - (perRow - 1) * spacing) / perRow)
            Repeater {
                model: fonts
                delegate: FontsTile {
                    required property var model
                    width: tiles.tileWidth
                    family: model.family
                    faceCount: model.faceCount
                    onClicked: root.openFamily = model.family
                }
            }
        }

        SectionHeader { text: i18nc("@title:group", "Related settings") }
        LinkCard {
            Layout.fillWidth: true
            iconName: "org.raye.settings-fonts"
            iconFallback: "preferences-desktop-font"
            title: i18nc("@label", "More font settings")
            description: i18nc("@info", "The fonts of the desktop and apps, sizes and smoothing")
            searchId: "font-settings"
            enabled: typeof moduleHost !== "undefined"
            onClicked: moduleHost.openExternal("kcm_fonts")
        }
    }

    Loader {
        anchors.fill: parent
        active: root.openFamily !== ""
        sourceComponent: FontsDetail {
            info: fonts.family(root.openFamily)
            onBackRequested: root.openFamily = ""
            onUninstallRequested: {
                fonts.uninstall(root.openFamily)
                root.openFamily = ""
            }
        }
    }

    FileDialog {
        id: fontDialog
        title: i18nc("@title:window", "Choose fonts to install")
        fileMode: FileDialog.OpenFiles
        nameFilters: [i18nc("@item file type", "Fonts (*.ttf *.otf *.ttc *.otc)")]
        onAccepted: fonts.install(selectedFiles)
    }
}
