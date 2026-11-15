// SPDX-License-Identifier: GPL-3.0-or-later
// "Search results for …": every setting that matches, grouped by
// category, best group first. Choosing one opens its page and flashes
// the row.
//
//   SearchResultsPage { query: "night" }
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    property string query

    title: i18nc("@title search results page", "Search results for “%1”", query)

    SearchModel {
        id: results
        grouped: true
        query: page.query
        Component.onCompleted: searchNow()
        onQueryChanged: searchNow()
    }

    Repeater {
        model: results
        delegate: ColumnLayout {
            id: row
            required property int index
            required property string entryId
            required property string label
            required property string breadcrumb
            required property string iconName
            required property string iconFallback
            required property string categoryName

            Layout.fillWidth: true
            spacing: 0

            SectionHeader {
                visible: row.index === 0 || results.categoryNameAt(row.index - 1) !== row.categoryName
                first: row.index === 0
                text: row.categoryName
            }
            NavCard {
                Layout.fillWidth: true
                title: row.label
                description: row.breadcrumb
                iconName: row.iconName
                iconFallback: row.iconFallback
                target: row.entryId
            }
        }
    }

    EmptyState {
        visible: results.count === 0 && !results.pending
        Layout.topMargin: Theme.s8
        iconName: "org.raye.settings-status-info"
        title: i18n("No results")
        message: i18n("Try other words.")
    }
}
