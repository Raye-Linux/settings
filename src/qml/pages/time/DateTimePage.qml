// SPDX-License-Identifier: GPL-3.0-or-later
// Time & language > Date & time: the time now, big; set it
// automatically or by hand, the time zone with a search, and how the
// taskbar clock looks. Changes apply at once (the password is asked
// when the system needs it).
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Time & language"), target: "time" },
             { text: i18nc("@title settings page", "Date & time") }]

    // why the last change did not go through, "" when it did
    property string problem: ""

    DateTimeSettings {
        id: clock
        onDone: (ok, error) => {
            page.problem = ok || error === "denied" ? "" : i18n("Raye couldn't change the time settings. Try again.")
        }
    }

    // the time now, as Windows shows it at the top of the page
    Item {
        Layout.fillWidth: true
        implicitHeight: clockColumn.implicitHeight + Theme.s4
        Accessible.role: Accessible.StaticText
        Accessible.name: clock.timeText + ", " + clock.dateText

        Column {
            id: clockColumn
            spacing: Theme.s1
            Text {
                text: clock.timeText
                font.family: Theme.titleLarge.family
                font.pixelSize: 40
                font.weight: Font.DemiBold
                color: Theme.text
                Accessible.ignored: true
            }
            Text {
                text: clock.dateText
                font: Theme.body
                color: Theme.textSecondary
                Accessible.ignored: true
            }
        }
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: clock.ready && !clock.available
        severity: "warning"
        closable: false
        title: i18n("The time can't be changed right now")
        message: i18n("The service that keeps the clock of this PC isn't answering. Restart the PC, then try again.")
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.problem !== ""
        severity: "error"
        title: i18n("Something went wrong")
        message: page.problem
        onClosed: page.problem = ""
    }

    ToggleCard {
        iconName: "org.raye.settings-date-time"
        iconFallback: "preferences-system-time"
        title: i18n("Set time automatically")
        description: clock.canAutomaticTime || !clock.available
                     ? i18n("Keep the clock right with the time from the internet")
                     : i18n("No time service is installed on this PC")
        searchId: "set-time-automatically"
        enabled: clock.available && clock.canAutomaticTime && !clock.busy
        checked: clock.automaticTime
        onToggled: clock.setAutomaticTime(checked)
    }

    Card {
        id: zoneCard
        iconName: "org.raye.settings-time"
        iconFallback: "preferences-system-time"
        title: i18n("Time zone")
        searchId: "time-zone"
        enabled: clock.available && !clock.busy

        TimeZoneButton {
            id: zoneButton
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: clock.timeZoneLabel
            Accessible.name: i18n("Time zone") + ", " + clock.timeZoneLabel
            onClicked: zonePicker.open()
        }
    }

    TimeZonePicker {
        id: zonePicker
        current: clock.timeZone
        anchorItem: zoneButton
        onPicked: (id) => clock.setTimeZone(id)
    }

    ButtonCard {
        iconName: "org.raye.settings-date-time"
        iconFallback: "view-calendar"
        title: i18n("Set the date and time manually")
        description: clock.automaticTime ? i18n("Turn off \"Set time automatically\" to change it") : ""
        searchId: "set-time-manually"
        buttonText: i18nc("@action:button change the date and time", "Change")
        enabled: clock.available && !clock.automaticTime && !clock.busy
        onButtonClicked: dateDialog.openAt(clock.nowParts())
    }

    DateTimeDialog {
        id: dateDialog
        use24h: clock.clockFormat === 1 || (clock.clockFormat === 0 && Qt.locale().timeFormat(Locale.ShortFormat).toLowerCase().indexOf("a") < 0)
        onChosen: (when) => clock.setDateTime(when)
    }

    SectionHeader { text: i18n("Additional settings") }

    Card {
        iconName: "org.raye.settings-date-time"
        iconFallback: "view-refresh"
        title: i18n("Sync now")
        description: clock.lastSyncText !== ""
                     ? i18n("Last successful time synchronization: %1", clock.lastSyncText)
                     : i18n("Set the clock from the internet now")
        searchId: "sync-now"
        enabled: clock.available

        Button {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: i18nc("@action:button set the clock from the internet", "Sync now")
            enabled: clock.automaticTime && !clock.busy
            onClicked: clock.syncNow()
        }
    }

    ToggleCard {
        iconName: "org.raye.settings-taskbar"
        iconFallback: "view-calendar-day"
        title: i18n("Show the date in the system tray")
        searchId: "show-date"
        visible: clock.showDateAvailable
        checked: clock.showDate
        onToggled: clock.showDate = checked
    }

    ToggleCard {
        iconName: "org.raye.settings-taskbar"
        iconFallback: "chronometer"
        title: i18n("Show seconds in the system tray clock")
        description: i18n("Uses a little more power")
        searchId: "show-seconds"
        visible: clock.showSecondsAvailable
        checked: clock.showSeconds
        onToggled: clock.showSeconds = checked
    }

    ComboCard {
        iconName: "org.raye.settings-date-time"
        iconFallback: "chronometer"
        title: i18n("Time format")
        description: i18n("How the clock shows hours")
        searchId: "time-format"
        visible: clock.clockFormatAvailable
        model: [i18nc("@item time format", "Automatic (from your region)"),
                i18nc("@item time format", "24-hour (14:30)"),
                i18nc("@item time format", "12-hour (2:30 PM)")]
        currentIndex: clock.clockFormat
        onActivated: (index) => clock.clockFormat = index
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-language"
        iconFallback: "preferences-desktop-locale"
        title: i18n("Language & region")
        description: i18n("Display language, date and number formats")
        target: "time/language"
    }
}
