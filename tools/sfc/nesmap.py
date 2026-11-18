#!/usr/bin/env python3
"""tools/sfc/nesmap.py - the maps of the NES game as the game's own map code gives them.

`nes-headless --map-dump FILE` asks the game for every square of the map the player stands on. This
tool draws such a dump and lists its squares: a square is four 8x8 tiles (as they are in the CHR RAM
at that moment) and one of the four background palettes, and it is known by the hash of those 64
bytes and the palette's number, the key the 16-bit textures (runtime/sfc.c) are looked up with.

usage:
    nesmap.py render DUMP OUT.png            the map (the part that is not all one square)
    nesmap.py squares OUT_PREFIX DUMP...     OUT_PREFIX.png: every different square of the dumps, numbered;
                                             OUT_PREFIX.json: their keys, tiles, palettes and where they are
                                             ("scene" 1: the overworld, whose squares have art of their own;
                                             "map": the mark of the map's kind ($28), for art bound to it:
                                             pack.py's "here")
    nesmap.py figures OUT_PREFIX DUMP...     the same for the people of the dumps' maps: every kind's four facings
                                             and two steps, from the game's own sprite definitions
"""
import json
import os
import struct
import sys

# the NES's colours (the 2C02's, a common measurement), for pictures of dumps only
NES_RGB = [
    0x666666, 0x002A88, 0x1412A7, 0x3B00A4, 0x5C007E, 0x6E0040, 0x6C0600, 0x561D00, 0x333500, 0x0B4800, 0x005200, 0x004F08, 0x00404D, 0x000000, 0x000000, 0x000000,
    0xADADAD, 0x155FD9, 0x4240FF, 0x7527FE, 0xA01ACC, 0xB71E7B, 0xB53120, 0x994E00, 0x6B6D00, 0x388700, 0x0C9300, 0x008F32, 0x007C8D, 0x000000, 0x000000, 0x000000,
    0xFFFEFF, 0x64B0FF, 0x9290FF, 0xC676FF, 0xF36AFF, 0xFE6ECC, 0xFE8170, 0xEA9E22, 0xBCBE00, 0x88D800, 0x5CE430, 0x45E082, 0x48CDDE, 0x4F4F4F, 0x000000, 0x000000,
    0xFFFEFF, 0xC0DFFF, 0xD3D2FF, 0xE8C8FF, 0xFBC2FF, 0xFEC4EA, 0xFECCC5, 0xF7D8A5, 0xE4E594, 0xCFEF96, 0xBDF4AB, 0xB3F3CC, 0xB5EBF2, 0xB8B8B8, 0x000000, 0x000000,
]


def fnv(data, h=0xcbf29ce484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def tile_hash(tile):
    """a tile's 16 bytes as runtime/sfc.c hashes them"""
    return fnv(tile) or 1


def square_key(tiles, pal):
    """the key of a map square (runtime/sfc.c): the hash of its four tiles' hashes (top left, top right, bottom
    left, bottom right; each 8 bytes, low first), then the palette's number"""
    import struct as _s
    return fnv(bytes([pal]), fnv(b"".join(_s.pack("<Q", tile_hash(t)) for t in tiles))) or 1


class MapDump:
    def __init__(self, path):
        d = open(path, "rb").read()
        if d[:4] != b"DWMP" or d[4] != 1:
            raise SystemExit("%s: not a map dump" % path)
        self.path = path
        self.ctrl, self.px, self.py, self.kind = d[8], d[9], d[10], d[11]
        o = 16
        self.pal = d[o:o + 32]; o += 32
        self.sq = d[o:o + 256 * 256 * 5]; o += 256 * 256 * 5
        self.chr = d[o:o + 8192]; o += 8192
        self.ram = d[o:o + 2048]; o += 2048
        self.wram = d[o:o + 8192]; o += 8192
        self.oam = d[o:o + 256]
        self.base = 256 if self.ctrl & 0x10 else 0     # the background's half of the CHR RAM

    def square(self, mx, my):
        o = (my * 256 + mx) * 5
        return self.sq[o:o + 4], self.sq[o + 4]

    def tile(self, n):
        return self.chr[(self.base + n) * 16:(self.base + n) * 16 + 16]

    def key(self, mx, my):
        t, p = self.square(mx, my)
        return square_key([self.tile(n) for n in t], p)

    def pixels(self, tiles, pal):
        """16x16 RGB values of a square"""
        out = [[0] * 16 for _ in range(16)]
        for q, n in enumerate(tiles):
            t = self.tile(n)
            for y in range(8):
                for x in range(8):
                    v = ((t[y] >> (7 - x)) & 1) | (((t[y + 8] >> (7 - x)) & 1) << 1)
                    c = self.pal[pal * 4 + v] if v else self.pal[0]
                    out[(q >> 1) * 8 + y][(q & 1) * 8 + x] = NES_RGB[c & 0x3F]
        return out

    def bounds(self):
        """the box of squares that are not the commonest one"""
        count = {}
        for i in range(0, len(self.sq), 5):
            k = self.sq[i:i + 5]
            count[k] = count.get(k, 0) + 1
        border = max(count, key=count.get)
        x0, y0, x1, y1 = 256, 256, -1, -1
        for my in range(256):
            row = self.sq[my * 1280:(my + 1) * 1280]
            for mx in range(256):
                if row[mx * 5:mx * 5 + 5] != border:
                    x0 = min(x0, mx); x1 = max(x1, mx); y0 = min(y0, my); y1 = max(y1, my)
        if x1 < 0:
            return 0, 0, 16, 16
        return max(0, x0 - 1), max(0, y0 - 1), min(256, x1 + 2), min(256, y1 + 2)


def render(dump, out):
    from PIL import Image
    d = MapDump(dump)
    x0, y0, x1, y1 = d.bounds()
    im = Image.new("RGB", ((x1 - x0) * 16, (y1 - y0) * 16))
    px = im.load()
    cache = {}
    for my in range(y0, y1):
        for mx in range(x0, x1):
            t, p = d.square(mx, my)
            k = (bytes(t), p)
            if k not in cache:
                cache[k] = d.pixels(t, p)
            s = cache[k]
            for y in range(16):
                for x in range(16):
                    c = s[y][x]
                    px[(mx - x0) * 16 + x, (my - y0) * 16 + y] = (c >> 16, (c >> 8) & 255, c & 255)
    im.save(out)
    print("%s: squares %d-%d x %d-%d, the player at %d,%d, map kind %02X" % (out, x0, x1 - 1, y0, y1 - 1, d.px, d.py, d.kind))


def squares(prefix, dumps):
    from PIL import Image, ImageDraw
    found = {}
    order = []
    for path in dumps:
        d = MapDump(path)
        x0, y0, x1, y1 = d.bounds()
        for my in range(y0, y1):
            for mx in range(x0, x1):
                t, p = d.square(mx, my)
                tiles = [d.tile(n) for n in t]
                k = square_key(tiles, p)
                if k not in found:
                    found[k] = {"key": "%016X" % k, "scene": 0 if d.kind & 0x80 else 1, "map": "%016X" % (((d.ram[0x28] + 1) * 0xD6E8FEB86659FD93) & 0xFFFFFFFFFFFFFFFF), "pal": p, "colors": [d.pal[p * 4 + i] if i else d.pal[0] for i in range(4)],
                                "tiles": [bytes(x).hex() for x in tiles], "count": 0, "dump": path.replace("\\", "/").split("/")[-1],
                                "at": [mx, my], "_px": d.pixels(t, p)}
                    order.append(k)
                found[k]["count"] += 1
    order.sort(key=lambda k: -found[k]["count"])
    cols, cell = 16, 54
    im = Image.new("RGB", (cols * cell, ((len(order) + cols - 1) // cols) * (cell + 10)), (30, 30, 30))
    dr = ImageDraw.Draw(im)
    px = im.load()
    for i, k in enumerate(order):
        s = found[k].pop("_px")
        found[k]["id"] = i
        ox, oy = (i % cols) * cell + 3, (i // cols) * (cell + 10) + 11
        for y in range(48):
            for x in range(48):
                c = s[y // 3][x // 3]
                px[ox + x, oy + y] = (c >> 16, (c >> 8) & 255, c & 255)
        dr.text((ox, oy - 11), "%d" % i, fill=(255, 255, 0))
    im.save(prefix + ".png")
    with open(prefix + ".json", "w") as f:
        json.dump([found[k] for k in order], f, indent=0)
    print("%s: %d different squares" % (prefix, len(order)))


def figure_key(pattern):
    """the key of a figure (runtime/sfc.c): the hash of its 16x16 two-bit pixels, row by row"""
    return fnv(bytes(pattern)) or 1


def figures(prefix, dumps):
    """Everyone the maps' people can be: the game keeps each kind's sprites (tile and attributes of the four
    quarters, for four facings and two steps) at $7200 while a map is loaded."""
    from PIL import Image, ImageDraw
    found, order = {}, []
    for path in dumps:
        d = MapDump(path)
        base = 256 if d.ctrl & 0x08 else 0
        for kind in range(16):
            poses = {}
            for facing in range(4):
                for step in range(2):
                    o = 0x1200 + kind * 64 + facing * 16 + step * 8
                    quarters = [(d.wram[o + q * 2], d.wram[o + q * 2 + 1]) for q in range(4)]
                    pat = [0] * 256
                    colors = []
                    for q, (tile, attr) in enumerate(quarters):
                        t = d.chr[(base + tile) * 16:(base + tile) * 16 + 16]
                        for y in range(8):
                            row = 7 - y if attr & 0x80 else y
                            for x in range(8):
                                bit = x if attr & 0x40 else 7 - x
                                pat[((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x] = ((t[row] >> bit) & 1) | (((t[row + 8] >> bit) & 1) << 1)
                        colors += [d.pal[16 + (attr & 3) * 4 + 1 + i] for i in range(3)]
                    if not any(pat):
                        continue
                    k = (figure_key(pat), tuple(colors), len(order))      # (every pose of every kind is listed, alike or not)
                    px = [0] * 256
                    for i, v in enumerate(pat):
                        q = (i // 16 // 8) * 2 + (i % 16) // 8
                        px[i] = NES_RGB[colors[q * 3 + v - 1] & 0x3F] if v else None
                    found[k] = {"key": "%016X" % k[0], "colors": list(colors), "kind": kind, "facing": facing, "step": step,
                                "dump": os.path.splitext(os.path.basename(path))[0], "_px": px}
                    order.append(k)
                    poses[(facing, step)] = (pat, found[k])
            # Someone lying down (a sleeper in his bed, the wounded soldier of the tower): the game has no pictures
            # of its own for that, the kind's four views are one picture, its front view standing, turned: upright,
            # a quarter clockwise, upside down, a quarter the other way, both steps alike. Marked for pack.py
            # ("turn": the quarters, 0 for the upright one; "of": the upright picture's key).
            if len(poses) == 8 and all(poses[(g, 0)][0] == poses[(g, 1)][0] for g in range(4)):
                turned = [poses[(0, 0)][0]]
                for _ in range(3):
                    turned.append([turned[-1][(15 - x) * 16 + y] for y in range(16) for x in range(16)])
                if turned[1] != turned[0] and turned[2] != turned[0] and all(poses[(g, 0)][0] == turned[g] for g in range(4)):
                    for g in range(4):
                        for step in range(2):
                            poses[(g, step)][1].update({"turn": g, "of": poses[(0, 0)][1]["key"]})
    cols, cell = 8, 54
    im = Image.new("RGB", (cols * cell, ((len(order) + cols - 1) // cols) * (cell + 10)), (70, 70, 90))
    dr = ImageDraw.Draw(im)
    px = im.load()
    for i, k in enumerate(order):
        e = found[k]
        s = e.pop("_px")
        e["id"] = i
        ox, oy = (i % cols) * cell + 3, (i // cols) * (cell + 10) + 11
        for y in range(48):
            for x in range(48):
                c = s[(y // 3) * 16 + x // 3]
                if c is not None:
                    px[ox + x, oy + y] = (c >> 16, (c >> 8) & 255, c & 255)
        dr.text((ox, oy - 11), "%d k%d f%d s%d" % (i, e["kind"], e["facing"], e["step"]), fill=(255, 255, 0))
    im.save(prefix + ".png")
    with open(prefix + ".json", "w") as f:
        json.dump([found[k] for k in order], f, indent=0)
    print("%s: %d different figures" % (prefix, len(order)))


def main():
    a = sys.argv
    if len(a) >= 4 and a[1] == "figures":
        figures(a[2], a[3:])
    elif len(a) == 4 and a[1] == "render":
        render(a[2], a[3])
    elif len(a) >= 4 and a[1] == "squares":
        squares(a[2], a[3:])
    else:
        sys.stdout.write(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
