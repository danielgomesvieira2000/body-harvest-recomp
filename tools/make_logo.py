#!/usr/bin/env python3
"""Draw the launcher emblem and the executable icon from one set of polygons.

    python tools/make_logo.py            # writes assets/icons/Logo.svg and assets/AppIcon.ico

The artwork is original to this project and derived from nothing in the
cartridge: a faceted, flat-shaded alien saucer in the low-polygon style of the
period, casting a tractor beam down onto the curve of a planet. It carries no
lettering, so the launcher keeps its own plain-text title (src/frontend.cpp).

Both outputs come from the same polygon list, so the icon cannot drift from the
launcher art. The SVG uses only <polygon> elements, because the menu's SVG
renderer (lunasvg) draws no <text>. The .ico is rasterised with Pillow at 1024 px
and downscaled per size with Lanczos (every size Windows asks for; see
n64recomp-claude_framework/tools/make-icon.py). Structure from Hybrid Heaven's
tools/make_logo.py. Needs Pillow only for the .ico.
"""

import io
import math
import struct
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
VIEW_W, VIEW_H = 680, 360
CX, CY = VIEW_W / 2, VIEW_H / 2

ICON_SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]


def shade(rgb, k):
    return tuple(max(0, min(255, int(c * k))) for c in rgb)


def planet(CX, CY):
    """The top of a planet: a fan of flat-shaded facets under the emblem."""
    polys = []
    r = 210
    centre = (CX, CY + 110 + r)
    land = (96, 150, 64)
    sea = (52, 96, 150)
    steps = 12
    # A cap of the circle cut by the chord between its end angles: each facet runs
    # straight down from the arc to the chord.
    chord_y = centre[1] + r * math.sin(math.radians(-150))
    for i in range(steps):
        a0 = math.radians(-150 + 120 * i / steps)
        a1 = math.radians(-150 + 120 * (i + 1) / steps)
        p0 = (centre[0] + r * math.cos(a0), centre[1] + r * math.sin(a0))
        p1 = (centre[0] + r * math.cos(a1), centre[1] + r * math.sin(a1))
        q0 = (p0[0], chord_y)
        q1 = (p1[0], chord_y)
        base = land if i % 3 != 1 else sea
        light = 0.75 + 0.25 * math.cos((a0 + a1) / 2 - math.radians(-120))
        polys.append(([p0, p1, q1, q0], shade(base, light)))
    return polys


def beam(CX, CY):
    """A translucent-looking cone from the saucer's underside to the planet."""
    top_y = CY + 12
    bottom_y = CY + 112
    return [
        ([(CX - 22, top_y), (CX + 22, top_y), (CX + 70, bottom_y), (CX - 70, bottom_y)], (150, 236, 170)),
        ([(CX - 10, top_y), (CX + 10, top_y), (CX + 34, bottom_y), (CX - 34, bottom_y)], (210, 255, 220)),
    ]


def saucer(CX, CY):
    """A disc seen slightly from above: rim facets, a lower hull, a domed cockpit."""
    polys = []
    rx, ry = 150, 34
    hull = (150, 156, 170)
    ring = []
    n = 16
    for i in range(n):
        a = math.radians(360 * i / n)
        ring.append((CX + rx * math.cos(a), CY + ry * math.sin(a)))
    # Underside: facets from the rim to a point below the centre.
    below = (CX, CY + 30)
    for i in range(n):
        p, q = ring[i], ring[(i + 1) % n]
        mid = math.radians(360 * (i + 0.5) / n)
        if math.sin(mid) > -0.1:
            light = 0.45 + 0.25 * math.cos(mid - math.radians(200))
            polys.append(([below, p, q], shade(hull, light)))
    # Upper surface: facets from the rim to a raised centre.
    above = (CX, CY - 18)
    for i in range(n):
        p, q = ring[i], ring[(i + 1) % n]
        mid = math.radians(360 * (i + 0.5) / n)
        light = 0.72 + 0.35 * math.cos(mid - math.radians(215))
        polys.append(([above, p, q], shade(hull, light)))
    # Running lights on the near rim.
    for i in range(n):
        mid = math.radians(360 * (i + 0.5) / n)
        if math.sin(mid) > 0.35:
            x = CX + (rx - 12) * math.cos(mid)
            y = CY + (ry - 5) * math.sin(mid)
            colour = (255, 196, 64) if i % 2 == 0 else (240, 72, 60)
            polys.append(([(x - 6, y), (x, y - 4), (x + 6, y), (x, y + 4)], colour))
    # Dome.
    dome = (120, 210, 240)
    d_rx, d_ry = 52, 44
    dome_base_y = CY - 14
    steps = 8
    for i in range(steps):
        a0 = math.radians(180 + 180 * i / steps)
        a1 = math.radians(180 + 180 * (i + 1) / steps)
        p0 = (CX + d_rx * math.cos(a0), dome_base_y + d_ry * math.sin(a0))
        p1 = (CX + d_rx * math.cos(a1), dome_base_y + d_ry * math.sin(a1))
        light = 0.7 + 0.35 * math.cos((a0 + a1) / 2 - math.radians(235))
        polys.append(([(CX, dome_base_y + 4), p0, p1], shade(dome, light)))
    return polys


def emblem(cx=CX, cy=CY, scale=1.0):
    """Planet, beam, saucer: painter's order. Drawn at the view centre and full
    size, then placed at (cx, cy) with `scale`."""
    out = []
    for poly, colour in planet(CX, CY) + beam(CX, CY) + saucer(CX, CY):
        out.append(([(cx + (x - CX) * scale, cy + (y - CY) * scale) for x, y in poly], colour))
    return out


# The launcher lays this SVG out at the window's full width, vertically centred,
# behind its title (about 28% down) and its menu (a centred column). Two small
# emblems flank the column so neither overlaps text at 4:3 or 16:9.
LAUNCHER_SCALE = 0.5
LAUNCHER_X = 105


def launcher_scene():
    return emblem(LAUNCHER_X, CY, LAUNCHER_SCALE) + emblem(VIEW_W - LAUNCHER_X, CY, LAUNCHER_SCALE)


def write_svg(path):
    parts = [f'<svg width="100%" viewBox="0 0 {VIEW_W} {VIEW_H}" role="img" '
             'xmlns="http://www.w3.org/2000/svg">',
             "<title>Body Harvest: Recompiled</title>",
             "<desc>Two faceted low-polygon flying saucers, each casting a green tractor beam onto the curve "
             "of a planet. Original artwork for this project.</desc>"]
    for poly, (r, g, b) in launcher_scene():
        pts = " ".join(f"{x:.1f},{y:.1f}" for x, y in poly)
        parts.append(f'<polygon points="{pts}" fill="#{r:02x}{g:02x}{b:02x}"/>')
    parts.append("</svg>")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(parts) + "\n", encoding="utf-8", newline="\n")


def write_ico(path):
    from PIL import Image, ImageDraw

    big = 1024
    # Square crop around the emblem.
    side = 340.0
    ox, oy = CX - side / 2, CY - side / 2 + 30
    scale = big / side
    img = Image.new("RGBA", (big, big), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    for poly, colour in emblem():
        draw.polygon([((x - ox) * scale, (y - oy) * scale) for x, y in poly], fill=colour + (255,))

    entries = []
    for size in ICON_SIZES:
        buf = io.BytesIO()
        img.resize((size, size), Image.LANCZOS).save(buf, format="PNG")
        entries.append((size, buf.getvalue()))

    header = struct.pack("<HHH", 0, 1, len(entries))
    offset = 6 + 16 * len(entries)
    directory = b""
    data = b""
    for size, png in entries:
        dim = 0 if size >= 256 else size
        directory += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(png), offset + len(data))
        data += png
    path.write_bytes(header + directory + data)


def main():
    svg = REPO / "assets" / "icons" / "Logo.svg"
    ico = REPO / "assets" / "AppIcon.ico"
    write_svg(svg)
    write_ico(ico)
    print(f"wrote {svg.relative_to(REPO)} and {ico.relative_to(REPO)}")


if __name__ == "__main__":
    main()
