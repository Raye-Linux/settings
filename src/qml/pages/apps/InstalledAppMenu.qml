// SPDX-License-Identifier: GPL-3.0-or-later
// The "..." menu of an app (Windows: Modify, Advanced options, Move,
// Uninstall), opened under its button.
//
//   InstalledAppMenu {
//       actions: [{ text: i18n("Open"), id: "open" }, ...]
//       onTriggered: (id) => ...
//   }
import QtQuick
import org.raye.settings

ActionMenu {}
