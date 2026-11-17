#!/usr/bin/env python3
"""tools/sfc/sfclib.py - the library of 16x16 map squares seen in captures of the Super Famicom games.

A capture is the memory of the running game at one frame (tools/sfc/capture writes them: work RAM,
VRAM, CGRAM, OAM and the PPU's registers). Both games draw their maps from 16x16 squares on two
4-bit layers that scroll together (the ground, and what stands on it); this tool cuts every capture's
screen into those squares, with the layers put together and the palette applied, and keeps each
different square once.

usage:
    sfclib.py build LIB.sfclib NAME=CAPTURE...     cut the captures (CAPTURE: the dump's path, without .vram etc.)
                                                   ("parity=1,1" before the names: the map's grid in the tile map, see cut)
    sfclib.py dq3-overworld LIB.sfclib DQ3.sfc CAPTURE   Dragon Quest III's overworld blocks, read from the ROM itself
                                                   (the colors from a capture of its overworld)
    sfclib.py sheet LIB.sfclib OUT.png [FIRST [COUNT]]   the squares, numbered
The library holds the games' art: it stays with your own copies (generated/ is git-ignored).
"""
import os
import pickle
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snesdump import Dump


def layer_squares(d, layers, ox, oy):
    """the 16x16 squares of the screen, the layers' tile columns and rows starting at parity (ox, oy):
    {(col, row): 256 RGB values}"""
    out = {}
    tiles = {}
    b0 = d.bg[layers[0]]
    sx, sy = b0["hScroll"], b0["vScroll"] + 1
    # the first whole square on the screen
    tx0, ty0 = -(-sx // 8), -(-sy // 8)
    if (tx0 & 1) != ox:
        tx0 += 1
    if (ty0 & 1) != oy:
        ty0 += 1
    for r in range(14):
        for c in range(16):
            tx, ty = tx0 + c * 2, ty0 + r * 2
            if (tx + 2) * 8 - sx > 256 or (ty + 2) * 8 - sy > 224:
                continue
            px = [None] * 256
            lo, hi = [None] * 256, [None] * 256
            sig = []
            for layer in layers:
                bpp = d.bpp(layer)
                base = d.bg[layer]["tileAdr"]
                for q in range(4):
                    e = d.tilemap_entry(layer, tx + (q & 1), ty + (q >> 1))
                    sig.append(e)
                    num, pal, hf, vf = e & 0x3FF, (e >> 10) & 7, (e >> 14) & 1, (e >> 15) & 1
                    if (layer, num) not in tiles:
                        tiles[(layer, num)] = d.tile_pixels(base, num, bpp)
                    t = tiles[(layer, num)]
                    for y in range(8):
                        for x in range(8):
                            v = t[7 - y if vf else y][7 - x if hf else x]
                            if v:
                                i = ((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x
                                px[i] = d.cg[pal * 16 + v]
                                (lo if layer == layers[0] else hi)[i] = d.cg[pal * 16 + v]
            back = d.cg[0]
            out[(c, r)] = (tuple(back if p is None else p for p in px), tuple(sig), (tx * 8 - sx, ty * 8 - sy),
                           tuple(back if p is None else p for p in lo), tuple(hi))
    return out


def cut(d, parity=None):
    """the capture's squares at the parity that repeats best (the map's own grid); or at the parity given: a game
    keeps its map's squares at the same tile columns and rows of the tile map from picture to picture, and saying
    which is surer than counting where planks and water repeat either way (Dragon Quest V's ship)"""
    layers = [1, 0]                      # BG2 under BG1
    if parity:
        return layer_squares(d, layers, parity[0], parity[1])
    best = None
    for oy in (0, 1):
        for ox in (0, 1):
            sq = layer_squares(d, layers, ox, oy)
            n = len(set(v[0] for v in sq.values()))
            if best is None or n < best[0]:
                best = (n, sq)
    return best[1]


def build(lib_path, named):
    squares = []            # {"px": 256 BGR555 words, "from": name, "at": (x, y) on that capture's screen, "count": n}
    index = {}
    parity = None
    if named and named[0].startswith("parity="):        # "parity=1,1": see cut()
        parity = tuple(int(v) for v in named[0][7:].split(","))
        named = named[1:]
    for item in named:
        name, path = item.split("=", 1)
        d = Dump(path)
        for (c, r), (px, sig, at, lo, hi) in sorted(cut(d, parity).items()):
            if px not in index:
                index[px] = len(squares)
                squares.append({"px": px, "from": name, "at": at, "count": 0, "sig": sig, "lo": lo, "hi": hi})
            squares[index[px]]["count"] += 1
    with open(lib_path, "wb") as f:
        pickle.dump(squares, f)
    print("%s: %d different squares from %d captures" % (lib_path, len(squares), len(named)))


def dq3_overworld(lib_path, rom_path, capture):
    """Dragon Quest III's overworld squares straight from the cartridge: its 16x16 blocks are a table of four
    tilemap words each, its tiles lie uncompressed before them; the colors come from a capture of the overworld
    (the game cycles some of them, so they are not in the ROM as they are shown)."""
    import struct
    rom = open(rom_path, "rb").read()
    if len(rom) % 0x8000 == 512:
        rom = rom[512:]                 # a copier header
    d = Dump(capture)
    TILES, BLOCKS, COUNT = 0x10DE00, 0x254F38, 239
    tiles = {}

    def tile(n):
        if n not in tiles:
            b = rom[TILES + n * 32:TILES + n * 32 + 32]
            px = [[0] * 8 for _ in range(8)]
            for y in range(8):
                for p in range(4):
                    byte = b[y * 2 + (p & 1) + (16 if p >= 2 else 0)]
                    for x in range(8):
                        px[y][x] |= ((byte >> (7 - x)) & 1) << p
            tiles[n] = px
        return tiles[n]

    squares = []
    for i in range(COUNT):
        words = struct.unpack_from("<4H", rom, BLOCKS + i * 8)
        px = [d.cg[0]] * 256
        for q, e in enumerate(words):
            num, pal, hf, vf = e & 0xFF, (e >> 10) & 7, (e >> 14) & 1, (e >> 15) & 1
            t = tile(num)
            for y in range(8):
                for x in range(8):
                    v = t[7 - y if vf else y][7 - x if hf else x]
                    if v:
                        px[((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x] = d.cg[pal * 16 + v]
        squares.append({"px": tuple(px), "from": "block %d" % i, "at": (0, 0), "count": 1, "sig": words})
    with open(lib_path, "wb") as f:
        pickle.dump(squares, f)
    print("%s: %d overworld blocks" % (lib_path, len(squares)))


def rgb(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2))


def sheet(lib_path, out, first=0, count=None):
    from PIL import Image, ImageDraw
    squares = pickle.load(open(lib_path, "rb"))
    ids = list(range(first, len(squares) if count is None else min(len(squares), first + count)))
    cols, cell = 16, 54
    im = Image.new("RGB", (cols * cell, ((len(ids) + cols - 1) // cols) * (cell + 10)), (30, 30, 30))
    dr = ImageDraw.Draw(im)
    px = im.load()
    for i, n in enumerate(ids):
        s = squares[n]["px"]
        ox, oy = (i % cols) * cell + 3, (i // cols) * (cell + 10) + 11
        for y in range(48):
            for x in range(48):
                px[ox + x, oy + y] = rgb(s[(y // 3) * 16 + x // 3])
        dr.text((ox, oy - 11), "%d" % n, fill=(255, 255, 0))
    im.save(out)


def main():
    a = sys.argv
    if len(a) >= 4 and a[1] == "build":
        build(a[2], a[3:])
    elif len(a) == 5 and a[1] == "dq3-overworld":
        dq3_overworld(a[2], a[3], a[4])
    elif len(a) >= 4 and a[1] == "sheet":
        sheet(a[2], a[3], int(a[4]) if len(a) > 4 else 0, int(a[5]) if len(a) > 5 else None)
    else:
        sys.stdout.write(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
