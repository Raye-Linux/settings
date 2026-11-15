// SPDX-License-Identifier: GPL-3.0-or-later
// Battery level over the last 24 hours, one bar an hour (Windows'
// battery usage chart): violet while on battery, green while charging,
// nothing where the battery said nothing.
import QtQuick
import QtQuick.Layouts
import org.raye.settings

Item {
    id: chart

    // [{ start (ms), level (-1 unknown), charging }], 24 of them
    property var history: []
    readonly property bool empty: history.every(h => h.level < 0)

    // "06:00", or "6 AM" where the clock has twelve hours
    function hourText(date) {
        const twelve = Qt.locale().timeFormat(Locale.ShortFormat).indexOf("AP") >= 0
                       || Qt.locale().timeFormat(Locale.ShortFormat).indexOf("ap") >= 0
        return Qt.formatTime(date, twelve ? "h AP" : "HH:mm")
    }

    implicitHeight: 196
    implicitWidth: 480

    Accessible.role: Accessible.Chart
    Accessible.name: i18nc("@info", "Battery level in the last 24 hours")

    // scale: 0, 50 and 100 % lines
    Repeater {
        model: [100, 50, 0]
        delegate: Item {
            required property int modelData
            x: 0
            y: plot.y + plot.height * (1 - modelData / 100)
            width: chart.width
            Rectangle {
                x: 40
                width: parent.width - 40
                height: 1
                color: Theme.divider
            }
            Text {
                anchors.verticalCenter: parent.top
                width: 32
                horizontalAlignment: Text.AlignRight
                font: Theme.caption
                color: Theme.textSecondary
                text: i18nc("@label chart scale, percent", "%1%", modelData)
            }
        }
    }

    Item {
        id: plot
        x: 48
        y: Theme.s3
        width: chart.width - x
        height: chart.height - Theme.s3 - 28

        Row {
            anchors.fill: parent
            spacing: 3
            Repeater {
                model: chart.history
                delegate: Item {
                    required property var modelData
                    required property int index
                    width: (plot.width - 23 * 3) / 24
                    height: plot.height
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: modelData.level < 0 ? 0 : Math.max(2, parent.height * modelData.level / 100)
                        radius: Math.min(3, width / 2)
                        color: modelData.charging ? Theme.success : Theme.accent
                        opacity: 0.9
                        Behavior on height { NumberAnimation { duration: Theme.durationNormal; easing.type: Theme.easingEnter } }
                    }
                }
            }
        }
        Text {
            anchors.centerIn: parent
            visible: chart.empty
            font: Theme.body
            color: Theme.textSecondary
            text: i18nc("@info", "No battery history yet")
        }
    }

    // hours under the chart: every six hours, and "Now"
    Repeater {
        model: chart.history.length === 24 ? [0, 6, 12, 18] : []
        delegate: Text {
            required property int modelData
            x: plot.x + modelData * (plot.width / 24)
            y: plot.y + plot.height + Theme.s2
            font: Theme.caption
            color: Theme.textSecondary
            text: chart.hourText(new Date(chart.history[modelData].start))
        }
    }
    Text {
        visible: chart.history.length === 24
        anchors.right: parent.right
        y: plot.y + plot.height + Theme.s2
        font: Theme.caption
        color: Theme.textSecondary
        text: i18nc("@label the right end of the battery chart", "Now")
    }
}
