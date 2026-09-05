import os, zlib, struct

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = os.path.join(ROOT, "repo")
os.makedirs(REPO, exist_ok=True)

def make_png(w, h):
    # RGBA-ish RGB gradient with a white square and red disc for visual check
    rows = []
    for y in range(h):
        row = bytearray([0])  # filter type 0 (None)
        for x in range(w):
            r = x * 255 // (w - 1)
            g = y * 255 // (h - 1)
            b = 128
            if 40 <= x <= 160 and 30 <= y <= 120:
                b = 255
            if (x - 100) ** 2 + (y - 75) ** 2 <= 30 ** 2:
                r, g, b = 255, 0, 0
            row += bytes((r, g, b))
        rows.append(bytes(row))
    raw = b"".join(rows)

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)

    ihdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)  # 8-bit truecolor
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", ihdr)
    png += chunk(b"IDAT", zlib.compress(raw, 6))
    png += chunk(b"IEND", b"")
    return png

def make_bmp(w, h):
    # 24-bit BMP (bottom-up rows), also decodable by stb_image
    row_size = (w * 3 + 3) & ~3
    data_size = row_size * h
    header = struct.pack("<2sIHHI", b"BM", 14 + 40 + data_size, 0, 0, 14 + 40)
    dib = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, data_size, 2835, 2835, 0, 0)
    pixels = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray()
        for x in range(w):
            r = x * 255 // (w - 1)
            g = y * 255 // (h - 1)
            b = 128
            if 40 <= x <= 160 and 30 <= y <= 120:
                b = 255
            row += bytes((b, g, r))  # BMP stores BGR
        row += b"\x00" * (row_size - w * 3)
        pixels += row
    return header + dib + pixels

w, h = 220, 160
png = make_png(w, h)
bmp = make_bmp(w, h)
with open(os.path.join(REPO, "teste.png"), "wb") as f:
    f.write(png)
with open(os.path.join(REPO, "teste.bmp"), "wb") as f:
    f.write(bmp)
print("Wrote repo/teste.png (%d bytes) and repo/teste.bmp (%d bytes) %dx%d" % (len(png), len(bmp), w, h))

# Seed a nested image so the viewver's recursive scan has something to find
# in a subdirectory (becomes /imgs/nested.png on the SDFS disk).
nested_dir = os.path.join(REPO, "imgs")
os.makedirs(nested_dir, exist_ok=True)
nw, nh = 80, 60
nested = make_png(nw, nh)
with open(os.path.join(nested_dir, "nested.png"), "wb") as f:
    f.write(nested)
print("Wrote repo/imgs/nested.png (%d bytes) %dx%d" % (len(nested), nw, nh))
