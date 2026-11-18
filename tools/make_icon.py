"""Draws KaiVideo's icon (icon.jpg, 256x256): a flat screen with a play button cut out.

Usage: python tools/make_icon.py [OUT]
"""
import sys

from icon_render import Canvas, capsule, polygon, round_rect, save_icon, union

PINK = (255, 107, 154)
CORNER = 7.0


def draw() -> Canvas:
    canvas = Canvas()
    screen = round_rect(36, 54, 220, 174, 20)
    stand = union(round_rect(116, 170, 140, 200, 2), capsule((92, 201), (164, 201), 6))
    symbol = union(screen, stand)
    canvas.long_shadow(symbol)
    canvas.fill(symbol, PINK)
    play = polygon([(113, 93 + 2), (113, 135 - 2), (149, 114)]) - CORNER
    canvas.cut(play)
    return canvas


def main() -> None:
    save_icon(draw().result(), sys.argv[1] if len(sys.argv) > 1 else "icon.jpg")


if __name__ == "__main__":
    main()
