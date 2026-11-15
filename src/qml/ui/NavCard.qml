// SPDX-License-Identifier: GPL-3.0-or-later
// A card that opens another page: the whole row is the button, a
// chevron says so. With `target` set ("system/display") it goes there.
import QtQuick
import org.raye.settings

Card {
    id: card

    // a page id; empty: the page handles clicked() itself
    property string target

    clickable: true
    actionGlyph: "chevron-right"
    onClicked: if (target !== "") Router.navigate(target)
}
