#!/usr/bin/env python3
"""Convert Chicago95 16x16 pixel-art icons into a C ARGB header for LiwusOS.

Usage:
    python scripts/convert_chicago95_icons.py <path-to-chicago95>/Icons/Chicago95

The generated header lives in include/gui/pixel_icons.h and is usable by the
LGX compositor (image_node). Icons are stored as 0xAARRGGBB uint32 rows,
16x16. Source: https://github.com/grassmunk/Chicago95 (not committed).
"""

import os
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_HEADER = os.path.join(ROOT, "src", "kernel", "gui", "assets", "pixel_icons.h")
OUT_PREVIEW = os.path.join(ROOT, "scripts", "out", "preview_icons.png")

# icon name -> relative path inside the Chicago95 icons theme
ICONS = {
    "demo":      "apps/16/gnome-system-monitor.png",
    "terminal":  "apps/16/utilities-terminal.png",
    "lde":       "devices/16/computer.png",
    "editor":    "apps/16/accessories-text-editor.png",
    "settings":  "apps/16/preferences-system.png",
    "multimedia": "apps/16/multimedia-audio-player.png",
    "imageviewer": "apps/16/image-viewer.png",
    "explorer":  "apps/16/system-file-manager.png",
    "browser":   "apps/16/web-browser.png",
    "start":     "places/16/start-here.png",
}
SIZE = 16  # all Chicago95 hicolor icons we use are 16x16

_FILTERS = (0, 1, 2, 3, 4)


def resolve(path):
    """Follow symlinks-on-Windows (checked out as text pointers)."""
    seen = set()
    while True:
        raw = open(path, "rb").read()
        if len(raw) >= 8 and raw[:2] == b"\x89P":
            return path
        if path in seen or len(raw) > 4096:
            raise ValueError("unresolvable pointer: %s" % path)
        seen.add(path)
        target = raw.decode("utf-8", "replace").strip()
        path = os.path.normpath(os.path.join(os.path.dirname(path), target))


def decode_png(path):
    """Decode a PNG into a list of (r, g, b, a) tuples, row-major."""
    raw = open(path, "rb").read()
    pos = 8  # skip PNG signature
    width = height = depth = ctype = interlace = None
    pal = pal_alpha = None
    idat = b""

    while pos < len(raw):
        (length,) = struct.unpack(">I", raw[pos:pos + 4])
        tag = raw[pos + 4:pos + 8]
        data = raw[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            width, height, depth, ctype, _, _, interlace = struct.unpack(
                ">IIBBBBB", data)
        elif tag == b"PLTE":
            pal = list(zip(data[0::3], data[1::3], data[2::3]))
        elif tag == b"tRNS":
            if ctype == 3:
                pal_alpha = list(data)
            else:
                pal_alpha = struct.unpack(">%dH" % (len(data) // 2), data)
        elif tag == b"IDAT":
            idat += data
        elif tag == b"IEND":
            break

    ba = bytearray(zlib.decompress(idat))
    ncomp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = ncomp * max(1, depth // 8)
    px = [None] * (width * height)
    # Adam7 pass table: (x0, y0, xstep, ystep)
    passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4),
              (0, 2, 2, 4), (1, 0, 2, 2), (0, 0, 1, 1)] if interlace else [(0, 0, 1, 1)]

    def _decode_pass(pw, ph):
        sub = []
        prev = bytearray(pw * bpp)
        for _y in range(ph):
            f = ba[0]
            ba.pop(0)
            raw = ba[:pw * bpp]
            del ba[:pw * bpp]
            line = bytearray(raw)
            if f in (1,):  # Sub
                for i in range(bpp, len(line)):
                    line[i] = (line[i] + line[i - bpp]) & 0xFF
            elif f in (2,):  # Up
                for i in range(len(line)):
                    line[i] = (line[i] + prev[i]) & 0xFF
            elif f in (3,):  # Average
                for i in range(len(line)):
                    a = line[i - bpp] if i >= bpp else 0
                    line[i] = (line[i] + ((a + prev[i]) >> 1)) & 0xFF
            elif f in (4,):  # Paeth
                for i in range(len(line)):
                    a = line[i - bpp] if i >= bpp else 0
                    b = prev[i]
                    c = prev[i - bpp] if i >= bpp else 0
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                    line[i] = (line[i] + pr) & 0xFF
            elif f != 0:
                raise ValueError("bad filter %d in %s" % (f, path))
            sub.append(line)
            prev = line
        return sub

    def _sample(line, x):
        if ctype == 6:  # RGBA
            return line[x * 4:x * 4 + 4]
        if ctype == 2:  # RGB
            r, g, b = line[x * 3:x * 3 + 3]
            return (r, g, b, 255)
        if ctype == 4:  # GA
            gv, a = line[x * 2:x * 2 + 2]
            return (gv, gv, gv, a)
        if ctype == 0:  # Gray
            return (line[x], line[x], line[x], 255)
        idx = line[x]  # Palette
        r, g, b = pal[idx]
        a = pal_alpha[idx] if pal_alpha and idx < len(pal_alpha) else 255
        return (r, g, b, a)

    for x0, y0, xstep, ystep in passes:
        pw = (width - x0 + xstep - 1) // xstep if width > x0 else 0
        ph = (height - y0 + ystep - 1) // ystep if height > y0 else 0
        if pw == 0 or ph == 0:
            continue
        for sy, line in enumerate(_decode_pass(pw, ph)):
            for sx in range(pw):
                px[(y0 + sy * ystep) * width + (x0 + sx * xstep)] = _sample(line, sx)
    return width, height, px


def to_argb(px):
    return [0xFF000000 | (c[0] << 16) | (c[1] << 8) | c[2] if c[3] == 255
            else ((c[3] << 24) | (c[0] << 16) | (c[1] << 8) | c[2])
            for c in px]


def main():
    theme = sys.argv[1] if len(sys.argv) > 1 else None
    if not theme:
        sys.exit("usage: convert_chicago95_icons.py <Chicago95 Icons dir>")

    icons = {}
    for name, rel in sorted(ICONS.items()):
        p = os.path.join(theme, rel)
        w, h, px = decode_png(resolve(p))
        if (w, h) != (SIZE, SIZE):
            sys.exit("%s: expected %dx%d, got %dx%d" % (rel, SIZE, SIZE, w, h))
        icons[name] = px
        print("ok %-12s %dx%d %s" % (name, w, h, rel))

    lines = [
        "/* Auto-generated by scripts/convert_chicago95_icons.py - DO NOT EDIT.",
        " * Icons from the Chicago95 theme pack (https://github.com/grassmunk/Chicago95),",
        " * CC BY-SA 4.0. Pixels are 0xAARRGGBB, %dx%d, row-major (top-left first). */"
        % (SIZE, SIZE),
        "#ifndef PIXEL_ICONS_H",
        "#define PIXEL_ICONS_H",
        "",
        "#include <stdint.h>",
        "",
        "#define PIXEL_ICON_W %d" % SIZE,
        "#define PIXEL_ICON_H %d" % SIZE,
        "",
    ]
    for name in icons:
        arr = ",".join("0x%08X" % v for v in to_argb(icons[name]))
        lines.append("static const uint32_t %s_icon_px[%d] = {%s};"
                     % (name, SIZE * SIZE, arr))
        lines.append("")
    lines.append("typedef struct {")
    lines.append("    const char *name;")
    lines.append("    uint16_t w;")
    lines.append("    uint16_t h;")
    lines.append("    const uint32_t *data;")
    lines.append("} pixel_icon_t;")
    lines.append("")
    lines.append("static const pixel_icon_t pixel_icons[] = {")
    for name in icons:
        lines.append('    {"%s", %d, %d, %s_icon_px},' % (name, SIZE, SIZE, name))
    lines.append("};")
    lines.append("#endif")
    os.makedirs(os.path.dirname(OUT_HEADER), exist_ok=True)
    open(OUT_HEADER, "w", newline="\n").write("\n".join(lines) + "\n")
    print("wrote %s" % OUT_HEADER)

    # Contact sheet preview (8x scaled) so the icons can be eyeballed.
    scale = 8
    cell = SIZE * scale + 8
    n = len(icons)
    cols = 5
    rows = (n + cols - 1) // cols
    os.makedirs(os.path.dirname(OUT_PREVIEW), exist_ok=True)
    sheet = bytearray(b"\xFF" * (cell * cols * cell * rows * 3))
    for i, name in enumerate(icons):
        cx, cy = i % cols, i // cols
        for y in range(SIZE):
            for x in range(SIZE):
                r, g, b, a = icons[name][y * SIZE + x]
                blit = r, g, b
                for dy in range(scale):
                    for dx in range(scale):
                        yy = cy * cell + 4 + y * scale + dy
                        xx = cx * cell + 4 + x * scale + dx
                        off = (yy * (cell * cols) + xx) * 3
                        sheet[off:off + 3] = bytearray(blit)
    def _write_png(path, w, h, rgb):
        def _chunk(tag, data):
            c = struct.pack(">I", len(data)) + tag + data
            return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
        raw = b""
        stride = w * 3
        for y in range(h):
            raw += b"\x00"
            raw += bytes(rgb[y * stride:(y + 1) * stride])
        ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
        open(path, "wb").write(
            b"\x89PNG\r\n\x1a\n"
            + _chunk(b"IHDR", ihdr)
            + _chunk(b"IDAT", zlib.compress(raw, 9))
            + _chunk(b"IEND", b""))
    _write_png(OUT_PREVIEW, cell * cols, cell * rows, sheet)
    print("wrote %s" % OUT_PREVIEW)


if __name__ == "__main__":
    main()