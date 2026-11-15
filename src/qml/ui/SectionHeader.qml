// SPDX-License-Identifier: GPL-3.0-or-later
// The title over a group of cards ("Related settings", "Brightness &
// colour"): Body Strong, 28 px above and 8 below once the 4 px between
// cards is counted (PageScaffold's column).
import QtQuick
import QtQuick.Layouts

Text {
    id: header

    // the first header of a page sits right under the page title
    property bool first: false

    Layout.fillWidth: true
    topPadding: first ? 0 : Theme.sectionHeaderTop - Theme.cardSpacing
    bottomPadding: Theme.sectionHeaderBottom - Theme.cardSpacing
    font: Theme.bodyStrong
    horizontalAlignment: Text.AlignLeft
    color: Theme.text
    wrapMode: Text.Wrap
    lineHeightMode: Text.FixedHeight
    lineHeight: Theme.bodyLineHeight
    Accessible.role: Accessible.Heading
    Accessible.name: text
}
