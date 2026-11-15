// SPDX-License-Identifier: GPL-3.0-or-later
// "Network reset": what it does before it does it.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    title: i18nc("@title:window", "Reset your network settings?")
    // NetworkText::wording().resetWarning of the page's NetworkStatus
    property string warning
    text: warning
    primaryText: i18nc("@action:button", "Reset now")
    closeText: i18nc("@action:button", "Cancel")
}
