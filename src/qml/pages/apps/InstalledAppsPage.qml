// SPDX-License-Identifier: GPL-3.0-or-later
// Apps > Installed apps: every app on this PC, wherever it came from,
// with search, sort, a filter by kind, a list or a grid, and a "..."
// menu per app (Open, Advanced options, Uninstall). Uninstalling asks
// once in a small flyout, as Windows does, then the Store (or the
// Windows programs tool) does the removal. "Advanced options" opens the
// app's own page in place: Apps > Installed apps > <app>.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

Item {
    id: root

    // the app whose page is open, "" for the list
    property string openApp: ""

    InstalledApps { id: apps }

    // the list's breadcrumb and searchIds, for whoever walks the page
    function searchIds() { return listPage.searchIds() }
    function scrollToAnchor(anchor) { openApp = ""; return listPage.scrollToAnchor(anchor) }

    // the error of the last removal, shown on top of the list
    property string failedName: ""

    Connections {
        target: apps
        function onUninstallFinished(id, ok, message) {
            const app = apps.app(id)
            root.failedName = ok ? "" : (app.name || "")
            if (ok && root.openApp === id)
                root.openApp = ""
        }
    }

    function sizeText(bytes) {
        if (bytes === undefined || bytes < 0)
            return ""
        return Qt.locale().formattedDataSize(bytes, 1, Locale.DataSizeTraditionalFormat)
    }
    function dateText(date) {
        if (!date || isNaN(date.getTime()))
            return ""
        return date.toLocaleDateString(Qt.locale(), Locale.ShortFormat)
    }
    function sourceText(source) {
        if (source === "store")
            return i18nc("@info where an app comes from", "Raye Store")
        if (source === "windows")
            return i18nc("@info where an app comes from", "Windows program")
        return i18nc("@info where an app comes from", "Part of Raye")
    }
    // "Raye Store · 1.2.3 · 12/10/2026"
    function detailLine(app) {
        const parts = [app.publisher !== "" ? app.publisher : sourceText(app.source)]
        if (app.version !== "")
            parts.push(app.version)
        const date = dateText(app.installed)
        if (date !== "")
            parts.push(date)
        return parts.join(" · ")
    }

    function askUninstall(id, anchorItem) {
        flyout.appId = id
        flyout.appName = apps.app(id).name || ""
        flyout.parent = anchorItem
        flyout.x = anchorItem.width - flyout.width
        flyout.y = anchorItem.height + 4
        flyout.open()
    }

    PageScaffold {
        id: listPage
        anchors.fill: parent
        visible: root.openApp === ""
        crumbs: [{ text: i18nc("@title settings category", "Apps"), target: "apps" },
                 { text: i18nc("@title settings page", "Installed apps") }]

        InfoBar {
            Layout.fillWidth: true
            Layout.bottomMargin: Theme.s3
            open: root.failedName !== ""
            severity: "error"
            title: i18nc("@info", "Couldn't uninstall %1", root.failedName)
            message: i18nc("@info", "Try again in a moment, or remove it from Raye Store.")
            onClosed: root.failedName = ""
        }

        // search and list or grid; under it the count, sort and filter
        RowLayout {
            Layout.fillWidth: true
            spacing: Theme.s3
            TextField {
                id: search
                Layout.fillWidth: true
                Layout.minimumWidth: 200
                Layout.maximumWidth: 360
                placeholderText: i18nc("@info:placeholder", "Search apps")
                Accessible.name: i18nc("@info:placeholder", "Search apps")
                property string searchId: "search-apps"
                onTextChanged: apps.searchText = text
            }
            Item { Layout.fillWidth: true }
            Button {
                id: viewToggle
                property bool grid: false
                text: grid ? i18nc("@action:button show the apps as a list", "List") : i18nc("@action:button show the apps as a grid", "Grid")
                Accessible.description: i18nc("@info", "Change how the apps are shown")
                onClicked: grid = !grid
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s3
            Layout.bottomMargin: Theme.s2
            spacing: Theme.s3
            Text {
                Layout.fillWidth: true
                text: apps.loading ? i18nc("@info", "Looking for apps…")
                                   : i18ncp("@info", "%1 app found", "%1 apps found", apps.count)
                font: Theme.caption
                color: Theme.textSecondary
            }
            Text {
                text: i18nc("@label:listbox", "Sort by:")
                font: Theme.body
                color: Theme.textSecondary
            }
            ComboBox {
                id: sortBox
                property string searchId: "sort-apps"
                textRole: "text"
                valueRole: "value"
                model: [{ text: i18nc("@item:inlistbox sort apps", "Name (A to Z)"), value: "name" },
                        { text: i18nc("@item:inlistbox sort apps", "Size (large to small)"), value: "size" },
                        { text: i18nc("@item:inlistbox sort apps", "Date installed"), value: "date" }]
                Accessible.name: i18nc("@label:listbox", "Sort by")
                onActivated: apps.sortOrder = currentValue
            }
            Text {
                text: i18nc("@label:listbox", "Filter by:")
                font: Theme.body
                color: Theme.textSecondary
            }
            ComboBox {
                id: filterBox
                property string searchId: "filter-apps"
                textRole: "text"
                valueRole: "value"
                model: [{ text: i18nc("@item:inlistbox show apps", "All apps"), value: "all" },
                        { text: i18nc("@item:inlistbox show apps", "Store apps"), value: "store" },
                        { text: i18nc("@item:inlistbox show apps", "System apps"), value: "system" },
                        { text: i18nc("@item:inlistbox show apps", "Windows programs"), value: "windows" }]
                Accessible.name: i18nc("@label:listbox", "Filter by")
                onActivated: apps.filter = currentValue
            }
        }

        // while the list is read
        Repeater {
            model: apps.loading && apps.total === 0 ? 6 : 0
            delegate: Shimmer {
                Layout.fillWidth: true
                Layout.preferredHeight: Theme.cardMinHeight
            }
        }

        EmptyState {
            Layout.fillWidth: true
            Layout.topMargin: Theme.s7
            visible: !apps.loading && apps.count === 0
            iconName: "org.raye.settings-installed-apps"
            title: apps.total === 0 ? i18nc("@info", "No apps found") : i18nc("@info", "No results. Try other words.")
            message: apps.total === 0 ? "" : i18nc("@info", "Check the spelling, or show all apps.")
        }

        // the list
        Repeater {
            model: viewToggle.grid ? null : apps
            delegate: InstalledAppRow {
                Layout.fillWidth: true
                busy: apps.busyId === appKey
                percent: apps.busyPercent
                detail: root.detailLine({ publisher: publisher, source: source, version: version, installed: installed })
                sizeText: root.sizeText(size)
                onOpenRequested: apps.open(appKey)
                onAdvancedRequested: root.openApp = appKey
                onUninstallRequested: (anchor) => root.askUninstall(appKey, anchor)
            }
        }

        // the grid
        Flow {
            Layout.fillWidth: true
            visible: viewToggle.grid
            spacing: Theme.cardSpacing
            Repeater {
                model: viewToggle.grid ? apps : null
                delegate: InstalledAppTile {
                    width: Math.floor((parent.width - 2 * Theme.cardSpacing) / 3)
                    busy: apps.busyId === appKey
                    detail: root.sizeText(size) !== "" ? root.sizeText(size) : root.sourceText(source)
                    onOpenRequested: apps.open(appKey)
                    onAdvancedRequested: root.openApp = appKey
                    onUninstallRequested: (anchor) => root.askUninstall(appKey, anchor)
                }
            }
        }

        SectionHeader { text: i18nc("@title:group", "Related settings") }

        NavCard {
            Layout.fillWidth: true
            iconName: "org.raye.settings-startup"
            iconFallback: "system-run"
            title: i18nc("@title settings page", "Startup")
            description: i18nc("@info", "Apps that start when you sign in")
            target: "apps/startup"
        }
        NavCard {
            Layout.fillWidth: true
            iconName: "org.raye.settings-windows-programs"
            iconFallback: "application-x-executable"
            title: i18nc("@title settings page", "Windows programs")
            description: i18nc("@info", "Where Windows programs live, and the space they take")
            target: "gaming/windows-programs"
        }
    }

    // one app's own page
    Loader {
        anchors.fill: parent
        active: root.openApp !== ""
        sourceComponent: InstalledAppOptions {
            app: apps.app(root.openApp)
            model: apps
            busy: apps.busyId === root.openApp
            percent: apps.busyPercent
            sizeText: root.sizeText(app.size)
            dateText: root.dateText(app.installed)
            sourceText: root.sourceText(app.source)
            onBackRequested: root.openApp = ""
            onUninstallRequested: (anchor) => root.askUninstall(root.openApp, anchor)
        }
    }

    InstalledAppUninstall {
        id: flyout
        onConfirmed: (id) => apps.uninstall(id)
    }
}
