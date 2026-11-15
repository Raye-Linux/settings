// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Default apps: the app for each job (browser, email, music...),
// and below, "Choose defaults by file type or link type": type ".png"
// or "mailto", pick a row, pick the app.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Apps"), target: "apps" },
             { text: i18nc("@title settings page", "Default apps") }]

    DefaultApps { id: defaults }

    // the card of one job, filled from defaults.roles
    component RoleCard: Card {
        id: roleCard
        property string roleId
        readonly property var role: {
            const list = defaults.roles
            for (const r of list) {
                if (r.id === roleId)
                    return r
            }
            return ({ id: roleId, title: "", iconName: "", appId: "", appName: "", appIcon: "" })
        }
        Layout.fillWidth: true
        iconName: role.iconName
        title: role.title
        description: role.appId !== "" ? "" : i18nc("@info", "No app chosen")
        clickable: true
        actionGlyph: "chevron-right"
        onClicked: page.pickRole(role)
        RowLayout {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            spacing: Theme.s3
            visible: roleCard.role.appId !== ""
            Kirigami.Icon {
                Layout.preferredWidth: 20
                Layout.preferredHeight: 20
                source: roleCard.role.appIcon
                fallback: "application-x-executable"
            }
            Text {
                text: roleCard.role.appName
                font: Theme.body
                color: Theme.text
                textFormat: Text.PlainText
            }
        }
    }

    // what the picker sets: { role } or { type, label }
    property var picking: ({})
    property string query: ""

    readonly property var results: query.trim() === "" ? [] : defaults.searchTypes(query, 30)

    function pickRole(role) {
        picking = { role: role.id }
        picker.title = i18nc("@title:window %1 is a job like Web browser", "Choose your %1", role.title.toLowerCase())
        picker.choices = defaults.candidates(role.id)
        picker.open()
    }
    function pickType(row) {
        picking = { type: row.type }
        picker.title = row.type.indexOf("x-scheme-handler/") === 0
            ? i18nc("@title:window %1 is a kind of link like MAILTO", "How do you want to open %1 links from now on?", row.label)
            : i18nc("@title:window %1 is a file extension like .png", "How do you want to open %1 files from now on?", row.label)
        picker.choices = defaults.candidatesForType(row.type)
        picker.open()
    }

    Connections {
        target: defaults
        // the list of results reads the defaults again
        function onChanged() { const q = page.query; page.query = ""; page.query = q }
    }

    SectionHeader { text: i18nc("@title:group", "Set defaults for apps"); first: true }

    RoleCard { roleId: "browser"; searchId: "role-browser" }
    RoleCard { roleId: "email"; searchId: "role-email" }
    RoleCard { roleId: "music"; searchId: "role-music" }
    RoleCard { roleId: "video"; searchId: "role-video" }
    RoleCard { roleId: "photos"; searchId: "role-photos" }
    RoleCard { roleId: "files"; searchId: "role-files" }
    RoleCard { roleId: "terminal"; searchId: "role-terminal" }
    RoleCard { roleId: "text"; searchId: "role-text" }
    RoleCard { roleId: "pdf"; searchId: "role-pdf" }
    RoleCard { roleId: "maps"; searchId: "role-maps" }

    SectionHeader { text: i18nc("@title:group", "Choose defaults by file type or link type") }

    TextField {
        id: typeSearch
        Layout.fillWidth: true
        Layout.maximumWidth: 480
        property string searchId: "file-types"
        placeholderText: i18nc("@info:placeholder", "Type a file type (.png) or a link type (mailto)")
        Accessible.name: i18nc("@title:group", "Choose defaults by file type or link type")
        onTextChanged: debounce.restart()
        Timer {
            id: debounce
            interval: 150
            onTriggered: page.query = typeSearch.text
        }
    }

    Text {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s3
        visible: page.query.trim() !== "" && page.results.length === 0
        text: i18nc("@info", "No results. Try other words.")
        font: Theme.body
        color: Theme.textSecondary
    }

    Repeater {
        model: page.results
        delegate: Card {
            id: typeCard
            required property var modelData
            Layout.fillWidth: true
            title: modelData.label
            description: modelData.description
            clickable: true
            actionGlyph: "chevron-right"
            onClicked: page.pickType(modelData)
            RowLayout {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                spacing: Theme.s3
                Kirigami.Icon {
                    visible: typeCard.modelData.appId !== ""
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    source: typeCard.modelData.appIcon
                    fallback: "application-x-executable"
                }
                Text {
                    text: typeCard.modelData.appId !== "" ? typeCard.modelData.appName : i18nc("@action:button", "Choose a default")
                    font: Theme.body
                    color: typeCard.modelData.appId !== "" ? Theme.text : Theme.accentText
                    textFormat: Text.PlainText
                }
            }
        }
    }

    SectionHeader { text: i18nc("@title:group", "Related settings") }
    NavCard {
        Layout.fillWidth: true
        iconName: "org.raye.settings-installed-apps"
        iconFallback: "applications-other"
        title: i18nc("@title settings page", "Installed apps")
        description: i18nc("@info", "See, repair and remove apps")
        target: "apps/installed"
    }

    DefaultAppPicker {
        id: picker
        onChosenApp: (appId) => {
            if (page.picking.role)
                defaults.setRoleDefault(page.picking.role, appId)
            else if (page.picking.type)
                defaults.setTypeDefault(page.picking.type, appId)
        }
    }
}
