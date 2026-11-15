// SPDX-License-Identifier: GPL-3.0-or-later
// Time & language > Language & region: the language Raye shows, and the
// regional format of dates, times and numbers with examples. Both take
// effect at the next sign-in; the page says so and offers to sign out.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Time & language"), target: "time" },
             { text: i18nc("@title settings page", "Language & region") }]

    RegionSettings { id: region }

    Connections {
        target: Router
        function onAnchorRequested(anchor) {
            if (anchor === "regional-format")
                formatExpander.expanded = true
        }
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: region.signOutNeeded
        severity: "informational"
        closable: false
        title: i18n("Sign out to finish")
        message: i18n("Raye shows the new language and formats the next time you sign in.")

        Button {
            text: i18nc("@action:button end the session", "Sign out")
            onClicked: region.signOut()
        }
    }

    SectionHeader { text: i18n("Language"); first: true }

    ComboCard {
        id: languageCard
        iconName: "org.raye.settings-language"
        iconFallback: "preferences-desktop-locale"
        title: i18n("Display language")
        description: i18n("Settings, the Start menu and apps that support it appear in this language")
        searchId: "display-language"
        model: region.languages
        textRole: "name"
        valueRole: "code"
        currentIndex: indexOfValue(region.language)
        onActivated: (index) => region.setLanguage(currentValue)
        // the list is not there yet at the first binding: look again
        Component.onCompleted: currentIndex = indexOfValue(region.language)
        Connections {
            target: region
            function onChanged() { languageCard.currentIndex = languageCard.indexOfValue(region.language) }
        }
    }

    Card {
        iconName: "org.raye.settings-language"
        iconFallback: "list-add"
        title: i18n("Add a language")
        description: i18n("Install another language, set the order of your languages and more")
        searchId: "add-language"
        clickable: true
        actionGlyph: "open-external"
        onClicked: region.openMoreOptions()
    }

    SectionHeader { text: i18n("Region") }

    Expander {
        id: formatExpander
        iconName: "org.raye.settings-language"
        iconFallback: "preferences-desktop-locale"
        title: i18n("Regional format")
        description: i18n("Raye and some apps format dates and times based on your regional format")
        searchId: "regional-format"

        headerContent: [
            ComboBox {
                id: formatBox
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                readonly property var choices: [{ locale: "", name: i18nc("@item regional format, %1 a language", "Recommended (%1)", region.languageName) }]
                                               .concat(region.formats)
                model: choices
                textRole: "name"
                valueRole: "locale"
                currentIndex: Math.max(0, indexOfValue(region.format))
                Accessible.name: i18n("Regional format") + ", " + displayText
                Component.onCompleted: currentIndex = Math.max(0, indexOfValue(region.format))
                onActivated: (index) => region.setFormat(currentValue)
                Connections {
                    target: region
                    function onChanged() { formatBox.currentIndex = Math.max(0, formatBox.indexOfValue(region.format)) }
                }
            }
        ]

        Repeater {
            model: [
                { label: i18n("First day of the week"), key: "firstDay" },
                { label: i18n("Short date"), key: "shortDate" },
                { label: i18n("Long date"), key: "longDate" },
                { label: i18n("Short time"), key: "shortTime" },
                { label: i18n("Long time"), key: "longTime" },
                { label: i18n("Numbers"), key: "number" },
                { label: i18n("Currency"), key: "currency" }
            ]
            ExpanderRow {
                required property var modelData
                title: modelData.label
                Text {
                    anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                    text: region.samples[modelData.key] || ""
                    font: Theme.body
                    color: Theme.textSecondary
                }
            }
        }
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-typing"
        iconFallback: "input-keyboard"
        title: i18n("Typing")
        description: i18n("Keyboard layouts and key repeat")
        target: "time/typing"
    }
    NavCard {
        iconName: "org.raye.settings-date-time"
        iconFallback: "preferences-system-time"
        title: i18n("Date & time")
        description: i18n("Time zone and clock")
        target: "time/date-time"
    }
}
