"""Draws KaiVideo's icon (icon.jpg, 256x256): a monitor with a play button on
the dark background KaiPlayer's and KaiRadio's icons use, in KaiVideo's pink,
with KaiPlayer's progress bar under it.

Usage: python tools/make_icon.py [OUT]
"""
import math
import sys

from PIL import Image, ImageDraw, ImageFilter

SIZE = 256
SCALE = 4  # Drawn larger, then shrunk, for smooth edges.
PINK = (255, 107, 154)
BLUE = (110, 190, 255)
TRACK = (52, 54, 68)
BG_CENTER = (40, 42, 58)
BG_EDGE = (16, 17, 24)


def background(size):
    img = Image.new("RGB", (size, size))
    px = img.load()
    c = size / 2
    for y in range(size):
        for x in range(size):
            t = min(1.0, math.hypot(x - c, y - c * 0.9) / (size * 0.72))
            px[x, y] = tuple(int(a + (b - a) * t) for a, b in zip(BG_CENTER, BG_EDGE))
    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "icon.jpg"
    s = SIZE * SCALE
    img = background(s).convert("RGBA")
    layer = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(layer)
    u = s / 256  # One icon pixel.

    # The screen, 16:9: a frame on a stand.
    screen = (38 * u, 40 * u, 218 * u, 142 * u)
    d.rounded_rectangle(screen, radius=16 * u, outline=PINK, width=int(11 * u))
    d.rounded_rectangle((114 * u, 142 * u, 142 * u, 156 * u), radius=3 * u, fill=PINK)
    d.rounded_rectangle((96 * u, 154 * u, 160 * u, 164 * u), radius=5 * u, fill=PINK)

    # The play button inside it, with rounded corners.
    cx, cy = 132 * u, 91 * u
    tri = [(cx - 20 * u, cy - 26 * u), (cx - 20 * u, cy + 26 * u), (cx + 26 * u, cy)]
    d.polygon(tri, fill=PINK)
    d.line(tri + [tri[0]], fill=PINK, width=int(8 * u), joint="curve")

    # The progress bar under it, as in KaiPlayer's icon.
    y = 198 * u
    d.rounded_rectangle((40 * u, y - 5 * u, 216 * u, y + 5 * u), radius=5 * u, fill=TRACK)
    d.rounded_rectangle((40 * u, y - 5 * u, 132 * u, y + 5 * u), radius=5 * u, fill=BLUE)
    d.ellipse((132 * u - 11 * u, y - 11 * u, 132 * u + 11 * u, y + 11 * u), fill=BLUE)

    # A soft glow under the shapes.
    glow = layer.filter(ImageFilter.GaussianBlur(10 * u))
    glow.putalpha(glow.getchannel("A").point(lambda a: a * 0.35))
    img = Image.alpha_composite(img, glow)
    img = Image.alpha_composite(img, layer)
    img = img.convert("RGB").resize((SIZE, SIZE), Image.LANCZOS)
    img.save(out, quality=95)


if __name__ == "__main__":
    main()
