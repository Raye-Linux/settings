// SPDX-License-Identifier: GPL-3.0-or-later
// One row of a Settings page, the way Windows draws every setting:
// icon, title, an optional line under it, and the control on the right.
// Anything put inside the card is that control (a switch, a list, a
// button). Below 476 px (286 without an icon) the control moves under
// the text. NavCard, LinkCard, ToggleCard, ComboCard, ButtonCard,
// SliderCard and InfoCard are this card with their control in place.
//
//   Card {
//       iconName: "org.raye.settings-display"
//       title: i18n("Brightness")
//       description: i18n("Change the brightness of the built-in display")
//       searchId: "brightness"
//       Slider { ... }
//   }
import QtQuick
import QtQuick.Layouts
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami

T.AbstractButton {
    id: card

    property string title
    property string description
    // a Raye glyph name, with the icon theme's name as the fallback
    property string iconName
    property string iconFallback
    property url iconSource
    // the anchor a link or a search result scrolls to ("brightness")
    property string searchId
    // the whole card reacts to the pointer and the keyboard
    property bool clickable: false
    // "chevron-right" (opens a page), "open-external" (leaves the app),
    // "chevron-down" (expander), "" (none)
    property string actionGlyph: ""
    property real actionRotation: 0
    // the expander draws its body below: square bottom corners then
    property bool squareBottom: false
    // for the gallery: "hover", "pressed" or "focus" shown at rest
    property string previewState: ""

    default property alias content: trailing.data
    readonly property alias controlItem: trailing

    signal linkActivated(string link)

    readonly property bool hasIcon: iconName !== "" || iconSource.toString() !== ""
    readonly property bool hasControl: trailing.children.length > 0 && trailing.childrenRect.width > 0
    readonly property bool wrapped: hasControl
                                    && width < (hasIcon ? Theme.cardWrapWidth : Theme.cardWrapWidthNoIcon)
    readonly property bool showHover: clickable && enabled && (hovered || previewState === "hover")
    readonly property bool showPressed: clickable && enabled && (down || previewState === "pressed")
    readonly property real textLeft: hasIcon ? Theme.cardTextLeft : Theme.cardPadding
    readonly property real actionWidth: actionGlyph !== "" ? Theme.actionIconSize + Theme.cardActionGap : 0
    readonly property real controlWidth: hasControl ? Math.max(Theme.controlMinWidth, trailing.implicitWidth) : 0

    Layout.fillWidth: true
    implicitWidth: 560
    implicitHeight: Math.max(description !== "" ? Theme.cardMinHeight : Theme.cardMinHeightCompact,
                             2 * Theme.cardPadding
                             + (wrapped ? header.implicitHeight + Theme.s3 + trailing.implicitHeight
                                        : Math.max(header.implicitHeight, trailing.implicitHeight)))

    hoverEnabled: clickable
    focusPolicy: clickable ? Qt.StrongFocus : Qt.NoFocus

    Accessible.role: clickable ? Accessible.Button : Accessible.Grouping
    Accessible.name: title
    Accessible.description: description

    Keys.onReturnPressed: if (clickable) card.click()
    Keys.onEnterPressed: if (clickable) card.click()
    // arrows walk from card to card, the way they do in Windows' lists
    Keys.onDownPressed: (event) => card._step(true, event)
    Keys.onUpPressed: (event) => card._step(false, event)

    // a position from the left edge, mirrored for right-to-left languages
    function _place(left, itemWidth, areaWidth) {
        return mirrored ? areaWidth - left - itemWidth : left
    }

    function _step(forward, event) {
        const next = card.nextItemInFocusChain(forward)
        if (next && next !== card) {
            next.forceActiveFocus(forward ? Qt.TabFocusReason : Qt.BacktabFocusReason)
            event.accepted = true
        } else {
            event.accepted = false
        }
    }

    // Draws the eye to the row: an accent outline that fades out.
    function flash() {
        flashAnimation.restart()
    }

    background: Item {
        // the card, with square bottom corners when an expander body hangs below
        Item {
            anchors.fill: parent
            clip: card.squareBottom
            Rectangle {
                width: parent.width
                height: parent.height + (card.squareBottom ? radius : 0)
                radius: Theme.radiusControl
                color: card.showPressed ? Theme.cardPressed
                     : card.showHover ? Theme.cardHover : Theme.card
                border.width: 1
                border.color: Theme.cardBorder
                Behavior on color { ColorAnimation { duration: Theme.durationFaster } }
            }
        }
        // the lit top edge
        Item {
            width: parent.width
            height: 1
            clip: true
            visible: Theme.night
            Rectangle {
                width: parent.width
                height: Theme.radiusControl * 2
                radius: Theme.radiusControl
                color: "transparent"
                border.width: 1
                border.color: Theme.cardTopEdge
            }
        }
        Rectangle {
            id: flashFrame
            anchors.fill: parent
            radius: Theme.radiusControl
            color: "transparent"
            border.width: 2
            border.color: Theme.accent
            opacity: 0
            SequentialAnimation {
                id: flashAnimation
                NumberAnimation { target: flashFrame; property: "opacity"; to: 1; duration: Theme.durationFast }
                PauseAnimation { duration: Math.max(0, Theme.durationFlash - Theme.durationFast - Theme.durationNormal) }
                NumberAnimation { target: flashFrame; property: "opacity"; to: 0; duration: Theme.durationNormal }
            }
        }
        FocusRing {
            shown: card.clickable && (card.visualFocus || card.previewState === "focus")
            radius: Theme.radiusControl
        }
    }

    contentItem: Item {
        Kirigami.Icon {
            id: icon
            x: card._place(Theme.cardIconLeft - card.leftPadding, width, parent.width)
            width: Theme.iconSize
            height: Theme.iconSize
            anchors.verticalCenter: header.verticalCenter
            visible: card.hasIcon
            source: card.iconSource.toString() !== "" ? card.iconSource : card.iconName
            fallback: card.iconFallback
            opacity: card.enabled ? 1 : 0.4
        }

        Column {
            id: header
            readonly property real textX: card.textLeft - card.leftPadding
            x: card._place(textX, width, parent.width)
            y: card.wrapped ? 0 : Math.round((parent.height - height) / 2)
            width: Math.max(0, parent.width - textX
                            - (card.wrapped ? 0 : card.controlWidth + (card.hasControl ? Theme.cardHeaderGap : 0))
                            - card.actionWidth)

            Text {
                id: titleText
                width: parent.width
                text: card.title
                font: Theme.body
                horizontalAlignment: Text.AlignLeft
                color: !card.enabled ? Theme.textDisabled
                     : card.showPressed ? Theme.textSecondary : Theme.text
                wrapMode: Text.Wrap
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.bodyLineHeight
                verticalAlignment: Text.AlignVCenter
                Accessible.ignored: true
            }
            Text {
                id: descriptionText
                width: parent.width
                visible: card.description !== ""
                text: card.description
                font: Theme.caption
                horizontalAlignment: Text.AlignLeft
                color: card.enabled ? Theme.textSecondary : Theme.textDisabled
                linkColor: Theme.accentText
                textFormat: Text.StyledText
                wrapMode: Text.Wrap
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.captionLineHeight
                verticalAlignment: Text.AlignVCenter
                onLinkActivated: (link) => card.linkActivated(link)
                Accessible.ignored: true
                HoverHandler {
                    enabled: descriptionText.hoveredLink !== ""
                    cursorShape: Qt.PointingHandCursor
                }
            }
        }

        // the control: right-aligned beside the text, or under it
        Row {
            id: trailing
            spacing: Theme.s3
            x: card.wrapped ? card._place(header.textX, width, parent.width)
                            : card._place(parent.width - card.actionWidth - width, width, parent.width)
            y: card.wrapped ? header.height + Theme.s3 : Math.round((parent.height - height) / 2)
        }

        Glyph {
            id: action
            visible: card.actionGlyph !== ""
            name: card.actionGlyph
            size: Theme.actionIconSize
            rotation: card.actionRotation
            color: card.enabled ? Theme.textSecondary : Theme.textDisabled
            x: card._place(parent.width - width, width, parent.width)
            anchors.verticalCenter: card.wrapped ? header.verticalCenter : parent.verticalCenter
            Behavior on rotation { NumberAnimation { duration: Theme.durationFast; easing.type: Theme.easingFast } }
        }
    }

    padding: Theme.cardPadding
}
