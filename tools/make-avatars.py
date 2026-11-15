#!/usr/bin/env python3
# The account pictures to pick from in Accounts > Your info. Drawn here,
# committed under data/icons-avatars. A shape in soft white on a Raye
# gradient; run this again to change them.
#
#   tools/make-avatars.py [output folder]
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

SIZE = 256
SCALE = 4
S = SIZE * SCALE
INK = (243, 234, 255, 255)


def gradient(top, bottom):
    image = Image.new("RGB", (S, S))
    pixels = image.load()
    for y in range(S):
        for x in range(S):
            t = (x + y) / (2 * S - 2)
            pixels[x, y] = tuple(round(a + (b - a) * t) for a, b in zip(top, bottom))
    return image


def glow(image, centre, radius, colour):
    layer = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    x, y = centre
    draw.ellipse((x - radius, y - radius, x + radius, y + radius), fill=colour)
    layer = layer.filter(ImageFilter.GaussianBlur(radius * 0.6))
    image.paste(layer, (0, 0), layer)


def eye(d):
    c = S / 2
    w, h = S * 0.32, S * 0.17
    points = []
    for i in range(121):
        a = i / 120 * 2 * math.pi
        points.append((c + w * math.cos(a), c - h * math.sin(a) * abs(math.sin(a)) ** 0.25))
    d.polygon(points, fill=INK)
    r = S * 0.1
    d.ellipse((c - r, c - r, c + r, c + r), fill=(90, 40, 200, 255))
    r = S * 0.04
    d.ellipse((c - r, c - r, c + r, c + r), fill=INK)


def moon(d):
    c, r = S / 2, S * 0.27
    d.ellipse((c - r, c - r, c + r, c + r), fill=INK)
    x, y = c + r * 0.45, c - r * 0.3
    k = r * 0.86
    d.ellipse((x - k, y - k, x + k, y + k), fill=(0, 0, 0, 0))


def star(d):
    c, outer, inner = S / 2, S * 0.3, S * 0.09
    points = []
    for i in range(8):
        r = outer if i % 2 == 0 else inner
        a = i * math.pi / 4 - math.pi / 2
        points.append((c + r * math.cos(a), c + r * math.sin(a)))
    d.polygon(points, fill=INK)


def mountain(d):
    b = S * 0.72
    d.polygon([(S * 0.14, b), (S * 0.42, S * 0.3), (S * 0.62, b)], fill=INK)
    d.polygon([(S * 0.46, b), (S * 0.66, S * 0.44), (S * 0.86, b)], fill=(243, 234, 255, 170))


def wave(d):
    for row, alpha in ((0.38, 255), (0.5, 200), (0.62, 140)):
        points = []
        for i in range(101):
            x = S * 0.16 + S * 0.68 * i / 100
            points.append((x, S * row + S * 0.05 * math.sin(i / 100 * 2 * math.pi * 1.5)))
        d.line(points, fill=(243, 234, 255, alpha), width=int(S * 0.045), joint="curve")


def planet(d):
    c, r = S / 2, S * 0.17
    d.ellipse((c - r, c - r, c + r, c + r), fill=INK)
    points = []
    for i in range(121):
        a = i / 120 * 2 * math.pi
        x, y = S * 0.34 * math.cos(a), S * 0.09 * math.sin(a)
        t = -0.4
        points.append((c + x * math.cos(t) - y * math.sin(t), c + x * math.sin(t) + y * math.cos(t)))
    d.line(points, fill=INK, width=int(S * 0.022), joint="curve")


def leaf(d):
    points = []
    for i in range(61):
        t = i / 60
        points.append((S * (0.26 + 0.48 * t), S * (0.74 - 0.48 * t) - S * 0.16 * math.sin(t * math.pi)))
    for i in range(61):
        t = 1 - i / 60
        points.append((S * (0.26 + 0.48 * t), S * (0.74 - 0.48 * t) + S * 0.16 * math.sin(t * math.pi)))
    d.polygon(points, fill=INK)


def bolt(d):
    d.polygon([(S * 0.56, S * 0.18), (S * 0.3, S * 0.56), (S * 0.47, S * 0.56),
               (S * 0.42, S * 0.82), (S * 0.7, S * 0.42), (S * 0.52, S * 0.42)], fill=INK)


PICTURES = [
    ("eye", (58, 22, 130), (18, 10, 40), eye),
    ("moon", (40, 28, 110), (120, 50, 200), moon),
    ("star", (123, 60, 255), (192, 64, 255), star),
    ("mountain", (70, 40, 160), (20, 90, 130), mountain),
    ("wave", (24, 70, 160), (90, 60, 220), wave),
    ("planet", (110, 30, 130), (40, 20, 100), planet),
    ("leaf", (24, 110, 100), (60, 50, 150), leaf),
    ("bolt", (170, 50, 170), (90, 30, 190), bolt),
]


def main(argv):
    out = argv[1] if len(argv) > 1 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "data", "icons-avatars")
    os.makedirs(out, exist_ok=True)
    for name, top, bottom, shape in PICTURES:
        image = gradient(top, bottom).convert("RGBA")
        glow(image, (S * 0.25, S * 0.2), S * 0.28, (192, 64, 255, 90))
        mark = Image.new("RGBA", (S, S), (0, 0, 0, 0))
        shape(ImageDraw.Draw(mark))
        image.paste(mark, (0, 0), mark)
        image.resize((SIZE, SIZE), Image.LANCZOS).convert("RGB").save(os.path.join(out, name + ".png"), optimize=True)


if __name__ == "__main__":
    main(sys.argv)
