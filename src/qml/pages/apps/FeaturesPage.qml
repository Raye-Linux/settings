// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Optional features: packs of Raye to add or remove (gaming
// tools, Windows programs, more languages), each with Install or
// Remove and its progress. The administrator password is asked by the
// system when a pack is added or removed. The Raye kernel row leads to
// Raye Update, where it is turned on.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Apps"), target: "apps" },
             { text: i18nc("@title settings page", "Optional features") }]

    OptionalFeatures {
        id: features
        onFinished: (id, ok, message) => {
            page.failedId = ok ? "" : id
            page.failedMessage = message
        }
    }

    property string failedId: ""
    property string failedMessage: ""

    Text {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s4
        text: i18nc("@info", "Add more to Raye, or remove what you don't use to free up space.")
        font: Theme.body
        color: Theme.textSecondary
        wrapMode: Text.Wrap
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.failedId !== ""
        severity: "error"
        title: i18nc("@info", "That didn't work")
        message: i18nc("@info", "Check your internet connection and try again.")
        onClosed: page.failedId = ""
    }

    SectionHeader {
        property string searchId: "features"
        text: i18nc("@title:group", "Features")
        first: true
    }

    Repeater {
        model: features
        delegate: Card {
            id: row
            required property var model
            readonly property bool busy: features.busyId === model.featureId
            readonly property string featureState: model.state
            Layout.fillWidth: true
            iconName: model.iconName
            iconFallback: "package-x-generic"
            title: model.title
            description: busy ? (featureState === "installed" ? i18nc("@info", "Removing…") : i18nc("@info", "Installing…"))
                       : featureState === "installed" ? i18nc("@info", "Installed") + " · " + model.description
                       : featureState === "" ? i18nc("@info", "Not available right now") + " · " + model.description
                       : model.description
            clickable: featureState === "link"
            actionGlyph: featureState === "link" ? "chevron-right" : ""
            onClicked: if (featureState === "link") Router.navigate(model.route)

            RowLayout {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                spacing: Theme.s3
                visible: row.featureState !== "link"
                ProgressBar {
                    visible: row.busy || row.featureState === "unknown"
                    Layout.preferredWidth: 120
                    indeterminate: row.featureState === "unknown" || features.busyPercent <= 0
                    value: features.busyPercent / 100
                }
                Button {
                    visible: !row.busy && (row.featureState === "available" || row.featureState === "partial")
                    accent: true
                    text: i18nc("@action:button", "Install")
                    Accessible.description: row.model.title
                    enabled: features.busyId === ""
                    onClicked: features.install(row.model.featureId)
                }
                Button {
                    visible: !row.busy && row.featureState === "installed"
                    text: i18nc("@action:button", "Remove")
                    Accessible.description: row.model.title
                    enabled: features.busyId === ""
                    onClicked: {
                        removeDialog.featureId = row.model.featureId
                        removeDialog.title = i18nc("@title:window %1 is a feature", "Remove %1?", row.model.title)
                        removeDialog.open()
                    }
                }
            }
        }
    }

    ContentDialog {
        id: removeDialog
        property string featureId
        text: i18nc("@info", "You can install it again from here at any time.")
        primaryText: i18nc("@action:button", "Remove")
        onAccepted: features.remove(featureId)
    }
}
