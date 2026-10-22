"""Renders scanreceiver.svg (scanreceiver-small.svg up to 24 px) at every icon size with Edge (headless) and packs ../app.ico.

Usage: python build_icon.py
Each size is rasterized natively by the browser (not downscaled from 256 px), so small
sizes stay sharp. The .ico stores PNG entries, which Windows Vista and later read.
"""
import io
import os
import struct
import subprocess
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SVG = os.path.join(HERE, "scanreceiver.svg")
SMALL_SVG = os.path.join(HERE, "scanreceiver-small.svg")  # simplified artwork for tiny sizes
SMALL_MAX = 24
ICO = os.path.join(HERE, "..", "app.ico")
SIZES = [16, 20, 24, 32, 40, 48, 64, 96, 128, 256]
GAP = 8
EDGE_PATHS = [
    os.path.expandvars(r"%ProgramFiles(x86)%\Microsoft\Edge\Application\msedge.exe"),
    os.path.expandvars(r"%ProgramFiles%\Microsoft\Edge\Application\msedge.exe"),
]


def render_strip(tmp, svg_path, sizes):
    svg = open(svg_path, encoding="utf-8").read()
    x, imgs = 0, []
    for s in sizes:
        imgs.append(f'<div style="position:absolute;left:{x}px;top:0;width:{s}px;height:{s}px">'
                    f'{svg.replace("<svg ", f"<svg width=\"{s}\" height=\"{s}\" ", 1)}</div>')
        x += s + GAP
    # Every size inlines the same ids; that is fine because all copies are identical.
    html = ('<!doctype html><html><head><meta charset="utf-8"><style>html,body{margin:0;background:transparent}'
            '</style></head><body>' + "".join(imgs) + "</body></html>")
    name = os.path.splitext(os.path.basename(svg_path))[0]
    page = os.path.join(tmp, name + ".html")
    png = os.path.join(tmp, name + ".png")
    open(page, "w", encoding="utf-8").write(html)
    edge = next(p for p in EDGE_PATHS if os.path.exists(p))
    subprocess.run([edge, "--headless=new", "--disable-gpu", "--hide-scrollbars", "--force-device-scale-factor=1",
                    # Headless window size includes non-viewport space; leave room so 256 px isn't clipped.
                    "--default-background-color=00000000", f"--window-size={max(x, 200)},{max(sizes) + 200}",
                    f"--screenshot={png}", "file:///" + page.replace("\\", "/")],
                   check=True, capture_output=True)
    return Image.open(png).convert("RGBA")


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
    with tempfile.TemporaryDirectory() as tmp:
        frames = []
        for path, sizes in ((SMALL_SVG, [s for s in SIZES if s <= SMALL_MAX]),
                            (SVG, [s for s in SIZES if s > SMALL_MAX])):
            strip, x = render_strip(tmp, path, sizes), 0
            for s in sizes:
                frames.append(strip.crop((x, 0, x + s, s)))
                x += s + GAP
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
