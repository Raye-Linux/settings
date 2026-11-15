// SPDX-License-Identifier: GPL-3.0-or-later
// Accessibility > Magnifier: zoom into the screen with the Windows key
// and Plus, the whole screen or a lens around the pointer, and what the
// zoomed view follows.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accessibility"), target: "accessibility" },
             { text: i18nc("@title settings page", "Magnifier") }]

    MagnifierSettings { id: magnifier }

    Connections {
        target: Router
        function onAnchorRequested(anchor) {
            if (["follow-pointer", "pointer-position", "follow-focus", "follow-text-cursor"].indexOf(anchor) >= 0)
                follow.expanded = true
        }
    }

    ToggleCard {
        iconName: "org.raye.settings-magnifier"
        iconFallback: "zoom-in"
        title: i18n("Magnifier")
        description: i18n("Press the Windows key + Plus (+) to zoom in, and the Windows key + Minus (-) to zoom out")
        searchId: "magnifier"
        checked: magnifier.enabled
        onToggled: magnifier.enabled = checked
    }

    ComboCard {
        iconName: "org.raye.settings-magnifier"
        iconFallback: "zoom-select"
        title: i18n("View")
        description: i18n("Zoom the whole screen, or only a lens around the pointer")
        searchId: "magnifier-view"
        enabled: magnifier.enabled
        model: [i18nc("@item magnifier view", "Full screen"), i18nc("@item magnifier view", "Lens")]
        currentIndex: magnifier.view
        onActivated: (index) => magnifier.view = index
    }

    ComboCard {
        iconName: "org.raye.settings-magnifier"
        iconFallback: "zoom-in"
        title: i18n("Zoom increment")
        description: i18n("How much the view grows each time you zoom in")
        searchId: "zoom-increment"
        enabled: magnifier.enabled
        // the steps of MagnifierSettings::zoomSteps
        model: ["10 %", "20 %", "25 %", "50 %", "100 %"]
        currentIndex: magnifier.zoomStep
        onActivated: (index) => magnifier.zoomStep = index
    }

    Expander {
        id: follow
        iconName: "org.raye.settings-magnifier"
        iconFallback: "transform-move"
        title: i18n("What the magnified view follows")
        description: i18n("The pointer, the keyboard and the text cursor")
        searchId: "magnifier-follow"
        enabled: magnifier.enabled

        ExpanderRow {
            title: i18n("Follow the mouse pointer")
            searchId: "follow-pointer"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Follow the mouse pointer")
                checked: magnifier.followPointer
                onToggled: magnifier.followPointer = checked
            }
        }
        ExpanderRow {
            title: i18n("Keep the mouse pointer")
            searchId: "pointer-position"
            enabled: magnifier.followPointer
            ComboBox {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                // the effect's numbers in this order: centred 1, edges 2, proportional 0
                model: [i18n("In the center of the screen"), i18n("Within the edges of the screen"),
                        i18n("Where it would be on the whole screen")]
                currentIndex: [1, 2, 0].indexOf(magnifier.tracking)
                Accessible.name: i18n("Keep the mouse pointer") + ", " + displayText
                onActivated: (index) => magnifier.tracking = [1, 2, 0][index]
            }
        }
        ExpanderRow {
            title: i18n("Follow the keyboard focus")
            searchId: "follow-focus"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Follow the keyboard focus")
                checked: magnifier.followFocus
                onToggled: magnifier.followFocus = checked
            }
        }
        ExpanderRow {
            title: i18n("Follow the text cursor")
            searchId: "follow-text-cursor"
            ToggleSwitch {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: i18n("Follow the text cursor")
                checked: magnifier.followTextCursor
                onToggled: magnifier.followTextCursor = checked
            }
        }
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-text-size"
        iconFallback: "format-font-size-more"
        title: i18n("Text size")
        description: i18n("Make text bigger everywhere")
        target: "accessibility/text-size"
    }
}
