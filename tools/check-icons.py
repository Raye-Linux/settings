#!/usr/bin/env python3
# Checks the glyphs of Raye Settings (data/icons).
#
#   tools/check-icons.py [settings repo]
#
# - every icon named in data/layout.json exists as
#   data/icons/hicolor/scalable/apps/<name>.svg;
# - the glyphs other parts of Settings ask for by name exist too (status
#   marks, the PC pictures of About and Home, the tile of a page that is
#   a tile of its own, Home, the app icon);
# - every SVG is a plain drawing: square, with a viewBox, no picture
#   embedded, no link to another file, no script, no text (a font
#   would have to be on the computer to draw it).
#
# Exit status 0 when all is well, 1 otherwise; each problem on a line.

import json
import os
import re
import sys
import xml.etree.ElementTree as ET

APPS = os.path.join("data", "icons", "hicolor", "scalable", "apps")
PREFIX = "org.raye.settings"
NAME = re.compile(r"^org\.raye\.settings(-[a-z0-9]+(-[a-z0-9]+)*)?$")
SVG_NS = "{http://www.w3.org/2000/svg}"

# Asked for by name in the code rather than in layout.json.
REQUIRED = [
    PREFIX,
    PREFIX + "-home",
    PREFIX + "-status-ok",
    PREFIX + "-status-warning",
    PREFIX + "-status-error",
    PREFIX + "-status-info",
    PREFIX + "-status-updating",
    PREFIX + "-status-restart",
    PREFIX + "-chassis-laptop",
    PREFIX + "-chassis-desktop",
    PREFIX + "-chassis-tablet",
    PREFIX + "-chassis-all-in-one",
]

FORBIDDEN_TAGS = {"image", "script", "foreignObject", "text", "tspan", "textPath", "font", "font-face", "use"}


def layout_icons(layout):
    names = []
    pages = {}
    for category in layout.get("categories", []):
        if category.get("icon"):
            names.append(("category " + category.get("id", "?"), category["icon"]))
        for page in category.get("pages", []):
            pages[page.get("id")] = page
            if page.get("icon"):
                names.append(("page " + page.get("id", "?"), page["icon"]))
    for tile in layout.get("tiles", []):
        target = tile.get("target", "")
        if tile.get("icon"):
            names.append(("tile " + target, tile["icon"]))
        elif target in pages and pages[target].get("icon"):
            # a page shown as a tile of the launch screen: a tile glyph
            names.append(("tile " + target, pages[target]["icon"] + "-tile"))
    return names


def check_svg(path):
    problems = []
    try:
        with open(path, "rb") as fh:
            data = fh.read()
        root = ET.fromstring(data)
    except (OSError, ET.ParseError) as error:
        return ["cannot read: %s" % error]
    if root.tag != SVG_NS + "svg":
        problems.append("not an SVG drawing")
    box = (root.get("viewBox") or "").split()
    if len(box) != 4:
        problems.append("no viewBox")
    else:
        try:
            w, h = float(box[2]), float(box[3])
            if w <= 0 or w != h:
                problems.append("viewBox is not square (%s)" % " ".join(box))
        except ValueError:
            problems.append("viewBox is not numbers")
    for element in root.iter():
        tag = element.tag.split("}")[-1]
        if tag in FORBIDDEN_TAGS:
            problems.append("<%s> is not allowed" % tag)
        for key, value in element.attrib.items():
            local = key.split("}")[-1]
            if local == "href" and not value.startswith("#"):
                problems.append("links to %s" % value)
            if local.startswith("on"):
                problems.append("event attribute %s" % local)
            for ref in re.findall(r"url\(([^)]*)\)", value):
                if not ref.strip().strip("'\"").startswith("#"):
                    problems.append("url(%s) points outside the file" % ref)
            if local in ("font-family", "font"):
                problems.append("names a font")
        if tag == "style" and element.text:
            text = element.text
            if "@import" in text or "font-family" in text:
                problems.append("style imports or names a font")
            for ref in re.findall(r"url\(([^)]*)\)", text):
                if not ref.strip().strip("'\"").startswith("#"):
                    problems.append("url(%s) points outside the file" % ref)
    return problems


def main(argv):
    repo = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
    apps = os.path.join(repo, APPS)
    problems = []

    try:
        with open(os.path.join(repo, "data", "layout.json"), encoding="utf-8") as fh:
            layout = json.load(fh)
    except (OSError, ValueError) as error:
        print("data/layout.json: %s" % error)
        return 1

    present = set()
    if os.path.isdir(apps):
        for entry in sorted(os.listdir(apps)):
            path = os.path.join(apps, entry)
            if not entry.endswith(".svg"):
                problems.append("%s: only .svg files belong here" % entry)
                continue
            name = entry[:-4]
            present.add(name)
            if os.path.islink(path) or not os.path.isfile(path):
                problems.append("%s: not a plain file" % entry)
                continue
            if not NAME.match(name):
                problems.append("%s: name must be %s-<word>" % (entry, PREFIX))
            for problem in check_svg(path):
                problems.append("%s: %s" % (entry, problem))
    else:
        problems.append("%s is missing" % APPS)

    wanted = layout_icons(layout) + [("code", name) for name in REQUIRED]
    for owner, name in wanted:
        if not name.startswith(PREFIX):
            continue  # an icon of the system theme, not ours to draw
        if name not in present:
            problems.append("%s: icon %s is missing" % (owner, name))

    for problem in problems:
        print(problem)
    used = {name for _, name in wanted}
    unused = sorted(present - used)
    print("%d icons, %d named by the layout or the code%s" % (
        len(present), len(present & used),
        (", not used yet: " + ", ".join(unused)) if unused else ""))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
