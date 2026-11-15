// SPDX-License-Identifier: GPL-3.0-or-later
// Privacy & security › Diagnostics & feedback: Raye itself sends nothing
// about you; the desktop's anonymous usage statistics stay off unless
// the user turns them on.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Privacy & security"), target: "privacy" },
             { text: i18nc("@title settings page", "Diagnostics & feedback") }]

    UsageStatistics { id: statistics }

    SectionHeader {
        text: i18nc("@title:group", "Your data")
        first: true
    }

    Card {
        Layout.fillWidth: true
        searchId: "no-data"
        iconName: "org.raye.settings-diagnostics"
        iconFallback: "help-feedback"
        title: i18nc("@label", "Raye sends no data about you")
        description: i18nc("@info", "No account, no tracking, no ads. What you do on this PC stays on this PC.")
        SecurityStatus { state: "success"; text: "" }
    }

    ToggleCard {
        Layout.fillWidth: true
        searchId: "usage-statistics"
        iconName: "view-statistics"
        title: i18nc("@label", "Send anonymous usage statistics")
        description: i18nc("@info", "Helps KDE, who make the desktop Raye is built on, see which features are used. Nothing that identifies you is sent.")
        checked: statistics.enabled
        onToggled: {
            statistics.enabled = checked
            checked = Qt.binding(() => statistics.enabled)
        }
    }
}
