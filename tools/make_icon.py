"""Draws the port's icon (original artwork: a sword standing before a night sky
and distant mountains, between the letters DQ and IV) and writes it as a Windows .ico (16/32/48/256), a PNG
preview and a C array for SDL_SetWindowIcon. Pure Python, no image libraries.

usage: python tools/make_icon.py
"""
import math
import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_ICO = os.path.join(ROOT, "frontend", "dw4.ico")
OUT_PNG = os.path.join(ROOT, "frontend", "icon_preview.png")
OUT_C = os.path.join(ROOT, "frontend", "icon_data.c")


def lerp(a, b, t):
    return tuple(a[i] + (b[i] - a[i]) * t for i in range(3))


STARS = [(0.22, 0.20, 0.014), (0.80, 0.16, 0.012), (0.33, 0.42, 0.009), (0.72, 0.38, 0.010),
         (0.14, 0.50, 0.008), (0.86, 0.52, 0.009), (0.62, 0.12, 0.007), (0.40, 0.14, 0.007)]


def ridge(u):
    """height of the mountain line (v of its top) at u"""
    return 0.80 - 0.07 * abs(math.sin(u * 5.3 + 0.6)) - 0.05 * abs(math.sin(u * 11.0 + 2.0))


# the letters DQ and IV, left and right of the blade: blocky, 5 x 7 (the I is 3 wide)
GLYPHS = {
    "D": ("11110", "10001", "10001", "10001", "10001", "10001", "11110"),
    "Q": ("01110", "10001", "10001", "10001", "10101", "10010", "01101"),
    "I": ("111", "010", "010", "010", "010", "010", "111"),
    "V": ("10001", "10001", "10001", "10001", "01010", "01010", "00100"),
}
LETTERS = (("D", 0.085, 0.165), ("Q", 0.270, 0.165), ("I", 0.585, 0.085), ("V", 0.690, 0.175))   # letter, left edge, width
LETTER_TOP, LETTER_HEIGHT = 0.30, 0.25


def in_letter(u, v):
    if not LETTER_TOP <= v < LETTER_TOP + LETTER_HEIGHT:
        return False
    for ch, left, width in LETTERS:
        if left <= u < left + width:
            g = GLYPHS[ch]
            return g[int((v - LETTER_TOP) / LETTER_HEIGHT * 7)][int((u - left) / width * len(g[0]))] == "1"
    return False


def lettering(u, v):
    """1 inside a letter, 2 on its dark outline, 0 elsewhere"""
    if in_letter(u, v):
        return 1
    d = 0.013
    for du, dv in ((d, 0), (-d, 0), (0, d), (0, -d), (d, d), (-d, d), (d, -d), (-d, -d)):
        if in_letter(u + du, v + dv):
            return 2
    return 0


def color_at(u, v):
    """RGBA (0..1) of the artwork at normalised coordinates"""
    # rounded-square badge
    r = 0.22
    cx, cy = min(max(u, r), 1 - r), min(max(v, r), 1 - r)
    if math.hypot(u - cx, v - cy) > r:
        return (0, 0, 0, 0)
    # night sky: deep indigo at the top to a teal glow at the horizon
    col = lerp((0.06, 0.06, 0.22), (0.16, 0.42, 0.56), max(0.0, min(1.0, v / 0.85)) ** 1.8)
    for sx, sy, sr in STARS:
        d = math.hypot(u - sx, v - sy)
        if d < sr:
            col = lerp(col, (1.0, 0.97, 0.80), 1.0 - d / sr * 0.5)
    # mountains
    if v > ridge(u):
        col = lerp((0.07, 0.10, 0.24), (0.03, 0.04, 0.12), min(1.0, (v - ridge(u)) / 0.2))
    # the letters, gold with a dark outline
    k = lettering(u, v)
    if k == 1:
        col = lerp((1.00, 0.92, 0.50), (0.94, 0.64, 0.14), (v - LETTER_TOP) / LETTER_HEIGHT)
    elif k == 2:
        col = (0.03, 0.04, 0.14)
    # the sword: point up, blade, crossguard, grip and pommel
    x = u - 0.5
    blade_top, blade_bottom = 0.10, 0.66
    if blade_top <= v <= blade_bottom:
        half = 0.050 if v > blade_top + 0.09 else 0.050 * (v - blade_top) / 0.09
        if abs(x) <= half:
            light = (0.93, 0.95, 1.00) if x < 0 else (0.62, 0.68, 0.84)
            if abs(x) < 0.008 and v > blade_top + 0.10:
                light = (0.40, 0.46, 0.66)      # the fuller
            return (*light, 1.0)
    gold, gold_dark = (0.98, 0.80, 0.26), (0.72, 0.50, 0.12)
    if 0.66 <= v <= 0.715 and abs(x) <= 0.20:
        return (*(gold if v < 0.69 else gold_dark), 1.0)
    if math.hypot(abs(x) - 0.20, v - 0.6875) < 0.036:
        return (*gold, 1.0)
    if 0.715 < v <= 0.86 and abs(x) <= 0.030:
        return (*((0.42, 0.20, 0.12) if int(v * 60) % 2 else (0.30, 0.13, 0.08)), 1.0)
    if math.hypot(x, v - 0.885) < 0.050:
        return (*(gold if x < 0.005 else gold_dark), 1.0)
    return (*col, 1.0)


def render(size, ss=4):
    px = bytearray()
    for y in range(size):
        for x in range(size):
            acc = [0.0, 0.0, 0.0, 0.0]
            for sy in range(ss):
                for sx in range(ss):
                    u = (x + (sx + 0.5) / ss) / size
                    v = (y + (sy + 0.5) / ss) / size
                    c = color_at(u, v)
                    acc[0] += c[0] * c[3]; acc[1] += c[1] * c[3]; acc[2] += c[2] * c[3]; acc[3] += c[3]
            n = ss * ss
            a = acc[3] / n
            if a > 0:
                rgb = [acc[i] / acc[3] for i in range(3)]
            else:
                rgb = [0, 0, 0]
            px += bytes(int(max(0, min(1, c)) * 255 + 0.5) for c in rgb) + bytes([int(a * 255 + 0.5)])
    return bytes(px)


def png(size, rgba):
    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + rgba[y * size * 4:(y + 1) * size * 4] for y in range(size))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def bmp_entry(size, rgba):
    """32-bit DIB with an empty AND mask, rows bottom-up, BGRA"""
    header = struct.pack("<IiiHHIIiiII", 40, size, size * 2, 1, 32, 0, size * size * 4, 0, 0, 0, 0)
    rows = []
    for y in range(size - 1, -1, -1):
        row = rgba[y * size * 4:(y + 1) * size * 4]
        rows.append(b"".join(bytes((row[i + 2], row[i + 1], row[i], row[i + 3])) for i in range(0, len(row), 4)))
    mask_row = b"\x00" * (((size + 31) // 32) * 4)
    return header + b"".join(rows) + mask_row * size


def main():
    sizes = (16, 32, 48, 256)
    images = {}
    for s in sizes:
        images[s] = render(s, ss=6 if s <= 48 else 2)
        print(f"rendered {s}x{s}")
    entries = []
    for s in sizes:
        data = png(s, images[s]) if s == 256 else bmp_entry(s, images[s])
        entries.append((s, data))
    offset = 6 + 16 * len(entries)
    ico = struct.pack("<HHH", 0, 1, len(entries))
    body = b""
    for s, data in entries:
        ico += struct.pack("<BBBBHHII", s if s < 256 else 0, s if s < 256 else 0, 0, 0, 1, 32, len(data), offset + len(body))
        body += data
    with open(OUT_ICO, "wb") as f:
        f.write(ico + body)
    with open(OUT_PNG, "wb") as f:
        f.write(png(256, images[256]))
    w = 64
    data = render(w, ss=4)
    with open(OUT_C, "w", newline="\n") as f:
        f.write("/* generated by tools/make_icon.py: the window icon, 64x64 RGBA (original artwork) */\n")
        f.write("#include <stdint.h>\n")
        f.write(f"const int icon_width = {w}, icon_height = {w};\n")
        f.write(f"const uint8_t icon_rgba[{w * w * 4}] = {{\n")
        for i in range(0, len(data), 32):
            f.write("    " + ",".join(str(b) for b in data[i:i + 32]) + ",\n")
        f.write("};\n")
    print("wrote", OUT_ICO, OUT_PNG, OUT_C)


if __name__ == "__main__":
    main()
