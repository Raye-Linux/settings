// SPDX-License-Identifier: GPL-3.0-or-later
// The glyphs of Network & internet, drawn on a 24 px grid so they follow
// the text colour at any size: Wi-Fi with 0 to 4 bars lit (and a lock
// for a secured network), Ethernet, VPN, a hotspot, an airplane, and
// "not connected".
import QtQuick
import QtQuick.Shapes
import org.raye.settings

Item {
    id: glyph

    // "wifi", "ethernet", "vpn", "hotspot", "airplane", "none"
    property string kind: "wifi"
    // Wi-Fi: how many of the 4 levels are lit
    property int bars: 4
    property bool locked: false
    property color color: Theme.text
    property color dimColor: Qt.alpha(color, 0.25)
    property real size: 20

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    readonly property real _stroke: 1.75

    Shape {
        width: 24
        height: 24
        scale: glyph.size / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        visible: glyph.kind === "wifi" || glyph.kind === "none"

        // the dot, then three arcs around it (Windows' four levels)
        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillColor: glyph.kind === "wifi" && glyph.bars >= 1 ? glyph.color : glyph.dimColor
            PathSvg { path: "M12 20.5 m-1.6 0 a1.6 1.6 0 1 0 3.2 0 a1.6 1.6 0 1 0 -3.2 0" }
        }
        WifiArc { radius: 5.5; lit: glyph.kind === "wifi" && glyph.bars >= 2 }
        WifiArc { radius: 10; lit: glyph.kind === "wifi" && glyph.bars >= 3 }
        WifiArc { radius: 14.5; lit: glyph.kind === "wifi" && glyph.bars >= 4 }
        // the lock of a secured network, in the free corner at the
        // bottom right: a filled body and its shackle
        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillColor: glyph.locked && glyph.kind === "wifi" ? glyph.color : "transparent"
            PathSvg { path: "M17.75 18.25 L22.25 18.25 Q23 18.25 23 19 L23 22.25 Q23 23 22.25 23 L17.75 23 Q17 23 17 22.25 L17 19 Q17 18.25 17.75 18.25 Z" }
        }
        ShapePath {
            strokeWidth: glyph.locked && glyph.kind === "wifi" ? 1.4 : 0
            strokeColor: glyph.locked && glyph.kind === "wifi" ? glyph.color : "transparent"
            fillColor: "transparent"
            capStyle: ShapePath.FlatCap
            PathSvg { path: "M18.4 18.25 L18.4 16.9 A1.6 1.6 0 0 1 21.6 16.9 L21.6 18.25" }
        }
        // not connected: a small cross at the bottom right
        ShapePath {
            strokeWidth: glyph.kind === "none" ? glyph._stroke : 0
            strokeColor: glyph.kind === "none" ? glyph.color : "transparent"
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            PathSvg { path: "M17.5 15.5 L22 20 M22 15.5 L17.5 20" }
        }
    }

    Shape {
        width: 24
        height: 24
        scale: glyph.size / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        visible: glyph.kind !== "wifi" && glyph.kind !== "none"

        ShapePath {
            strokeWidth: glyph._stroke
            strokeColor: glyph.color
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg {
                path: glyph.kind === "ethernet"
                      // a wall socket: the plug's outline and its clip
                      ? "M4.5 6.5 Q4.5 4.5 6.5 4.5 L17.5 4.5 Q19.5 4.5 19.5 6.5 L19.5 15 Q19.5 17 17.5 17 L15.5 17 L15.5 19.5 L8.5 19.5 L8.5 17 L6.5 17 Q4.5 17 4.5 15 Z M8.5 8.5 L8.5 11 M12 8.5 L12 11 M15.5 8.5 L15.5 11"
                      : glyph.kind === "vpn"
                      // a shield with a keyhole
                      ? "M12 3 L19 5.75 L19 11 Q19 17.5 12 21 Q5 17.5 5 11 L5 5.75 Z M12 10 m-1.75 0 a1.75 1.75 0 1 0 3.5 0 a1.75 1.75 0 1 0 -3.5 0 M12 11.75 L12 15"
                      : glyph.kind === "hotspot"
                      // a tower giving out waves on both sides
                      ? "M12 12 L9.5 21 M12 12 L14.5 21 M10.4 17.5 L13.6 17.5 M12 10.5 m-1.5 0 a1.5 1.5 0 1 0 3 0 a1.5 1.5 0 1 0 -3 0 M8.2 6.8 Q6.5 10.5 8.2 14.2 M15.8 6.8 Q17.5 10.5 15.8 14.2 M5.3 4.3 Q2.3 10.5 5.3 16.7 M18.7 4.3 Q21.7 10.5 18.7 16.7"
                      // an airplane seen from above
                      : "M12 2.75 Q13.25 2.75 13.25 5 L13.25 9.5 L20.5 13.75 L20.5 15.5 L13.25 13.25 L13.25 18 L15.75 19.75 L15.75 21.25 L12 20.25 L8.25 21.25 L8.25 19.75 L10.75 18 L10.75 13.25 L3.5 15.5 L3.5 13.75 L10.75 9.5 L10.75 5 Q10.75 2.75 12 2.75 Z"
            }
        }
    }

    component WifiArc: ShapePath {
        property real radius
        property bool lit
        strokeWidth: glyph._stroke
        strokeColor: lit ? glyph.color : glyph.dimColor
        fillColor: "transparent"
        capStyle: ShapePath.RoundCap
        startX: 12 - radius * 0.7071
        startY: 20.5 - radius * 0.7071
        PathArc {
            x: 12 + radius * 0.7071
            y: 20.5 - radius * 0.7071
            radiusX: radius
            radiusY: radius
        }
    }
}
