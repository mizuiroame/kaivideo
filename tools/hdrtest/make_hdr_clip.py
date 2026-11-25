"""Made-up HDR test clips for KaiVideo (no real content): a test pattern in
BT.2020 with PQ (HDR10) or HLG, written as raw 10-bit Y'CbCr for ffmpeg to
encode as tagged HEVC Main 10.

    python make_hdr_clip.py pq 1920 1080 6 > clip.yuv
    ffmpeg -f rawvideo -pix_fmt yuv420p10le -s 1920x1080 -r 24 -i clip.yuv ...

Top to bottom: a gray ramp from 0 to 1000 nits (marks every 100), a gray ramp
from 0 to 4000 nits, color bars at 100 nits and at 600 nits (BT.709's colors,
then BT.2020's own wider ones), a skin tone and a 203-nit white patch beside
dark grays, and under them a sky gradient with a 1000-nit sun moving across.
"""
import sys

import numpy as np

M1, M2, C1, C2, C3 = 0.1593017578125, 78.84375, 0.8359375, 18.8515625, 18.6875
HLG_A, HLG_B, HLG_C = 0.17883277, 0.28466892, 0.55991073
KR, KG, KB = 0.2627, 0.6780, 0.0593
# BT.709's colors in BT.2020's terms (linear).
TO_2020 = np.array([[0.6274, 0.3293, 0.0433], [0.0691, 0.9195, 0.0114], [0.0164, 0.0880, 0.8956]])


def pq(nits):
    y = np.clip(nits / 10000.0, 0, 1) ** M1
    return ((C1 + C2 * y) / (1 + C3 * y)) ** M2


def hlg(nits):
    """Display light (nominal 1000-nit HLG) to signal, through the inverse OOTF on gray."""
    scene = np.clip(nits / 1000.0, 0, 1) ** (1 / 1.2)
    return np.where(scene <= 1 / 12, np.sqrt(3 * scene), HLG_A * np.log(np.maximum(12 * scene - HLG_B, 1e-6)) + HLG_C)


def pattern(w, h):
    img = np.zeros((h, w, 3))  # Linear BT.2020 light, nits.
    band = h // 8
    x = np.linspace(0, 1, w)
    img[0:band] = (x * 1000)[None, :, None]
    for mark in range(1, 10):
        img[band - band // 6:band, int(w * mark / 10)] = 0
    img[band:2 * band] = (x * 4000)[None, :, None]
    colors709 = [(1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 1, 1), (1, 0, 1), (1, 1, 0)]
    colors2020 = [(1, 0, 0), (0, 1, 0), (0, 0, 1)]
    for row, nits in ((2, 100), (3, 600)):
        bars = [TO_2020 @ np.array(c) for c in colors709] + [np.array(c, float) for c in colors2020]
        bw = w // len(bars)
        for i, c in enumerate(bars):
            img[row * band:(row + 1) * band, i * bw:(i + 1) * bw] = c * nits
    patches = [(0.5, 0.5, 0.5), (2, 2, 2), (10, 10, 10), (50, 50, 50), None, (203, 203, 203)]
    pw = w // len(patches)
    skin = TO_2020 @ np.array([0.62, 0.38, 0.26]) * 120
    for i, c in enumerate(patches):
        img[4 * band:5 * band, i * pw:(i + 1) * pw] = skin if c is None else c
    sky = np.linspace(250, 20, h - 5 * band)[:, None, None] * np.array([0.55, 0.75, 1.0])
    img[5 * band:] = sky
    return img


def to_yuv(img, transfer):
    e = pq(img) if transfer == "pq" else hlg(img)
    y = KR * e[..., 0] + KG * e[..., 1] + KB * e[..., 2]
    cb = (e[..., 2] - y) / 1.8814
    cr = (e[..., 0] - y) / 1.4746
    Y = np.round(64 + 876 * y).astype("<u2")
    sub = lambda c: (c[0::2, 0::2] + c[1::2, 0::2] + c[0::2, 1::2] + c[1::2, 1::2]) / 4
    U = np.round(512 + 896 * sub(cb)).astype("<u2")
    V = np.round(512 + 896 * sub(cr)).astype("<u2")
    return Y.tobytes() + U.tobytes() + V.tobytes()


def main():
    transfer, w, h, seconds = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4])
    base = pattern(w, h)
    band = h // 8
    yy, xx = np.mgrid[5 * band:h, 0:w]
    radius = band * 0.6
    frames = int(seconds * 24)
    out = sys.stdout.buffer
    for f in range(frames):
        img = base.copy()
        cx, cy = w * (0.1 + 0.8 * f / max(frames - 1, 1)), 5 * band + (h - 5 * band) / 2
        d = np.hypot(xx - cx, yy - cy)
        glow = np.clip(1.4 - d / radius, 0, 1)[..., None]
        sun = np.array([1.0, 0.95, 0.8]) * 1000
        img[5 * band:] = img[5 * band:] * (1 - glow.clip(0, 1)) + sun * glow.clip(0, 1)
        out.write(to_yuv(img, transfer))


if __name__ == "__main__":
    main()
