// SPDX-License-Identifier: GPL-3.0-or-later
// A category's own page when it has none of its own yet: its name as
// the title, then a card for each page in it (glyph, name, one line,
// chevron), the way Windows lists the pages of System.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    property string categoryId
    property var model: typeof categories !== "undefined" ? categories : null

    readonly property int row: model ? model.categoryOf(categoryId) : -1
    readonly property var modelIndex: model && row >= 0 ? model.index(row, 0) : null
    // CategoryModel roles: name, pages
    readonly property string categoryName: modelIndex ? model.data(modelIndex, Qt.UserRole + 2) : ""
    readonly property var pages: modelIndex ? model.data(modelIndex, Qt.UserRole + 7) : []

    crumbs: [{ text: categoryName, target: "" }]
    showBack: false

    Repeater {
        model: page.pages
        delegate: NavCard {
            required property var modelData
            title: modelData.name
            description: modelData.summary
            iconName: modelData.icon
            target: modelData.id
        }
    }

    EmptyState {
        Layout.fillWidth: true
        visible: page.pages.length === 0
        iconName: "org.raye.settings-status-info"
        title: i18n("Nothing to set here on this PC")
        message: i18n("This computer has no device for the pages of this category.")
    }
}
