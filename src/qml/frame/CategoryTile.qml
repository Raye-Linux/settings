// SPDX-License-Identifier: GPL-3.0-or-later
// A tile of the launch screen grid, from an entry of the layout's tile
// list ({ target, name, summary, icon }). A tile that opens a single
// page (Windows programs) shows that page's tile glyph, drawn like the
// category ones.
import QtQuick
import org.raye.settings

Tile {
    id: tile

    property var entry: ({})
    property int tileIndex: 0

    // arrow keys move in the grid; the grid decides where
    signal moveRequested(int index, int dx, int dy)

    target: entry.target || ""
    title: entry.name || ""
    summary: entry.summary || ""
    iconName: target.indexOf("/") >= 0 && (entry.icon || "").indexOf("org.raye.settings-") === 0
              ? entry.icon + "-tile" : (entry.icon || "")
    iconFallback: entry.icon || "preferences-system"

    Keys.onLeftPressed: moveRequested(tileIndex, LayoutMirroring.enabled ? 1 : -1, 0)
    Keys.onRightPressed: moveRequested(tileIndex, LayoutMirroring.enabled ? -1 : 1, 0)
    Keys.onUpPressed: moveRequested(tileIndex, 0, -1)
    Keys.onDownPressed: moveRequested(tileIndex, 0, 1)
}
