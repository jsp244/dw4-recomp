#!/usr/bin/env python3
"""tools/remaster/remaster_art.py - make the remaster pack (remaster.pak) from art painted over frames.

The remastered textures (runtime/remaster.c) color every pixel with an "ink" its source tile gives
it: one of the tile's four palette colors, 1-7 steps on its ramp (4 = the color itself). Art is
painted over a frame of the game: `nes-headless ... --hd-dump frame.hd` records, for every pixel of
the last frame, which tile (and which pixel of it) each layer drew there, and the tiles themselves:
the game keeps them in CHR RAM, so a tile is known by its 16 bytes, wherever it is loaded. An art
file names such a dump, a layer and a rectangle of the screen, and holds one ink per pixel; the
packer maps every painted pixel back to its tile, so whole trees, walls or characters are painted
where they appear, tiles and all.

Art file (text; '#' starts a comment):
    dump frame.hd          the dump the rectangle refers to (relative to the art file)
    layer bg | spr         which layer's tiles the inks go to
    rect X Y W H           the rectangle on the 256x240 screen
    grid                   then H lines of W cells: a color (0-3) and a step (1-7, or a-f for
                           the half steps 1.5-6.5), e.g. 24 or 2d; '..' leaves the pixel to the
                           automatic shading
A tile painted in several places takes the last painting of each pixel.

Tile art (for textures that repeat, like grass or water): no dump or rect, but
    layer bg | spr
    tile HASH              the tile's hash (16 hex digits, as `tiles` lists them), then 8 lines
    ...                    of 8 cells; as many tiles as wanted

usage:
    remaster_art.py template DUMP LAYER X Y W H [OUT.art]   an art file with the frame's own colors
    remaster_art.py pack OUT.pak ART...                     the pack from art files
    remaster_art.py tiles DUMP                              the hashes of the tiles a frame shows
The art is drawn over the cartridge's own tiles, so keep it (and the pack) with your own copy of
the game, not in a public place.
"""
import os
import struct
import sys

PRESENT = 1 << 27


def tile_hash(tile):
    """64-bit FNV-1a of a tile's 16 bytes, as runtime/remaster.c keys the pack"""
    h = 0xcbf29ce484222325
    for b in tile:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h or 1


def read_dump(path):
    with open(path, "rb") as f:
        d = f.read()
    if d[:4] != b"DWHD" or d[4] != 1:
        raise SystemExit("%s: not a pixel-source dump" % path)
    o = 8
    # the frame's scroll: the screen's top left corner in the nametables
    t, fx = d[o] | (d[o + 1] << 8), d[o + 2]
    scroll = (((t >> 10) & 1) * 256 + (t & 0x1F) * 8 + fx, ((t >> 11) & 1) * 240 + ((t >> 5) & 0x1F) * 8 + (t >> 12))
    o += 4
    pal = d[o:o + 32]; o += 32
    emph = d[o:o + 240]; o += 240
    fb = d[o:o + 256 * 240]; o += 256 * 240
    bg = struct.unpack_from("<%dI" % (256 * 240), d, o); o += 4 * 256 * 240
    spr = struct.unpack_from("<%dI" % (256 * 240), d, o); o += 4 * 256 * 240
    # the palettes the frame's lines were drawn with (bits 28-29 of a source pick one)
    n = d[o]
    palsets = [d[o + 4 + 32 * k:o + 4 + 32 * (k + 1)] for k in range(max(1, n))]
    o += 4 + 4 * 32
    chr_ram = d[o:o + 8192]
    if len(chr_ram) != 8192:
        raise SystemExit("%s: no CHR RAM in the dump" % path)
    hashes = [tile_hash(chr_ram[k * 16:k * 16 + 16]) for k in range(512)]   # slot -> the tile it held
    return {"pal": pal, "emph": emph, "fb": fb, "bg": bg, "spr": spr, "scroll": scroll, "palsets": palsets,
            "chr": chr_ram, "hashes": hashes}


def source(info):
    """(slot, row, col, value) of a packed source, or None"""
    if not info & PRESENT:
        return None
    return info & 0x3FFF, (info >> 14) & 7, (info >> 17) & 7, (info >> 22) & 3


def template(dump_path, layer, x, y, w, h, out=None):
    d = read_dump(dump_path)
    src = d["bg"] if layer == "bg" else d["spr"]
    lines = ["# painted over %s, %s layer; each cell = color (0-3) + step (1-7, 4 = the color itself)" % (os.path.basename(dump_path), layer),
             "dump %s" % os.path.basename(dump_path), "layer %s" % layer, "rect %d %d %d %d" % (x, y, w, h), "grid"]
    for j in range(h):
        cells = []
        for i in range(w):
            s = source(src[(y + j) * 256 + x + i]) if 0 <= x + i < 256 and 0 <= y + j < 240 else None
            if s is None or (layer == "spr" and s[3] == 0):
                cells.append("..")
            else:
                cells.append("%d4" % s[3])
        lines.append(" ".join(cells))
    text = "\n".join(lines) + "\n"
    if out:
        with open(out, "w", encoding="utf-8") as f:
            f.write(text)
    else:
        sys.stdout.write(text)


def read_tile_art(path):
    """[(tile, layer, 8x8 cells)] of a tile art file"""
    layer, out, cur = None, [], None
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            key, _, rest = line.partition(" ")
            if key == "layer":
                layer = rest.strip()
            elif key == "tile":
                cur = (int(rest.strip(), 16), layer, [])
                out.append(cur)
            elif cur is not None:
                cur[2].append(line.split())
    for tile, lay, rows in out:
        if lay not in ("bg", "spr") or len(rows) != 8 or any(len(r) != 8 for r in rows):
            raise SystemExit("%s: tile %016X needs a layer and 8 lines of 8 cells" % (path, tile))
    return out


def ink_of(cell, path):
    """a cell -> its ink byte (runtime/remaster.h): color, step, half step"""
    if len(cell) != 2 or cell[0] not in "0123" or cell[1] not in "1234567abcdef":
        raise SystemExit("%s: bad cell %r" % (path, cell))
    if cell[1] in "abcdef":
        return int(cell[0]) | ((ord(cell[1]) - ord("a") + 1) << 2) | 0x20
    return int(cell[0]) | (int(cell[1]) << 2)


def is_tile_art(path):
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if line.startswith("tile "):
                return True
            if line.startswith("dump ") or line.startswith("rect "):
                return False
    return False


def read_art(path):
    head, grid = {}, []
    in_grid = False
    with open(path, encoding="utf-8") as f:
        for raw in f:
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            if in_grid:
                grid.append(line.split())
                continue
            key, _, rest = line.partition(" ")
            if key == "grid":
                in_grid = True
            else:
                head[key] = rest.strip()
    for k in ("dump", "layer", "rect"):
        if k not in head:
            raise SystemExit("%s: no '%s' line" % (path, k))
    x, y, w, h = (int(v) for v in head["rect"].split())
    if len(grid) != h or any(len(r) != w for r in grid):
        raise SystemExit("%s: the grid is not %d lines of %d cells" % (path, h, w))
    return os.path.join(os.path.dirname(path), head["dump"]), head["layer"], x, y, w, h, grid


def pack(out, arts):
    tiles = {}   # (tile, layer) -> 64 inks
    conflicts = 0
    dumps = {}
    for path in arts:
        if is_tile_art(path):
            for tile, layer, rows in read_tile_art(path):
                inks = tiles.setdefault((tile, layer), [0] * 64)
                for r in range(8):
                    for c in range(8):
                        if rows[r][c] != "..":
                            inks[r * 8 + c] = ink_of(rows[r][c], path)
            continue
        dump_path, layer, x, y, w, h, grid = read_art(path)
        if layer not in ("bg", "spr"):
            raise SystemExit("%s: layer must be bg or spr" % path)
        if dump_path not in dumps:
            dumps[dump_path] = read_dump(dump_path)
        src = dumps[dump_path]["bg" if layer == "bg" else "spr"]
        for j in range(h):
            for i in range(w):
                cell = grid[j][i]
                if cell == "..":
                    continue
                ink = ink_of(cell, path)
                sx, sy = x + i, y + j
                if not (0 <= sx < 256 and 0 <= sy < 240):
                    continue
                s = source(src[sy * 256 + sx])
                if s is None or s[0] >= 512 or (layer == "spr" and s[3] == 0):
                    continue
                slot, row, col, _ = s
                tile = dumps[dump_path]["hashes"][slot]
                inks = tiles.setdefault((tile, layer), [0] * 64)
                if inks[row * 8 + col] and inks[row * 8 + col] != ink:
                    conflicts += 1
                inks[row * 8 + col] = ink
    with open(out, "wb") as f:
        f.write(b"DW4R" + struct.pack("<II", 1, len(tiles)))
        for (tile, layer), inks in sorted(tiles.items()):
            f.write(struct.pack("<QB", tile, 0 if layer == "bg" else 1) + bytes(inks))
    print("%s: %d tiles from %d art files%s" % (out, len(tiles), len(arts),
          ", %d pixels painted differently in two places (the last one kept)" % conflicts if conflicts else ""))


def list_tiles(dump_path):
    """the tiles a frame shows: hash, layer, CHR RAM slot and how many pixels"""
    d = read_dump(dump_path)
    for layer in ("bg", "spr"):
        seen = {}
        for info in d[layer]:
            s = source(info)
            if s is not None and s[0] < 512 and not (layer == "spr" and s[3] == 0):
                seen[s[0]] = seen.get(s[0], 0) + 1
        for slot, n in sorted(seen.items()):
            print("%016X %s slot %3d  %5d pixels" % (d["hashes"][slot], layer, slot, n))


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "tiles":
        list_tiles(sys.argv[2])
    elif len(sys.argv) >= 8 and sys.argv[1] == "template":
        _, _, dump, layer, x, y, w, h = sys.argv[:8]
        template(dump, layer, int(x), int(y), int(w), int(h), sys.argv[8] if len(sys.argv) > 8 else None)
    elif len(sys.argv) >= 4 and sys.argv[1] == "pack":
        pack(sys.argv[2], sys.argv[3:])
    else:
        sys.stdout.write(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
