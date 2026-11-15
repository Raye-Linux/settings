// SPDX-License-Identifier: GPL-3.0-or-later
// A page named in the layout whose file is not built yet: the same
// header as every page (back arrow, breadcrumb), and the placeholder
// that says so plainly.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: holder

    property string pageName
    property string summary
    property string iconName

    followAnchors: false

    PlaceholderPage {
        Layout.fillWidth: true
        Layout.preferredHeight: Math.max(320, holder.height - 200)
        pageName: holder.pageName
        summary: holder.summary
        iconName: holder.iconName !== "" ? holder.iconName : "preferences-system"
    }
}
