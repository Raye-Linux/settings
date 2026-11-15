#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The violet greys and the magenta of the 0.2 palette, copied by hand
# into a page or a glyph, outlive every change of ui/Theme.qml.
# Colours a user picks from (background, colour filters) may keep them.
set -u
SOURCE_DIR=${SOURCE_DIR:-$(cd "$(dirname "$0")/.." && pwd)}
cd "$SOURCE_DIR" || exit 1
old='130e1f|f3eaff|b9a8d6|f6f3fb|1a1028|574a6d|8a78ad|e4dcf2|a395bf|231a36|8c7eaa|c040ff'
hits=$(grep -rniE "#([0-9a-f]{2})?($old)\b" src/qml data/icons \
    | grep -v '^src/qml/ui/Theme.qml:[0-9]*: *readonly property color brandMagenta' \
    | grep -v '^src/qml/pages/personalization/BackgroundPage.qml:' \
    | grep -v '^src/qml/pages/accessibility/ColorFiltersPage.qml:')
if [ -n "$hits" ]; then
    echo "$hits" | sed 's/^/FAIL: old palette: /' | cut -c1-200
    exit 1
fi
# the picked colours stay where they are, and the check still sees them
grep -qi '#c040ff' src/qml/pages/accessibility/ColorFiltersPage.qml || { echo "FAIL: colour filter list moved"; exit 1; }
echo "palette: ok"
