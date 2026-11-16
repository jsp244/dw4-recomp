#!/usr/bin/env python3
"""tools/sfc/sfcfig.py - the library of figures (the people on the maps) seen in captures of the Super
Famicom games.

Dragon Quest I.II draws everyone as one 16x16 sprite; a character's eight pictures (four facings, two
steps) lie side by side in the sprite tiles: two rows of sixteen 8x8 tiles. For every sprite a capture
shows, this tool takes the whole set it belongs to, in that sprite's palette.

Dragon Quest III draws everyone 16x24, as six 8x8 sprites (two columns of three), and loads a character's tiles
as one run from the ROM, where they lie uncompressed; which six make a picture is not in a table this tool knows,
so it keeps every different picture the captures show, the characters told apart by where their tiles lie in
the ROM. Which picture is which facing and step is then said in the map file (pack.py: cast ... poses).
The party (the hero and the companions behind him) has its tiles loaded picture by picture, from one block of
the ROM per class and sex ($C40 bytes each from $0E0000: the hero, the soldier, ...): build3p keeps only those,
a set per block (its number is the block's), a set's pictures in the order they lie in the ROM. Companions
walking in a column overlap on the screen, so there a figure's six sprites are those nearest it in the sprite
list.

usage:
    sfcfig.py build LIB.figs NAME=CAPTURE...
    sfcfig.py build3 LIB.figs DQ3.sfc NAME=CAPTURE...   Dragon Quest III's
    sfcfig.py build3p LIB.figs DQ3.sfc NAME=CAPTURE...  Dragon Quest III's party: the hero and his companions
    sfcfig.py sheet LIB.figs OUT.png
The library holds the games' art: it stays with your own copies (generated/ is git-ignored).
"""
import os
import pickle
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from snesdump import Dump


def argb(c):
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return 0xFF000000 | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2))


def cell(d, tile, pal):
    """the 16x16 sprite at 8x8 tile number `tile` (0-511) in sprite palette `pal`: 256 ARGB values, 0 = clear"""
    px = [0] * 256
    for q in range(4):
        n = tile + (q & 1) + (q >> 1) * 16
        base = d.regs["objTileAdr1"] if n < 256 else d.regs["objTileAdr2"]
        t = d.tile_pixels(base, n & 255, 4)
        for y in range(8):
            for x in range(8):
                v = t[y][x]
                if v:
                    px[((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x] = argb(d.cg[128 + pal * 16 + v])
    return tuple(px)


def build(lib_path, named):
    figs, seen, sets = [], set(), set()
    for item in named:
        name, path = item.split("=", 1)
        d = Dump(path)
        for i in range(128):
            x, y, t, a = d.oam[i * 4:i * 4 + 4]
            hb = (d.hoam[i >> 2] >> ((i & 3) * 2)) & 3
            if not hb >> 1 or y >= 224:
                continue                    # only the 16x16 ones on the screen
            tile, pal = t | ((a & 1) << 8), (a >> 1) & 7
            row = tile & ~0x1F              # the set: two rows of tiles
            colors = tuple(d.cg[128 + pal * 16:128 + pal * 16 + 16])
            tiles = tuple(tuple(map(tuple, d.tile_pixels(d.regs["objTileAdr1"] if n < 256 else d.regs["objTileAdr2"], n & 255, 4)))
                          for n in range(row, row + 32))
            if (tiles, colors) in sets:
                continue
            sets.add((tiles, colors))
            group = len(sets) - 1
            for k in range(8):
                px = cell(d, row + k * 2, pal)
                if not any(px) or px in seen:
                    continue
                seen.add(px)
                figs.append({"px": px, "w": 16, "h": 16, "from": name, "group": group, "pose": k})
    with open(lib_path, "wb") as f:
        pickle.dump(figs, f)
    print("%s: %d figures in %d sets from %d captures" % (lib_path, len(figs), len(sets), len(named)))


PARTY_AT, PARTY_END, PARTY_BLOCK = 0x0E0000, 0x0F0000, 0xC40


def build3(lib_path, rom_path, named, party=False):
    rom = open(rom_path, "rb").read()
    if len(rom) % 0x8000 == 512:
        rom = rom[512:]
    where = {}                                  # a tile's 32 bytes -> its place in the ROM

    def rom_of(b):
        if b not in where:
            where[b] = rom.find(b)
        return where[b]

    figs, seen, groups = [], set(), {}
    for item in named:
        name, path = item.split("=", 1)
        d = Dump(path)
        vram = open(path + ".vram", "rb").read()
        spr = []
        for i in range(128):
            x, y, t, a = d.oam[i * 4:i * 4 + 4]
            hb = (d.hoam[i >> 2] >> ((i & 3) * 2)) & 3
            if hb >> 1 or y >= 224 or (hb & 1):
                continue                        # the small ones on the screen
            spr.append((i, x, y, t | ((a & 1) << 8), (a >> 1) & 7, (a >> 6) & 1, a >> 7))
        at = {(s[1], s[2]): s for s in spr}
        every = {}
        for s in spr:
            every.setdefault((s[1], s[2]), []).append(s)
        used = set()
        for s in spr:
            i, x, y, t, pal, hf, vf = s
            if i in used:
                continue
            if party:               # (two figures may have a sprite at the same place: the one next in the list is its own)
                parts = []
                for dx in (0, 8):
                    for dy in (0, 8, 16):
                        c = [p for p in every.get((x + dx, (y + dy) & 255), []) if p[4] == pal and p[0] not in used and abs(p[0] - i) < 6]
                        parts.append(min(c, key=lambda p: abs(p[0] - i)) if c else None)
            else:
                parts = [at.get((x + dx, (y + dy) & 255)) for dx in (0, 8) for dy in (0, 8, 16)]
            if any(p is None or p[4] != pal or p[0] in used for p in parts):
                continue
            used.update(p[0] for p in parts)
            px = [0] * (16 * 24)
            offs = []
            for k, p in enumerate(parts):
                n = p[3]
                base = d.regs["objTileAdr1"] if n < 256 else d.regs["objTileAdr2"]
                tp = d.tile_pixels(base, n & 255, 4)
                a0 = ((base + (n & 255) * 16) & 0x7FFF) * 2
                offs.append(rom_of(vram[a0:a0 + 32]))
                for yy in range(8):
                    for xx in range(8):
                        v = tp[7 - yy if p[6] else yy][7 - xx if p[5] else xx]
                        if v:
                            px[((k % 3) * 8 + yy) * 16 + (k // 3) * 8 + xx] = argb(d.cg[128 + pal * 16 + v])
            px = tuple(px)
            if not any(px) or px in seen or min(offs) < 0:
                continue
            if party:
                if not PARTY_AT <= min(offs) < PARTY_END:
                    continue
                seen.add(px)
                figs.append({"px": px, "w": 16, "h": 24, "from": name, "group": (min(offs) - PARTY_AT) // PARTY_BLOCK,
                             "pose": 0, "rom": min(offs), "roms": tuple(offs)})
                continue
            seen.add(px)
            # the character: the run of tiles this picture's first tile was loaded with (the game copies a
            # character's tiles from the ROM as one run), in these colors
            n0 = min(p[3] for p in parts)
            base0 = d.regs["objTileAdr1"] if n0 < 256 else d.regs["objTileAdr2"]

            def rom_at(n):
                a1 = ((base0 + (n & 255) * 16) & 0x7FFF) * 2
                return rom_of(vram[a1:a1 + 32])
            start = rom_at(n0)
            n = n0
            while n & 255 and rom_at(n - 1) == start - 32 and start > 0:
                n -= 1
                start -= 32
            who = (start, tuple(d.cg[128 + pal * 16 + 1:128 + pal * 16 + 16]))
            if who not in groups:
                groups[who] = len(groups)
            group = groups[who]
            pose = sum(1 for e in figs if e["group"] == group)
            figs.append({"px": px, "w": 16, "h": 24, "from": name, "group": group, "pose": pose, "rom": min(offs)})
    if party:                   # a set's pictures in the order they lie in the ROM
        figs.sort(key=lambda e: (e["group"], e["rom"], e["roms"], e["px"]))
        for n, e in enumerate(figs):
            e["pose"] = sum(1 for o in figs[:n] if o["group"] == e["group"])
            del e["roms"]
        groups = set(e["group"] for e in figs)
    figs.sort(key=lambda e: (e["group"], e["pose"]))
    with open(lib_path, "wb") as f:
        pickle.dump(figs, f)
    print("%s: %d figures of %d characters from %d captures" % (lib_path, len(figs), len(groups), len(named)))


def sheet(lib_path, out):
    from PIL import Image, ImageDraw
    figs = pickle.load(open(lib_path, "rb"))
    cols, size = 12, 54
    tall = max(e["h"] for e in figs) * 3 + 6
    im = Image.new("RGB", (cols * size, ((len(figs) + cols - 1) // cols) * (tall + 10)), (70, 70, 90))
    dr = ImageDraw.Draw(im)
    px = im.load()
    for i, e in enumerate(figs):
        ox, oy = (i % cols) * size + 3, (i // cols) * (tall + 10) + 11
        for y in range(e["h"] * 3):
            for x in range(e["w"] * 3):
                c = e["px"][(y // 3) * e["w"] + x // 3]
                if c >> 24:
                    px[ox + x, oy + y] = ((c >> 16) & 255, (c >> 8) & 255, c & 255)
        dr.text((ox, oy - 11), "%d g%d p%d" % (i, e["group"], e["pose"]), fill=(255, 255, 0))
    im.save(out)


def main():
    a = sys.argv
    if len(a) >= 4 and a[1] == "build":
        build(a[2], a[3:])
    elif len(a) >= 5 and a[1] == "build3":
        build3(a[2], a[3], a[4:])
    elif len(a) >= 5 and a[1] == "build3p":
        build3(a[2], a[3], a[4:], party=True)
    elif len(a) == 4 and a[1] == "sheet":
        sheet(a[2], a[3])
    else:
        sys.stdout.write(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
