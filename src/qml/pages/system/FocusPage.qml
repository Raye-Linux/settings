// SPDX-License-Identifier: GPL-3.0-or-later
// System › Focus: pick how long, start a focus session; while it runs,
// the time left on a ring and "Stop session". The taskbar clock shows
// the same timer (it reads the end of the session from the shell's
// file). Do not disturb runs with the session unless turned off below.
import QtQuick
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Templates as T
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "System"), target: "system" },
             { text: i18nc("@title settings page", "Focus") }]

    NotificationSettings { id: focus; objectName: "focus" }

    // the time, ticking while a session runs
    property double nowMs: Date.now()
    Timer {
        interval: 1000
        repeat: true
        running: focus.focusActive && page.visible
        triggeredOnStart: true
        onTriggered: page.nowMs = Date.now()
    }

    readonly property double leftMs: focus.focusActive ? Math.max(0, focus.focusUntil.getTime() - nowMs) : 0
    // the length of the running session, for the ring
    property int sessionMinutes: focus.focusMinutes

    function clock(ms) {
        const total = Math.ceil(ms / 1000)
        const h = Math.floor(total / 3600)
        const m = Math.floor((total % 3600) / 60)
        const s = total % 60
        const pad = (n) => (n < 10 ? "0" : "") + n
        return h > 0 ? h + ":" + pad(m) + ":" + pad(s) : pad(m) + ":" + pad(s)
    }
    // Windows steps by 5 minutes up to an hour, then by 15
    function step(minutes, up) {
        if (up)
            return minutes < 60 ? minutes + 5 : minutes + 15
        return minutes <= 60 ? minutes - 5 : minutes - 15
    }
    function minutesText(m) {
        if (m < 60)
            return i18ncp("@label length of a focus session", "%1 min", "%1 mins", m)
        const h = Math.floor(m / 60)
        const rest = m % 60
        return rest === 0 ? i18ncp("@label length of a focus session", "%1 hour", "%1 hours", h)
                          : i18nc("@label length of a focus session, hours and minutes", "%1 h %2 min", h, rest)
    }

    // --- the session --------------------------------------------------
    Rectangle {
        id: hero
        Layout.fillWidth: true
        implicitHeight: heroRow.implicitHeight + 2 * Theme.s7
        radius: Theme.radiusOverlay
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder
        property string searchId: "focus-session"
        function flash() { flashAnim.restart() }
        SequentialAnimation on border.color {
            id: flashAnim
            running: false
            ColorAnimation { to: Theme.accent; duration: Theme.durationFast }
            PauseAnimation { duration: 900 }
            ColorAnimation { to: Theme.cardBorder; duration: Theme.durationNormal }
        }

        RowLayout {
            id: heroRow
            anchors.fill: parent
            anchors.margins: Theme.s7
            spacing: Theme.s7

            // the ring: full at the start, empty at the end
            Item {
                id: ring
                Layout.preferredWidth: 152
                Layout.preferredHeight: 152
                Layout.alignment: Qt.AlignVCenter
                Accessible.role: Accessible.ProgressBar
                Accessible.name: focus.focusActive ? i18nc("@info", "Time left: %1", page.clock(page.leftMs))
                                                   : page.minutesText(focus.focusMinutes)

                readonly property real fraction: focus.focusActive
                                                 ? Math.min(1, page.leftMs / (page.sessionMinutes * 60000)) : 1
                Shape {
                    anchors.fill: parent
                    preferredRendererType: Shape.CurveRenderer
                    ShapePath {
                        strokeColor: Theme.strokeSurface
                        strokeWidth: 8
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        PathAngleArc { centerX: 76; centerY: 76; radiusX: 68; radiusY: 68; startAngle: -90; sweepAngle: 360 }
                    }
                    ShapePath {
                        strokeColor: Theme.accent
                        strokeWidth: 8
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        PathAngleArc {
                            centerX: 76; centerY: 76; radiusX: 68; radiusY: 68; startAngle: -90
                            sweepAngle: 360 * ring.fraction
                            Behavior on sweepAngle { NumberAnimation { duration: Theme.durationNormal } }
                        }
                    }
                }
                Column {
                    anchors.centerIn: parent
                    spacing: 0
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        font: Theme.title
                        color: Theme.text
                        text: focus.focusActive ? page.clock(page.leftMs) : focus.focusMinutes
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        font: Theme.caption
                        color: Theme.textSecondary
                        text: focus.focusActive ? i18nc("@label under the time left", "left")
                                                : i18ncp("@label under the length of a focus session", "min", "mins", focus.focusMinutes)
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: Theme.s3

                Text {
                    Layout.fillWidth: true
                    font: Theme.subtitle
                    color: Theme.text
                    wrapMode: Text.Wrap
                    text: focus.focusActive ? i18nc("@title", "Focus session in progress")
                                            : i18nc("@title", "Start a focus session")
                }
                Text {
                    Layout.fillWidth: true
                    font: Theme.body
                    color: Theme.textSecondary
                    wrapMode: Text.Wrap
                    text: focus.focusActive
                          ? i18nc("@info %1 a time", "It ends at %1. The timer also shows on the taskbar clock.",
                                  focus.focusUntil.toLocaleTimeString(Qt.locale(), Locale.ShortFormat))
                          : i18nc("@info", "Choose how long, then start. A timer shows on the taskbar clock until it ends.")
                }
                RowLayout {
                    Layout.topMargin: Theme.s3
                    spacing: Theme.s3
                    visible: !focus.focusActive

                    Button {
                        text: "−"
                        Accessible.name: i18nc("@action:button", "Less time")
                        enabled: focus.focusMinutes > 5 // NotificationSettings::minFocusMinutes
                        onClicked: focus.focusMinutes = page.step(focus.focusMinutes, false)
                    }
                    Text {
                        Layout.preferredWidth: 96
                        horizontalAlignment: Text.AlignHCenter
                        font: Theme.bodyStrong
                        color: Theme.text
                        text: page.minutesText(focus.focusMinutes)
                    }
                    Button {
                        text: "+"
                        Accessible.name: i18nc("@action:button", "More time")
                        enabled: focus.focusMinutes < 240 // NotificationSettings::maxFocusMinutes
                        onClicked: focus.focusMinutes = page.step(focus.focusMinutes, true)
                    }
                }
                Button {
                    Layout.topMargin: Theme.s3
                    accent: !focus.focusActive
                    text: focus.focusActive ? i18nc("@action:button", "Stop session")
                                            : i18nc("@action:button", "Start focus session")
                    onClicked: {
                        if (focus.focusActive) {
                            focus.stopFocus()
                        } else {
                            page.sessionMinutes = focus.focusMinutes
                            focus.startFocus()
                        }
                    }
                }
            }
        }
    }

    SectionHeader { text: i18nc("@title", "While a session runs") }

    ToggleCard {
        searchId: "focus-do-not-disturb"
        iconName: "org.raye.settings-notifications"
        iconFallback: "notifications-disabled"
        title: i18nc("@title", "Turn on do not disturb")
        description: i18nc("@info", "Notifications go straight to the notification center until the session ends")
        enabled: !focus.focusActive
        checked: focus.focusDoNotDisturb
        onToggled: focus.focusDoNotDisturb = checked
    }

    SectionHeader { text: i18nc("@title", "Related settings") }

    NavCard {
        iconName: "org.raye.settings-notifications"
        iconFallback: "preferences-desktop-notification-bell"
        title: i18nc("@title settings page", "Notifications")
        description: i18nc("@info", "Do not disturb, and notifications from each app")
        target: "system/notifications"
    }
}
