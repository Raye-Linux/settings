// SPDX-License-Identifier: GPL-3.0-or-later
// The "..." menu of a Bluetooth device (Windows: Connect or Disconnect,
// Remove device), opened under its button.
//
//   BluetoothDeviceMenu {
//       actions: [{ text: i18n("Disconnect"), id: "disconnect" }, ...]
//       onTriggered: (id) => ...
//   }
import QtQuick
import org.raye.settings

ActionMenu {}
