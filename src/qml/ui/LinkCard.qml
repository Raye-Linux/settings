// SPDX-License-Identifier: GPL-3.0-or-later
// A card that leaves Settings (a web page, another app): the whole row
// is the button, with the open-elsewhere glyph. With `url` set it opens
// it; otherwise the page handles clicked().
import QtQuick

Card {
    id: card

    property url url

    clickable: true
    actionGlyph: "open-external"
    Accessible.role: Accessible.Link
    onClicked: if (url.toString() !== "") Qt.openUrlExternally(url)
}
