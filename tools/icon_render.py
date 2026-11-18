"""Draws the Kai apps' icons: one flat symbol on a matte background.

The same file is in KaiPlayer, KaiRadio and KaiVideo. Each edge comes from the
exact distance to the shape, so it stays smooth at any size. The symbol throws
a long flat shadow towards the bottom right. The icon is saved as a JPEG
without chroma subsampling: subsampling is what turns saturated edges on a dark
background into saw teeth.

Needs numpy and Pillow.
"""
from __future__ import annotations

from typing import Sequence

import numpy as np
from PIL import Image

ICON_SIZE = 256
WORK_SIZE = 1536
JPEG_QUALITY = 97
UNIT = WORK_SIZE / ICON_SIZE
BACKGROUND = (44, 45, 51)
SHADOW = (31, 32, 37)

Rgb = tuple[int, int, int]
Point = tuple[float, float]

_centers = (np.arange(WORK_SIZE) + 0.5) / UNIT
X, Y = np.meshgrid(_centers, _centers)


def rgb(color: Rgb) -> np.ndarray:
    return np.array(color, dtype=np.float64) / 255.0


# --- Shapes: the distance to each, in icon pixels, negative inside ---


def circle(cx: float, cy: float, r: float) -> np.ndarray:
    return np.hypot(X - cx, Y - cy) - r


def ellipse(cx: float, cy: float, rx: float, ry: float, degrees: float = 0.0) -> np.ndarray:
    a = np.radians(degrees)
    dx, dy = X - cx, Y - cy
    px = dx * np.cos(a) + dy * np.sin(a)
    py = -dx * np.sin(a) + dy * np.cos(a)
    k0 = np.hypot(px / rx, py / ry)
    k1 = np.hypot(px / rx**2, py / ry**2)
    return k0 * (k0 - 1.0) / np.maximum(k1, 1e-9)


def round_rect(x0: float, y0: float, x1: float, y1: float, r: float) -> np.ndarray:
    qx = np.abs(X - (x0 + x1) / 2) - ((x1 - x0) / 2 - r)
    qy = np.abs(Y - (y0 + y1) / 2) - ((y1 - y0) / 2 - r)
    outside = np.hypot(np.maximum(qx, 0), np.maximum(qy, 0))
    return outside + np.minimum(np.maximum(qx, qy), 0) - r


def capsule(a: Point, b: Point, r: float) -> np.ndarray:
    ax, ay = a
    bax, bay = b[0] - ax, b[1] - ay
    pax, pay = X - ax, Y - ay
    h = np.clip((pax * bax + pay * bay) / (bax**2 + bay**2), 0, 1)
    return np.hypot(pax - bax * h, pay - bay * h) - r


def polygon(points: Sequence[Point]) -> np.ndarray:
    """Exact distance to a closed polygon."""
    best = np.full(X.shape, np.inf)
    inside = np.zeros(X.shape, dtype=bool)
    count = len(points)
    for i in range(count):
        ax, ay = points[i]
        bx, by = points[(i + count - 1) % count]
        ex, ey = bx - ax, by - ay
        wx, wy = X - ax, Y - ay
        t = np.clip((wx * ex + wy * ey) / (ex**2 + ey**2), 0, 1)
        best = np.minimum(best, (wx - ex * t) ** 2 + (wy - ey * t) ** 2)
        above_a, below_b, left = Y >= ay, Y < by, ex * wy > ey * wx
        inside ^= (above_a & below_b & left) | (~above_a & ~below_b & ~left)
    return np.where(inside, -1.0, 1.0) * np.sqrt(best)


def arc(cx: float, cy: float, radius: float, center_deg: float, half_deg: float, half_width: float) -> np.ndarray:
    """A curved band with round ends; 0 degrees points up, angles go clockwise."""
    a = np.radians(center_deg)
    dx, dy = X - cx, Y - cy
    px = np.abs(dx * np.cos(a) + dy * np.sin(a))
    py = dx * np.sin(a) - dy * np.cos(a)
    s, c = np.sin(np.radians(half_deg)), np.cos(np.radians(half_deg))
    to_end = np.hypot(px - s * radius, py - c * radius)
    to_ring = np.abs(np.hypot(px, py) - radius)
    return np.where(c * px > s * py, to_end, to_ring) - half_width


def union(*shapes: np.ndarray) -> np.ndarray:
    result = shapes[0]
    for shape in shapes[1:]:
        result = np.minimum(result, shape)
    return result


def smooth_union(a: np.ndarray, b: np.ndarray, k: float) -> np.ndarray:
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0, 1)
    return b * (1 - h) + a * h - k * h * (1 - h)


def subtract(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    return np.maximum(a, -b)


def coverage(d: np.ndarray) -> np.ndarray:
    return np.clip(0.5 - d * UNIT, 0, 1)


# --- Painting ---


class Canvas:
    def __init__(self, background: Rgb = BACKGROUND) -> None:
        self.background = rgb(background)
        self.pixels = np.ones((WORK_SIZE, WORK_SIZE, 3)) * self.background

    def fill(self, d: np.ndarray, color: Rgb) -> None:
        alpha = coverage(d)[..., None]
        self.pixels = self.pixels * (1 - alpha) + rgb(color) * alpha

    def cut(self, d: np.ndarray) -> None:
        """Opens a hole in the symbol, showing the background."""
        alpha = coverage(d)[..., None]
        self.pixels = self.pixels * (1 - alpha) + self.background * alpha

    def long_shadow(self, d: np.ndarray, color: Rgb = SHADOW) -> None:
        """The shape swept to the bottom right, off the edge of the icon."""
        mask = d <= 0
        step = 1
        while step < WORK_SIZE:
            moved = np.zeros_like(mask)
            moved[step:, step:] = mask[:-step, :-step]
            mask = mask | moved
            step *= 2
        alpha = np.asarray(
            Image.fromarray(mask.astype(np.uint8) * 255).resize((WORK_SIZE, WORK_SIZE)),
            dtype=np.float64,
        )[..., None] / 255
        self.pixels = self.pixels * (1 - alpha) + rgb(color) * alpha

    def result(self) -> Image.Image:
        data = (np.clip(self.pixels, 0, 1) * 255 + 0.5).astype(np.uint8)
        return Image.fromarray(data).resize((ICON_SIZE, ICON_SIZE), Image.LANCZOS)


def save_icon(image: Image.Image, path: str) -> None:
    image.save(path, "JPEG", quality=JPEG_QUALITY, subsampling=0, optimize=True, progressive=False)
