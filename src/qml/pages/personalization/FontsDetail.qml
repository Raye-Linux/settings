// SPDX-License-Identifier: GPL-3.0-or-later
// Personalization > Fonts > <family>: the family at several sizes, a
// box to try your own text, its faces, and Uninstall when the user
// installed it (a font of the system stays).
import QtQuick
import QtQuick.Layouts
import org.raye.settings

PageScaffold {
    id: page

    // FontSettings.family(): { family, styles, userFont, faceCount }
    property var info: ({})
    readonly property string family: info.family || ""

    signal backRequested()
    signal uninstallRequested()

    showBack: true
    crumbs: [{ text: i18nc("@title settings category", "Personalization"), target: "personalization" },
             { text: i18nc("@title settings page", "Fonts"), target: "personalization/fonts" },
             { text: family }]
    onBackClicked: backRequested()
    onCrumbActivated: (target) => {
        if (target === "personalization/fonts")
            backRequested()
        else if (target !== "")
            Router.navigate(target)
    }

    TextField {
        id: sample
        Layout.fillWidth: true
        Layout.maximumWidth: 480
        placeholderText: i18nc("@info:placeholder", "Type here to preview")
        Accessible.name: i18nc("@info:placeholder", "Type here to preview")
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.topMargin: Theme.s4
        implicitHeight: sizes.implicitHeight + 2 * Theme.cardPadding
        radius: Theme.radiusControl
        color: Theme.card
        border.width: 1
        border.color: Theme.cardBorder
        Column {
            id: sizes
            x: Theme.cardPadding
            y: Theme.cardPadding
            width: parent.width - 2 * Theme.cardPadding
            spacing: Theme.s3
            Repeater {
                model: [12, 18, 24, 36, 48, 72]
                delegate: Row {
                    required property int modelData
                    width: sizes.width
                    spacing: Theme.s4
                    Text {
                        width: 40
                        anchors.baseline: line.baseline
                        text: modelData
                        font: Theme.caption
                        color: Theme.textSecondary
                    }
                    Text {
                        id: line
                        width: parent.width - 40 - Theme.s4
                        text: sample.text !== "" ? sample.text
                              : i18nc("@info a sentence to show what a font looks like", "The quick brown fox jumps over the lazy dog.")
                        font.family: page.family
                        font.pointSize: modelData
                        color: Theme.text
                        elide: Text.ElideRight
                        textFormat: Text.PlainText
                    }
                }
            }
        }
    }

    SectionHeader { text: i18nc("@title:group", "Font faces") }
    Repeater {
        model: page.info.styles || []
        delegate: Card {
            id: face
            required property string modelData
            Layout.fillWidth: true
            title: modelData
            Text {
                anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                text: "Aa Bb Cc 123"
                font.family: page.family
                font.styleName: face.modelData
                font.pixelSize: 18
                color: Theme.text
            }
        }
    }

    SectionHeader { text: i18nc("@title:group", "Uninstall") }
    ButtonCard {
        Layout.fillWidth: true
        title: page.info.userFont ? i18nc("@label", "Remove this font from your PC")
                                  : i18nc("@info", "This font is part of Raye and can't be uninstalled")
        buttonText: i18nc("@action:button", "Uninstall")
        enabled: page.info.userFont === true
        onButtonClicked: confirm.open()
    }

    ContentDialog {
        id: confirm
        title: i18nc("@title:window %1 is a font", "Uninstall %1?", page.family)
        text: i18nc("@info", "Apps using it will show another font instead.")
        primaryText: i18nc("@action:button", "Uninstall")
        onAccepted: page.uninstallRequested()
    }
}
