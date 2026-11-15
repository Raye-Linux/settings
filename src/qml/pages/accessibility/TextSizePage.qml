// SPDX-License-Identifier: GPL-3.0-or-later
// Accessibility > Text size: one slider for every font of the desktop,
// a live preview, and Apply (as Windows: the size changes only then).
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    crumbs: [{ text: i18nc("@title settings category", "Accessibility"), target: "accessibility" },
             { text: i18nc("@title settings page", "Text size") }]

    TextSizeSettings { id: textSize }

    // what the slider shows; the desktop changes only on Apply
    property int pending: textSize.percent
    property bool applied: false

    // the preview: how text will look, at the size on the slider
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 148
        radius: Theme.radiusOverlay
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder
        clip: true
        Accessible.role: Accessible.StaticText
        Accessible.name: previewText.text

        Text {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.s5
            text: i18n("Text size preview")
            font: Theme.caption
            color: Theme.textSecondary
            Accessible.ignored: true
        }
        Text {
            id: previewText
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.verticalCenterOffset: Theme.s3
            anchors.leftMargin: Theme.s5
            anchors.rightMargin: Theme.s5
            text: i18n("Drag the slider until the text here is easy to read, then select Apply.")
            font.family: textSize.family
            font.pointSize: textSize.basePointSize * page.pending / 100
            color: Theme.text
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            Behavior on font.pointSize { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingFast } }
        }
    }

    Card {
        iconName: "org.raye.settings-text-size"
        iconFallback: "format-font-size-more"
        title: i18n("Text size")
        description: i18n("Make text bigger everywhere in Raye and your apps")
        searchId: "text-size"

        Row {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            spacing: Theme.s3

            Text {
                anchors.verticalCenter: parent.verticalCenter
                width: 44
                text: i18nc("@label percent", "%1%", page.pending)
                font: Theme.body
                color: Theme.textSecondary
                horizontalAlignment: Text.AlignRight
                Accessible.ignored: true
            }
            Slider {
                id: slider
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.sliderWidth
                from: 100
                to: 225
                stepSize: 5
                snapMode: Slider.SnapAlways
                value: textSize.percent
                Accessible.name: i18n("Text size")
                onMoved: {
                    page.pending = Math.round(value)
                    page.applied = false
                }
            }
            Button {
                anchors.verticalCenter: parent.verticalCenter
                text: i18nc("@action:button apply the text size", "Apply")
                accent: true
                enabled: page.pending !== textSize.percent
                onClicked: {
                    textSize.apply(page.pending)
                    page.applied = true
                }
            }
        }
    }

    InfoBar {
        Layout.fillWidth: true
        Layout.bottomMargin: Theme.s3
        open: page.applied
        severity: "success"
        title: i18n("Text size changed")
        message: i18n("Some apps show the new size after you close and open them again.")
        onClosed: page.applied = false
    }

    SectionHeader { text: i18n("Related settings") }

    NavCard {
        iconName: "org.raye.settings-fonts"
        iconFallback: "preferences-desktop-font"
        title: i18n("Fonts")
        description: i18n("Choose and install fonts")
        target: "personalization/fonts"
    }
    NavCard {
        iconName: "org.raye.settings-display"
        iconFallback: "video-display"
        title: i18n("Display")
        description: i18n("Make everything bigger with Scale")
        target: "system/display"
    }
}
