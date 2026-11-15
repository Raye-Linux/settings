// SPDX-License-Identifier: GPL-3.0-or-later
// The small line glyphs of Settings (chevrons, check, close, open
// elsewhere, eye...), drawn here so they follow the text colour and
// stay sharp at any size. Each is designed on a 16 px grid.
import QtQuick
import QtQuick.Shapes

Item {
    id: glyph

    // chevron-right, chevron-left, chevron-down, chevron-up, arrow-left,
    // open-external, check, close, minus, exclamation, info, dot, more,
    // copy, eye, eye-off, search
    property string name: "chevron-right"
    property color color: Theme.text
    property real size: 16
    // stroke width on the 16 px grid
    property real weight: 1.5

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    readonly property var _paths: ({
        "chevron-right": [{ d: "M6 3 L11 8 L6 13" }],
        "chevron-left": [{ d: "M10 3 L5 8 L10 13" }],
        "chevron-down": [{ d: "M3 6 L8 11 L13 6" }],
        "chevron-up": [{ d: "M3 10 L8 5 L13 10" }],
        "arrow-left": [{ d: "M13.5 8 L2.5 8" }, { d: "M7 3.5 L2.5 8 L7 12.5" }],
        "open-external": [{ d: "M7 2.75 L4.25 2.75 Q2.75 2.75 2.75 4.25 L2.75 11.75 Q2.75 13.25 4.25 13.25 L11.75 13.25 Q13.25 13.25 13.25 11.75 L13.25 9" },
                          { d: "M9.5 2.75 L13.25 2.75 L13.25 6.5" }, { d: "M13 3 L7.5 8.5" }],
        "check": [{ d: "M3.25 8.5 L6.5 11.75 L12.75 4.75" }],
        "close": [{ d: "M4 4 L12 12" }, { d: "M12 4 L4 12" }],
        "minus": [{ d: "M3.5 8 L12.5 8" }],
        "exclamation": [{ d: "M8 3.5 L8 9" }, { d: "M8 12 m-1.1 0 a1.1 1.1 0 1 0 2.2 0 a1.1 1.1 0 1 0 -2.2 0", fill: true }],
        "info": [{ d: "M8 7.25 L8 12.5" }, { d: "M8 4.25 m-1.1 0 a1.1 1.1 0 1 0 2.2 0 a1.1 1.1 0 1 0 -2.2 0", fill: true }],
        "dot": [{ d: "M8 8 m-3 0 a3 3 0 1 0 6 0 a3 3 0 1 0 -6 0", fill: true }],
        "more": [{ d: "M3 8 m-1.25 0 a1.25 1.25 0 1 0 2.5 0 a1.25 1.25 0 1 0 -2.5 0", fill: true },
                 { d: "M8 8 m-1.25 0 a1.25 1.25 0 1 0 2.5 0 a1.25 1.25 0 1 0 -2.5 0", fill: true },
                 { d: "M13 8 m-1.25 0 a1.25 1.25 0 1 0 2.5 0 a1.25 1.25 0 1 0 -2.5 0", fill: true }],
        "copy": [{ d: "M5.75 2.75 L11.75 2.75 Q13.25 2.75 13.25 4.25 L13.25 10.25" },
                 { d: "M3.75 5.25 L9.75 5.25 Q10.75 5.25 10.75 6.25 L10.75 12.25 Q10.75 13.25 9.75 13.25 L3.75 13.25 Q2.75 13.25 2.75 12.25 L2.75 6.25 Q2.75 5.25 3.75 5.25 Z" }],
        "eye": [{ d: "M1.75 8 Q8 0.5 14.25 8 Q8 15.5 1.75 8 Z" },
                { d: "M8 8 m-2.25 0 a2.25 2.25 0 1 0 4.5 0 a2.25 2.25 0 1 0 -4.5 0" }],
        "eye-off": [{ d: "M1.75 8 Q8 0.5 14.25 8 Q8 15.5 1.75 8 Z" },
                    { d: "M8 8 m-2.25 0 a2.25 2.25 0 1 0 4.5 0 a2.25 2.25 0 1 0 -4.5 0" },
                    { d: "M2.5 2.5 L13.5 13.5" }],
        "search": [{ d: "M6.75 6.75 m-4.25 0 a4.25 4.25 0 1 0 8.5 0 a4.25 4.25 0 1 0 -8.5 0" }, { d: "M9.9 9.9 L13.75 13.75" }]
    })

    readonly property var _parts: _paths[name] || []

    Shape {
        id: shape
        width: 16
        height: 16
        scale: glyph.size / 16
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer

        // no glyph has more than three strokes
        GlyphPath { part: glyph._parts[0] }
        GlyphPath { part: glyph._parts[1] }
        GlyphPath { part: glyph._parts[2] }
    }

    component GlyphPath: ShapePath {
        property var part
        strokeColor: part && !part.fill ? glyph.color : "transparent"
        strokeWidth: part && !part.fill ? glyph.weight : 0
        fillColor: part && part.fill ? glyph.color : "transparent"
        capStyle: ShapePath.RoundCap
        joinStyle: ShapePath.RoundJoin
        PathSvg { path: part ? part.d : "M0 0" }
    }
}
