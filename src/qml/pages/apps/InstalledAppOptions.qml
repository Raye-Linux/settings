// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Installed apps > <app>: what Windows calls the app's Advanced
// options. Its version, size and origin; for a Store app, its
// permissions, "Terminate" and "Reset" (what it saved is deleted, it
// starts afresh); and Uninstall for every app that may be removed.
import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.raye.settings

PageScaffold {
    id: page

    // the app, as InstalledApps.app() gives it
    property var app: ({})
    property var model: null
    property bool busy: false
    property int percent: 0
    property string sizeText
    property string dateText
    property string sourceText

    readonly property bool storeApp: app.source === "store"
    property bool terminated: false
    property bool resetDone: false

    signal backRequested()
    signal uninstallRequested(Item anchor)

    showBack: true
    crumbs: [{ text: i18nc("@title settings category", "Apps"), target: "apps" },
             { text: i18nc("@title settings page", "Installed apps"), target: "apps/installed" },
             { text: app.name || "" }]
    onBackClicked: backRequested()
    onCrumbActivated: (target) => {
        if (target === "apps/installed")
            backRequested()
        else if (target !== "")
            Router.navigate(target)
    }

    // name, icon and what it is
    RowLayout {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s4
        spacing: Theme.s5
        Kirigami.Icon {
            Layout.preferredWidth: 64
            Layout.preferredHeight: 64
            source: page.app.iconSource && page.app.iconSource.toString() !== "" ? page.app.iconSource : (page.app.iconName || "")
            fallback: "application-x-executable"
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: Theme.s1
            Text {
                Layout.fillWidth: true
                text: page.app.name || ""
                font: Theme.subtitle
                color: Theme.text
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                Layout.fillWidth: true
                text: page.app.publisher ? page.app.publisher : page.sourceText
                font: Theme.body
                color: Theme.textSecondary
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
        }
    }

    SectionHeader { text: i18nc("@title:group", "Specifications"); first: true }

    InfoCard {
        Layout.fillWidth: true
        title: i18nc("@label", "Version")
        value: page.app.version || i18nc("@info no value", "Not known")
        copyable: !!page.app.version
    }
    InfoCard {
        Layout.fillWidth: true
        title: i18nc("@label", "Size")
        value: page.sizeText !== "" ? page.sizeText : i18nc("@info no value", "Not known")
        copyable: false
    }
    InfoCard {
        Layout.fillWidth: true
        visible: page.dateText !== ""
        title: i18nc("@label", "Installed on")
        value: page.dateText
        copyable: false
    }
    InfoCard {
        Layout.fillWidth: true
        title: i18nc("@label where the app comes from", "Comes from")
        value: page.sourceText
        copyable: false
    }
    // Windows programs: settings Raye checked for this one, or the ones for all
    InfoCard {
        Layout.fillWidth: true
        visible: page.app.source === "windows"
        readonly property bool optimized: page.app.profile === "optimized"
        title: i18nc("@label settings a Windows program runs with", "Profile")
        value: optimized ? i18nc("@info Windows program profile", "Optimized for Raye")
                         : i18nc("@info Windows program profile", "Default")
        description: optimized
            ? i18nc("@info", "Raye has settings made and checked for this program, so it should run with fewer problems.")
            : i18nc("@info", "The settings Raye uses for every Windows program. Most run well with them, but some may have bugs.")
        copyable: false
    }

    SectionHeader {
        visible: page.storeApp
        text: i18nc("@title:group", "App permissions")
    }
    ButtonCard {
        Layout.fillWidth: true
        visible: page.storeApp
        iconName: "org.raye.settings-permissions"
        iconFallback: "security-medium"
        title: i18nc("@label", "Files, devices and network this app can use")
        description: i18nc("@info", "Store apps only reach what they are allowed to")
        buttonText: i18nc("@action:button", "Change")
        enabled: typeof moduleHost !== "undefined"
        onButtonClicked: moduleHost.openExternal("kcm_flatpak")
    }

    SectionHeader {
        visible: page.storeApp
        text: i18nc("@title:group", "Terminate")
    }
    ButtonCard {
        Layout.fillWidth: true
        visible: page.storeApp
        iconName: "org.raye.settings-terminate"
        iconFallback: "process-stop"
        title: i18nc("@label", "Immediately stop this app and the processes that go with it")
        description: page.terminated ? i18nc("@info", "Stopped") : ""
        buttonText: i18nc("@action:button", "Terminate")
        onButtonClicked: page.terminated = page.model.terminate(page.app.appKey)
    }

    SectionHeader {
        visible: page.storeApp
        text: i18nc("@title:group", "Reset")
    }
    ButtonCard {
        Layout.fillWidth: true
        visible: page.storeApp
        iconName: "org.raye.settings-reset"
        iconFallback: "edit-reset"
        title: i18nc("@label", "If this app isn't working right, reset it. What it saved on this PC will be deleted.")
        description: page.resetDone ? i18nc("@info", "Reset done") : ""
        buttonText: i18nc("@action:button", "Reset")
        onButtonClicked: resetDialog.open()
    }

    SectionHeader { text: i18nc("@title:group", "Uninstall") }
    Card {
        id: uninstallCard
        Layout.fillWidth: true
        iconName: "org.raye.settings-uninstall"
        iconFallback: "edit-delete"
        title: page.app.removable === false ? i18nc("@info", "This app is part of Raye and can't be uninstalled")
                                            : i18nc("@label", "Uninstall this app and its settings. Your documents stay.")
        RowLayout {
            spacing: Theme.s3
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            ProgressBar {
                visible: page.busy
                Layout.preferredWidth: 120
                indeterminate: page.percent <= 0
                value: page.percent / 100
            }
            Button {
                id: uninstallButton
                text: i18nc("@action:button", "Uninstall")
                enabled: page.app.removable !== false && !page.busy
                onClicked: page.uninstallRequested(uninstallButton)
            }
        }
    }

    ContentDialog {
        id: resetDialog
        title: i18nc("@title:window", "Reset %1?", page.app.name || "")
        text: i18nc("@info", "Everything this app saved on this PC is deleted: its settings, its sign-ins and what it keeps for you. It then starts as if it were new.")
        primaryText: i18nc("@action:button", "Reset")
        onAccepted: page.resetDone = page.model.resetData(page.app.appKey)
    }
}
