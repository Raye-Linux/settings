// SPDX-License-Identifier: GPL-3.0-or-later
// "Change date and time", as Windows asks it: day, month, year, then
// hour and minute, each a drop-down list. "Change" sets the clock.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

ContentDialog {
    id: dialog

    // hours as 0-23 or 12 AM-11 PM
    property bool use24h: true

    signal chosen(date when)

    title: i18n("Change date and time")
    primaryText: i18nc("@action:button set the clock", "Change")
    onAccepted: {
        const day = Math.min(dayBox.currentIndex + 1, daysIn(yearBox.currentIndex + firstYear, monthBox.currentIndex))
        dialog.chosen(new Date(yearBox.currentIndex + firstYear, monthBox.currentIndex, day,
                               hourBox.currentIndex, minuteBox.currentIndex, 0))
    }

    readonly property int firstYear: 2020

    function daysIn(year, month) {
        return new Date(year, month + 1, 0).getDate()
    }

    // `when`: { year, month (0-11), day, hour, minute } on the PC's clock
    function openAt(when) {
        yearBox.currentIndex = Math.max(0, Math.min(when.year - firstYear, yearBox.count - 1))
        monthBox.currentIndex = when.month
        dayBox.currentIndex = when.day - 1
        hourBox.currentIndex = when.hour
        minuteBox.currentIndex = when.minute
        open()
    }

    function numbers(from, to, width) {
        const list = []
        for (let i = from; i <= to; ++i)
            list.push(width > 0 ? String(i).padStart(width, "0") : String(i))
        return list
    }

    function hourNames() {
        const list = []
        for (let h = 0; h < 24; ++h)
            list.push(use24h ? String(h).padStart(2, "0")
                             : Qt.locale().toString(new Date(2000, 0, 1, h, 0), "h AP"))
        return list
    }

    Text {
        text: i18n("Date")
        font: Theme.bodyStrong
        color: Theme.text
    }
    RowLayout {
        spacing: Theme.s2
        ComboBox {
            id: dayBox
            model: dialog.numbers(1, dialog.daysIn(yearBox.currentIndex + dialog.firstYear, monthBox.currentIndex), 0)
            Accessible.name: i18n("Day") + ", " + displayText
        }
        ComboBox {
            id: monthBox
            Layout.fillWidth: true
            model: {
                const list = []
                for (let m = 1; m <= 12; ++m)
                    list.push(Qt.locale().standaloneMonthName(m - 1, Locale.LongFormat))
                return list
            }
            Accessible.name: i18n("Month") + ", " + displayText
        }
        ComboBox {
            id: yearBox
            model: dialog.numbers(dialog.firstYear, dialog.firstYear + 30, 0)
            Accessible.name: i18n("Year") + ", " + displayText
        }
    }

    Item { implicitHeight: Theme.s3; implicitWidth: 1 }

    Text {
        text: i18n("Time")
        font: Theme.bodyStrong
        color: Theme.text
    }
    RowLayout {
        spacing: Theme.s2
        ComboBox {
            id: hourBox
            model: dialog.hourNames()
            Accessible.name: i18n("Hour") + ", " + displayText
        }
        Text {
            text: ":"
            font: Theme.bodyStrong
            color: Theme.textSecondary
            Accessible.ignored: true
        }
        ComboBox {
            id: minuteBox
            model: dialog.numbers(0, 59, 2)
            Accessible.name: i18n("Minute") + ", " + displayText
        }
    }
}
