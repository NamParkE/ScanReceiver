"""Renders scanreceiver.svg (scanreceiver-small.svg up to 24 px) at every icon size and packs ../app.ico.

Usage: python build_icon.py   (needs Pillow and PyMuPDF: pip install pillow pymupdf)
Each size is rasterized from the vector source (not downscaled from 256 px), so small
sizes stay sharp. The .ico stores PNG entries, which Windows Vista and later read.
Edge headless was used before, but stopped writing screenshots on this PC (2026-10-26).
"""
import io
import os
import struct

import pymupdf
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SVG = os.path.join(HERE, "scanreceiver.svg")
SMALL_SVG = os.path.join(HERE, "scanreceiver-small.svg")  # simplified artwork for tiny sizes
SMALL_MAX = 24
ICO = os.path.join(HERE, "..", "app.ico")
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
GAP = 8


def render(svg_path, size):
    page = pymupdf.open(svg_path)[0]
    zoom = size / page.rect.width
    pix = page.get_pixmap(matrix=pymupdf.Matrix(zoom, zoom), alpha=True)
    return Image.open(io.BytesIO(pix.tobytes("png"))).convert("RGBA")


def write_ico(frames, path):
    blobs = []
    for img in frames:
        buf = io.BytesIO()
        img.save(buf, format="PNG", optimize=True)
        blobs.append(buf.getvalue())
    header = struct.pack("<HHH", 0, 1, len(frames))
    offset = 6 + 16 * len(frames)
    entries = b""
    for img, blob in zip(frames, blobs):
        w = img.width if img.width < 256 else 0  # 0 means 256 in the ICO directory
        entries += struct.pack("<BBBBHHII", w, w, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    with open(path, "wb") as f:
        f.write(header + entries + b"".join(blobs))


def main():
    frames = [render(SMALL_SVG if s <= SMALL_MAX else SVG, s) for s in SIZES]
    write_ico(frames, ICO)
    preview = Image.new("RGBA", (sum(SIZES) + GAP * len(SIZES), max(SIZES)), (0, 0, 0, 0))
    x = 0
    for f in frames:
        preview.paste(f, (x, max(SIZES) - f.height))
        x += f.width + GAP
    preview.save(os.path.join(HERE, "preview.png"))
    print(f"wrote {os.path.normpath(ICO)} ({', '.join(map(str, SIZES))} px)")


if __name__ == "__main__":
    main()
