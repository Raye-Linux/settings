// SPDX-License-Identifier: GPL-3.0-or-later
// "Find a setting": the search box of Settings. On the launch screen it
// is a pill 48 px high (the one place Raye is rounder than Windows); in
// the navigation pane, compact, a 36 px field. Typing drops the
// suggestions under it. Up and Down walk them, Enter opens the one
// chosen, or the results page when none is; Esc closes them, then
// clears the box.
//
//   SearchField {
//       onShowAllResults: (text) => frame.showSearchResults(text)
//   }
import QtQuick
import QtQuick.Templates as T
import org.kde.kirigami as Kirigami
import org.raye.settings

FocusScope {
    id: field

    property bool compact: false
    property alias text: input.text
    property string placeholderText: i18n("Find a setting")
    // a chosen suggestion opens its page through the router; off when
    // whoever holds the box wants to do it
    property bool navigateOnChoose: true
    // for the gallery: "hover" or "focus" shown at rest
    property string previewState: ""
    readonly property alias suggestions: flyout
    readonly property alias model: results

    // a suggestion was chosen ("system/display#night-light")
    signal chosen(string entryId)
    // Enter on the text, or "Show all results"
    signal showAllResults(string text)

    readonly property bool showFocus: input.activeFocus || previewState === "focus"
    readonly property bool showHover: hover.hovered || previewState === "hover"
    readonly property real radius: compact ? Theme.radiusControl : height / 2

    implicitWidth: compact ? 268 : 640
    implicitHeight: compact ? 36 : 48

    // Typing anywhere on the launch screen lands here: the screen hands
    // the first key over with this.
    function startTyping(text) {
        input.forceActiveFocus()
        if (text)
            input.insert(input.cursorPosition, text)
    }

    function choose(entryId) {
        if (entryId === "")
            return
        flyout.close()
        if (navigateOnChoose)
            Router.navigate(entryId)
        chosen(entryId)
    }

    function showAll() {
        if (input.text.trim() === "")
            return
        flyout.close()
        showAllResults(input.text)
    }

    function _syncFlyout() {
        if (input.activeFocus && input.text.trim() !== "")
            flyout.open()
        else
            flyout.close()
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.IBeamCursor
    }
    TapHandler {
        onTapped: input.forceActiveFocus()
    }

    // the magenta light of the focused pill
    Glow {
        visible: field.showFocus && !field.compact
        radius: field.radius
        spread: 12
    }

    Rectangle {
        id: frame
        anchors.fill: parent
        radius: field.radius
        color: field.showFocus ? Theme.inputActive : field.showHover ? Theme.controlHover : Theme.control
        border.width: field.showFocus && !field.compact ? 2 : 1
        border.color: field.showFocus && !field.compact ? Theme.accent : Theme.strokeControl
        Behavior on color { ColorAnimation { duration: Theme.durationFaster } }

        // the compact field says it has the focus the way text fields do
        Rectangle {
            visible: field.compact
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 1
            anchors.rightMargin: 1
            height: field.showFocus ? 2 : 1
            radius: 1
            color: field.showFocus ? Theme.accent : Theme.strokeControlBottom
        }
    }

    Glyph {
        id: magnifier
        x: field.compact ? Theme.s4 - 2 : Theme.s6
        anchors.verticalCenter: parent.verticalCenter
        name: "search"
        size: field.compact ? 16 : 20
        color: field.showFocus ? Theme.accentText : Theme.textSecondary
    }

    TextInput {
        id: input
        anchors.left: magnifier.right
        anchors.leftMargin: field.compact ? Theme.s3 + 2 : Theme.s4
        anchors.right: clear.visible ? clear.left : parent.right
        anchors.rightMargin: clear.visible ? Theme.s2 : (field.compact ? Theme.s4 : Theme.s6)
        anchors.verticalCenter: parent.verticalCenter
        focus: true
        clip: true
        font.family: "Inter"
        font.pixelSize: field.compact ? 14 : 16
        color: Theme.text
        selectionColor: Theme.accent
        selectedTextColor: Theme.textOnAccent
        selectByMouse: true
        Accessible.role: Accessible.EditableText
        Accessible.name: field.placeholderText
        Accessible.searchEdit: true

        onTextChanged: {
            results.query = text
            flyout.currentIndex = -1
            field._syncFlyout()
        }
        onActiveFocusChanged: field._syncFlyout()

        Keys.onDownPressed: (event) => {
            if (!flyout.visible)
                field._syncFlyout()
            flyout.move(1)
        }
        Keys.onUpPressed: flyout.move(-1)
        Keys.onReturnPressed: field._activate()
        Keys.onEnterPressed: field._activate()
        Keys.onEscapePressed: (event) => {
            if (flyout.visible) {
                flyout.close()
            } else if (input.text !== "") {
                input.clear()
            } else {
                event.accepted = false
            }
        }

        Text {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            visible: input.text === ""
            text: field.placeholderText
            font: input.font
            color: Theme.textSecondary
            elide: Text.ElideRight
            Accessible.ignored: true
        }
    }

    function _activate() {
        // the newest text, not what was there 120 ms ago
        results.searchNow()
        if (flyout.showAllCurrent || flyout.currentIndex < 0)
            showAll()
        else
            choose(flyout.currentId)
    }

    T.AbstractButton {
        id: clear
        visible: input.text !== ""
        anchors.right: parent.right
        anchors.rightMargin: field.compact ? Theme.s2 : Theme.s4
        anchors.verticalCenter: parent.verticalCenter
        width: 28
        height: 24
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        Accessible.name: i18n("Clear")
        onClicked: {
            input.clear()
            input.forceActiveFocus()
        }
        background: Rectangle {
            radius: Theme.radiusControl
            color: clear.down ? Theme.subtlePressed : clear.hovered ? Theme.subtleHover : "transparent"
        }
        contentItem: Item {
            Glyph {
                anchors.centerIn: parent
                name: "close"
                size: 12
                color: Theme.textSecondary
            }
        }
    }

    SearchModel {
        id: results
        limit: 8
    }

    SuggestionsFlyout {
        id: flyout
        model: results
        parent: field
        y: field.height + Theme.s2
        x: field.compact ? 0 : (field.width - width) / 2
        width: field.compact ? Math.max(field.width, 360) : field.width
        onChosen: (entryId) => field.choose(entryId)
        onShowAll: field.showAll()
    }
}
