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
the ROM per class and sex ($600 bytes each from $0E00E0, a class's man and then its woman: 0 the hero, 2 the
soldier, 4 the priest, 5 the female priest, 6 the mage, 7 the female mage, 9 the female fighter, 10 the merchant):
build3p keeps only those, a set per block (its number is the block's), a set's pictures in the order they lie in
the ROM. A picture may borrow a tile from another's (a woman's from the man's): most of its tiles say whose it is. Companions
walking in a column overlap on the screen, so there a figure's six sprites are those nearest it in the sprite
list.
Any other character of Dragon Quest III (the cat, the dog, the horse, the old woman, the slime: the game has some
150) is drawn by the game itself where extract.py asks for it: in a copy of the ROM the people on the ground floor
of Luisa's Place are given that character's number and the four facings, and the floor is loaded. buildn cuts
those captures: a set per character (its number is the game's), its pictures up, up, right, right, down, down,
left, left (the two steps of each facing), whatever size the game draws it.

usage:
    sfcfig.py build LIB.figs NAME=CAPTURE...
    sfcfig.py build3 LIB.figs DQ3.sfc NAME=CAPTURE...   Dragon Quest III's
    sfcfig.py build3p LIB.figs DQ3.sfc NAME=CAPTURE...  Dragon Quest III's party: the hero and his companions
    sfcfig.py buildn LIB.figs NAME=CAPTURE...           Dragon Quest III's people by number (NAME: nXX_a, nXX_b,
                                                        nXXR_a, nXXR_b: number XX in hex, the two steps, R =
                                                        the run that turns the one on the carpet right)
    sfcfig.py buildn6 LIB.figs NAME=CAPTURE...          Dragon Quest VI's people by number (NAME: vXXU_a, vXXU_b,
                                                        vXXR_.., vXXD_.., vXXL_..: the run that turns the one
                                                        at the cliff up, right, down, left)
    sfcfig.py buildn5 LIB.figs NAME=CAPTURE...          Dragon Quest V's people by number (NAME: qXXU_a, ..., as
                                                        buildn6: the one in the sailor's place on the deck)
    sfcfig.py buildall LIB.figs CAPTURE[@X0,Y0,X1,Y1]...  one figure of all the sprites of each capture (a
                                                        creature alone on the screen; with a box: only the
                                                        sprites that lie inside it)
    sfcfig.py buildscroll LIB.figs BG WIDTH ROW0-ROW1 CAPTURE...  a painting a background layer scrolls through
                                                        (BG 1-4), put together from what each capture shows of
                                                        it: WIDTH is its repeat, the rows are counted from the
                                                        first capture's scroll
    sfcfig.py buildwindow LIB.figs PICTURE.ppm X0,Y0,X1,Y1  a window's frame and colours, from a picture of the
                                                        screen on which it stands in that box
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


PARTY_AT, PARTY_END, PARTY_BLOCK = 0x0E00E0, 0x0F0000, 0x600


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
                blocks = [(o - PARTY_AT) // PARTY_BLOCK for o in offs]
                whose = max(set(blocks), key=lambda b: (blocks.count(b), b))
                figs.append({"px": px, "w": 16, "h": 24, "from": name, "group": whose, "pose": 0,
                             "rom": min(o for o, b in zip(offs, blocks) if b == whose), "roms": tuple(offs)})
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


# Where the people of Luisa's ground floor stand on the screen when the hero has come down the stairs (the corner of
# a 16x24 picture), and the first picture of the facing each run turns them to. The first run has the one behind
# the counter look up, the one in the middle of the room left and the one on the carpet down; the second run ("R")
# turns the one on the carpet right. (A character's left is not always its right mirrored: the soldier's spear.)
# The hero himself stands at HERO_AT.
SPOTS = {"": (((24, 112), 0), ((80, 96), 6), ((24, 192), 4)), "R": (((24, 192), 2),)}
HERO_AT = (120, 96)
# Dragon Quest VI (extract.py: DQ6_PEOPLE): one person, the man at the cliff's edge of the opening, in four runs that
# turn him up, right, down and left; the hero has stopped below him.
SPOTS6 = {"U": (((112, 64), 0),), "R": (((112, 64), 2),), "D": (((112, 64), 4),), "L": (((112, 64), 6),)}
HERO_AT6 = (128, 96)
# Dragon Quest V (extract.py: DQ5_PEOPLE): one person, the sailor beside the barrels of the ship's deck, in four runs;
# the boy has stopped outside the deckhouse's door.
SPOTS5 = {"U": (((176, 47), 0),), "R": (((176, 47), 2),), "D": (((176, 47), 4),), "L": (((176, 47), 6),)}
HERO_AT5 = (120, 103)
OBJ_SIZES = {0: (8, 16), 1: (8, 32), 2: (8, 64), 3: (16, 32), 4: (16, 64), 5: (32, 64)}


def buildn(lib_path, named, spots=SPOTS, hero_at=HERO_AT):
    figs, lacking = [], set()
    for item in named:
        name, path = item.split("=", 1)
        number, run, step = int(name[1:3], 16), name[3:-2], "ab".index(name[-1])
        d = Dump(path)
        small, big = OBJ_SIZES.get(d.regs.get("objSize", 0), (8, 16))
        spr = []
        for i in range(128):
            x, y, t, a = d.oam[i * 4:i * 4 + 4]
            hb = (d.hoam[i >> 2] >> ((i & 3) * 2)) & 3
            size = big if hb >> 1 else small
            if hb & 1:
                x -= 256
            if y >= 224 or x + size <= 0:
                continue                        # not on the screen
            if hero_at[0] <= x < hero_at[0] + 16 and hero_at[1] <= y < hero_at[1] + 24:
                continue                        # the hero
            spr.append((i, x, y, size, t | ((a & 1) << 8), (a >> 1) & 7, (a >> 6) & 1, a >> 7))
        for (sx, sy), pose in spots[run]:
            # the sprites of the one who stands there: those over the spot and those touching them
            mine = [s for s in spr if s[1] < sx + 16 and s[1] + s[3] > sx and s[2] < sy + 24 and s[2] + s[3] > sy]
            grew = bool(mine)
            while grew:
                grew = False
                for s in spr:
                    if s in mine or abs(s[1] + s[3] / 2 - sx - 8) > 40 or abs(s[2] + s[3] / 2 - sy - 12) > 40:
                        continue
                    if any(s[1] <= m[1] + m[3] and m[1] <= s[1] + s[3] and s[2] <= m[2] + m[3] and m[2] <= s[2] + s[3] for m in mine):
                        mine.append(s)
                        grew = True
            if not mine:
                lacking.add(number)
                continue
            # a picture as wide to the left of the spot's middle as to the right, down to the spot's foot
            ext = max(0, sx - min(s[1] for s in mine), max(s[1] + s[3] for s in mine) - sx - 16)
            left, w = sx - ext, 16 + 2 * ext
            top = min(sy, min(s[2] for s in mine))
            h = max(sy + 24, max(s[2] + s[3] for s in mine)) - top
            px = [0] * (w * h)
            for i, x, y, size, t, pal, hf, vf in sorted(mine, reverse=True):       # (the first in the list lies on top)
                for q in range((size // 8) ** 2):
                    qx, qy = q % (size // 8), q // (size // 8)
                    n = (t & 0x100) | ((t + qy * 16) & 0xF0) | ((t + qx) & 0x0F)
                    base = d.regs["objTileAdr1"] if n < 256 else d.regs["objTileAdr2"]
                    tp = d.tile_pixels(base, n & 255, 4)
                    for yy in range(8):
                        for xx in range(8):
                            v = tp[yy][xx]
                            if not v:
                                continue
                            u, r = qx * 8 + xx, qy * 8 + yy
                            if hf:
                                u = size - 1 - u
                            if vf:
                                r = size - 1 - r
                            if 0 <= x + u - left < w and 0 <= y + r - top < h:
                                px[(y + r - top) * w + x + u - left] = argb(d.cg[128 + pal * 16 + v])
            if not any(px):
                lacking.add(number)
                continue
            while h > 1 and not any(px[:w]):                                        # empty rows above it
                px, h = px[w:], h - 1
            while w > 2 and not any(px[k * w] or px[k * w + w - 1] for k in range(h)):   # and columns, both sides alike
                px = [p for k in range(h) for p in px[k * w + 1:k * w + w - 1]]
                w -= 2
            figs.append({"px": tuple(px), "w": w, "h": h, "from": name, "group": number, "pose": pose + step})
    figs.sort(key=lambda e: (e["group"], e["pose"]))
    with open(lib_path, "wb") as f:
        pickle.dump(figs, f)
    groups = sorted(set(e["group"] for e in figs))
    short = [g for g in groups if sum(1 for e in figs if e["group"] == g) < 8]
    print("%s: %d figures of %d characters from %d captures%s%s" % (
        lib_path, len(figs), len(groups), len(named),
        "; not all eight pictures of %s" % ", ".join("%02X" % g for g in short) if short else "",
        "; nothing drawn of %s" % ", ".join("%02X" % g for g in sorted(lacking - set(groups))) if lacking - set(groups) else ""))


def buildall(lib_path, paths):
    """one figure of everything the sprites draw in each capture (a creature alone on the screen, however many
    sprites it is made of): set 0, its pictures in the order given. "CAPTURE@X0,Y0,X1,Y1": only the sprites
    that lie inside that box of the screen (a creature that others stand before)"""
    figs = []
    for pose, path in enumerate(paths):
        path, at, box = path.rpartition("@") if "@" in os.path.basename(path) else (path, "", "")
        box = tuple(int(v) for v in box.split(",")) if at else (-256, 0, 512, 256)
        d = Dump(path)
        small, big = OBJ_SIZES.get(d.regs.get("objSize", 0), (8, 16))
        spr = []
        for i in range(128):
            x, y, t, a = d.oam[i * 4:i * 4 + 4]
            hb = (d.hoam[i >> 2] >> ((i & 3) * 2)) & 3
            size = big if hb >> 1 else small
            if hb & 1:
                x -= 256
            if y >= 224 or x + size <= 0:
                continue                        # not on the screen
            if x < box[0] or y < box[1] or x + size > box[2] or y + size > box[3]:
                continue                        # not in the box
            spr.append((i, x, y, size, t | ((a & 1) << 8), (a >> 1) & 7, (a >> 6) & 1, a >> 7))
        if not spr:
            raise SystemExit("%s: no sprite on the screen" % path)
        left, top = min(s[1] for s in spr), min(s[2] for s in spr)
        w, h = max(s[1] + s[3] for s in spr) - left, max(s[2] + s[3] for s in spr) - top
        px = [0] * (w * h)
        for i, x, y, size, t, pal, hf, vf in sorted(spr, reverse=True):       # (the first in the list lies on top)
            for q in range((size // 8) ** 2):
                qx, qy = q % (size // 8), q // (size // 8)
                n = (t & 0x100) | ((t + qy * 16) & 0xF0) | ((t + qx) & 0x0F)
                base = d.regs["objTileAdr1"] if n < 256 else d.regs["objTileAdr2"]
                tp = d.tile_pixels(base, n & 255, 4)
                for yy in range(8):
                    for xx in range(8):
                        v = tp[yy][xx]
                        if not v:
                            continue
                        u, r = qx * 8 + xx, qy * 8 + yy
                        if hf:
                            u = size - 1 - u
                        if vf:
                            r = size - 1 - r
                        if 0 <= x + u - left < w and 0 <= y + r - top < h:
                            px[(y + r - top) * w + x + u - left] = argb(d.cg[128 + pal * 16 + v])
        rows = [r for r in range(h) if any(px[r * w:(r + 1) * w])]
        cols = [c for c in range(w) if any(px[r * w + c] for r in range(h))]
        if not rows:
            raise SystemExit("%s: the sprites draw nothing" % path)
        px = [px[r * w + c] for r in range(rows[0], rows[-1] + 1) for c in range(cols[0], cols[-1] + 1)]
        figs.append({"px": tuple(px), "w": cols[-1] + 1 - cols[0], "h": rows[-1] + 1 - rows[0],
                     "from": os.path.basename(path), "group": 0, "pose": pose})
    with open(lib_path, "wb") as f:
        pickle.dump(figs, f)
    print("%s: %s" % (lib_path, ", ".join("%dx%d" % (e["w"], e["h"]) for e in figs)))


# The Super Famicom's picture starts one line below the scroll's row. Dragon Quest VI keeps a black frame of eight
# pixels round its picture, and writes the tiles a scrolling layer needs next under that frame: only what lies
# well inside it is the painting as it is shown.
SHOWN = (12, 12, 244, 212)


def buildscroll(lib_path, layer, width, rows, paths):
    """a painting that a background layer scrolls through, larger than the screen: what each capture shows of the
    layer is laid by the layer's scroll and folded to `width`, the painting's repeat. The scroll's registers hold
    one screen's worth, so the way travelled is added up from capture to capture (they must lie less than half a
    screen apart). Rows ROW0-ROW1 of it are the picture (the first capture's scroll counts as it stands): one
    figure, set 0. Every pixel of it must have been seen, and seen the same each time."""
    row0, _, row1 = rows.partition("-")
    row0, row1 = int(row0), int(row1)
    seen = {}
    last = None
    for path in paths:
        d = Dump(path)
        im = d.layer_image(layer)
        px = im.load()
        h, v = d.bg[layer]["hScroll"] % im.width, d.bg[layer]["vScroll"] % im.height
        if last is None:
            x0, y0 = h, v
        else:
            x0 += (h - last[0] + im.width // 2) % im.width - im.width // 2
            y0 += (v - last[1] + im.height // 2) % im.height - im.height // 2
        last = (h, v)
        for line in range(SHOWN[1], SHOWN[3]):
            y = y0 + 1 + line
            if not row0 <= y <= row1:
                continue
            for sx in range(SHOWN[0], SHOWN[2]):
                r, g, b, a = px[(h + sx) % im.width, (v + 1 + line) % im.height]
                c = (0xFF000000 | (r << 16) | (g << 8) | b) if a else 0
                k = (y - row0) * width + (x0 + sx) % width
                if seen.setdefault(k, c) != c:
                    raise SystemExit("%s shows the painting otherwise than a capture before it (other colours, a flash?)" % path)
    n = width * (row1 - row0 + 1)
    if len(seen) != n or not all(seen.values()):
        raise SystemExit("%s: %d of the painting's %d pixels were not seen" % (lib_path, n - sum(1 for c in seen.values() if c), n))
    figs = [{"px": tuple(seen[k] for k in range(n)), "w": width, "h": row1 - row0 + 1, "from": os.path.basename(paths[0]),
             "group": 0, "pose": 0}]
    with open(lib_path, "wb") as f:
        pickle.dump(figs, f)
    print("%s: %dx%d" % (lib_path, width, row1 - row0 + 1))


def buildwindow(lib_path, picture, box):
    """a window's frame and its two colours, cut from a picture of the screen (capture.c --ppm: these games set their
    text layer up for part of the frame only, so a capture's memory does not show a window as the screen does). BOX
    is the window on the picture: X0,Y0,X1,Y1 (the pixel past it), on the 8-pixel grid its tiles lie on. One figure
    of 24x24, set 0: the four corners and the four edges, 8x8 each, round a piece of the fill. Each edge must be
    one piece all along, and the whole of it two colours: the fill's and the line's."""
    from PIL import Image
    x0, y0, x1, y1 = (int(v) for v in box.split(","))
    px = Image.open(picture).convert("RGB").load()

    def piece(x, y):
        return tuple(0xFF000000 | (px[x + i, y + j][0] << 16) | (px[x + i, y + j][1] << 8) | px[x + i, y + j][2]
                     for j in range(8) for i in range(8))

    def edge(cells, name):
        got = set(piece(x, y) for x, y in cells)
        if len(got) != 1:
            raise SystemExit("%s: the %s edge of %s is not one piece all along (is that the window?)" % (picture, name, box))
        return got.pop()

    top = edge([(x, y0) for x in range(x0 + 8, x1 - 8, 8)], "top")
    bottom = edge([(x, y1 - 8) for x in range(x0 + 8, x1 - 8, 8)], "bottom")
    left = edge([(x0, y) for y in range(y0 + 8, y1 - 8, 8)], "left")
    right = edge([(x1 - 8, y) for y in range(y0 + 8, y1 - 8, 8)], "right")
    corner = piece(x0, y0)
    fill = corner[63]                   # (the corner's inmost pixel)
    rows = ((corner, top, piece(x1 - 8, y0)), (left, (fill,) * 64, right), (piece(x0, y1 - 8), bottom, piece(x1 - 8, y1 - 8)))
    out = tuple(rows[y // 8][x // 8][(y % 8) * 8 + x % 8] for y in range(24) for x in range(24))
    line = set(out) - {fill}
    if len(line) != 1:
        raise SystemExit("%s: the frame of %s has %d colours beside its fill's" % (picture, box, len(line)))
    figs = [{"px": out, "w": 24, "h": 24, "from": os.path.basename(picture), "group": 0, "pose": 0}]
    with open(lib_path, "wb") as f:
        pickle.dump(figs, f)
    print("%s: a window, its fill %06X, its line %06X" % (lib_path, fill & 0xFFFFFF, line.pop() & 0xFFFFFF))


def sheet(lib_path, out):
    from PIL import Image, ImageDraw
    figs = pickle.load(open(lib_path, "rb"))
    cols, size = 12, max(54, max(e["w"] for e in figs) * 3 + 6)
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
    elif len(a) >= 4 and a[1] == "buildn":
        buildn(a[2], a[3:])
    elif len(a) >= 4 and a[1] == "buildn6":
        buildn(a[2], a[3:], SPOTS6, HERO_AT6)
    elif len(a) >= 4 and a[1] == "buildn5":
        buildn(a[2], a[3:], SPOTS5, HERO_AT5)
    elif len(a) >= 4 and a[1] == "buildall":
        buildall(a[2], a[3:])
    elif len(a) >= 7 and a[1] == "buildscroll":
        buildscroll(a[2], int(a[3]) - 1, int(a[4]), a[5], a[6:])
    elif len(a) == 5 and a[1] == "buildwindow":
        buildwindow(a[2], a[3], a[4])
    elif len(a) == 4 and a[1] == "sheet":
        sheet(a[2], a[3])
    else:
        sys.stdout.write(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
