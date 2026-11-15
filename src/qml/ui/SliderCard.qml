// SPDX-License-Identifier: GPL-3.0-or-later
// A setting with a level: the value as text, then a 240 px slider.
// moved() comes while the user drags; changes apply as it moves.
import QtQuick

Card {
    id: card

    property alias from: slider.from
    property alias to: slider.to
    property alias value: slider.value
    property alias stepSize: slider.stepSize
    property alias textFromValue: slider.textFromValue
    readonly property alias slider: slider
    // show the value beside the slider
    property bool showValue: true

    signal moved()

    Text {
        anchors.verticalCenter: parent ? parent.verticalCenter : undefined
        visible: card.showValue
        text: slider.textFromValue(slider.value)
        font: Theme.body
        color: card.enabled ? Theme.textSecondary : Theme.textDisabled
        Accessible.ignored: true
    }
    Slider {
        id: slider
        enabled: card.enabled
        width: Theme.sliderWidth
        snapMode: stepSize > 0 ? Slider.SnapAlways : Slider.NoSnap
        Accessible.name: card.title
        onMoved: card.moved()
    }
}
