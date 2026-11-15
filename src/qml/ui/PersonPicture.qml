// SPDX-License-Identifier: GPL-3.0-or-later
// A round account picture: the user's photo, or their initials on the
// accent gradient when there is none.
import QtQuick

Item {
    id: picture

    property url source
    // the name the initials come from ("Alex Martin" gives AM)
    property string name
    property real size: 72

    readonly property string initials: {
        const words = name.trim().split(/\s+/).filter(w => w.length > 0)
        if (words.length === 0)
            return ""
        const first = words[0].charAt(0)
        const last = words.length > 1 ? words[words.length - 1].charAt(0) : ""
        return (first + last).toLocaleUpperCase()
    }

    implicitWidth: size
    implicitHeight: size
    Accessible.role: Accessible.Graphic
    Accessible.name: name

    Rectangle {
        anchors.fill: parent
        radius: width / 2
        visible: !picture.hasPicture
        gradient: Gradient {
            orientation: Gradient.Vertical
            GradientStop { position: 0; color: Theme.accent }
            GradientStop { position: 1; color: Theme.accentGlowEnd }
        }
        Text {
            anchors.centerIn: parent
            text: picture.initials
            font.family: Theme.subtitle.family
            font.weight: Font.DemiBold
            font.pixelSize: Math.round(picture.size * 0.36)
            color: Theme.textOnAccent
            Accessible.ignored: true
        }
    }

    // The photo, cut round by a canvas, which does it the same way on
    // the GPU and in software rendering. The canvas loads the file itself:
    // handed an Image item, it reloads the file by its address, draws
    // nothing until that is done and never paints again. `shape` reads
    // only the proportions of the photo, from a small copy.
    Image {
        id: shape
        source: picture.source
        visible: false
        asynchronous: true
        sourceSize.width: 128
        sourceSize.height: 128
    }
    property url loaded
    onSourceChanged: load()
    Component.onCompleted: load()
    function load() {
        if (picture.loaded.toString() !== "")
            round.unloadImage(picture.loaded)
        picture.loaded = picture.source
        if (picture.source.toString() !== "")
            round.loadImage(picture.source)
        round.requestPaint()
    }
    readonly property bool hasPicture: shape.status === Image.Ready && round.ready
    Canvas {
        id: round
        property bool ready: false
        anchors.fill: parent
        onWidthChanged: requestPaint()
        onImageLoaded: requestPaint()
        onAvailableChanged: requestPaint()
        onVisibleChanged: requestPaint()
        Connections {
            target: shape
            function onStatusChanged() { round.requestPaint() }
        }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const url = picture.source
            ready = url.toString() !== "" && isImageLoaded(url)
            if (!ready || shape.status !== Image.Ready || shape.implicitHeight <= 0)
                return
            ctx.save()
            ctx.beginPath()
            ctx.arc(width / 2, height / 2, Math.min(width, height) / 2, 0, 2 * Math.PI)
            ctx.closePath()
            ctx.clip()
            // cover the circle, centred: the longer side runs past it
            const aspect = shape.implicitWidth / shape.implicitHeight
            const w = aspect >= 1 ? height * aspect : width
            const h = aspect >= 1 ? height : width / aspect
            ctx.drawImage(url, (width - w) / 2, (height - h) / 2, w, h)
            ctx.restore()
        }
    }
    Rectangle {
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: 1
        border.color: Theme.strokeSurface
    }
}
