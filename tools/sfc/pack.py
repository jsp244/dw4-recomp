#!/usr/bin/env python3
"""tools/sfc/pack.py - build the 16-bit texture pack (sfc.pak) the game loads (runtime/sfc.c).

A map file says which Super Famicom art takes the place of which NES map square and figure:

    lib NAME FILE.sfclib          a library of Super Famicom squares (sfclib.py) under a short name
    nes NAME FILE.json            an inventory of NES squares (nesmap.py squares) under a short name
    figs NAME FILE.figs           a library of Super Famicom figures (sfcfig.py)
    people NAME FILE.json         an inventory of NES figures (nesmap.py figures)

    sq NES SFC [SFC...] [ticks N] [over SFC] [top SFC] [here] [hue A-B>C] [tint H:S] [gray P] [dark P]
                        [basegray P] [basetint H:S] [basedark P] [basehue A-B>C] [rows LIST] [cols LIST] [clear V]
                        [cutting SFC]
                                          the NES square NES drawn as the square SFC; several = animation
                                          frames, each lasting N game frames (default 16). "over X" lays X's
                                          upper layer on every frame (a tree's trunk on other ground); "top X"
                                          is what of it stands in the square above (a tree's crown: X's upper
                                          layer), drawn over that square and over people there; "here"
                                          binds the line to the place the NES square was listed in (one
                                          square may be a town's road and a castle's floor): such a line is
                                          looked up before the ones for any place. "hue 40-100>115" turns the
                                          colors between those hues to hue 115, "gray P" takes P percent of the
                                          color out and "dark P" P percent of the light (a negative P adds
                                          light; "P@A-B": only of the colors between hues A and B, so a
                                          brazier's stone and not its fire).
                                          With "over", these tone what is laid over and the top, and
                                          "basegray P" (and "basetint", "basedark", "basehue" likewise)
                                          the ground under it: for art that is the
                                          right thing in another place's colors (a tan castle's stone for a grey
                                          one's); "tint 25:55" gives every color hue 25 at 55 percent of
                                          full color, its light kept (grey boulders as a cave's brown rock).
                                          "rows 4,1-3,4,5-7,4,1-3,4,5-7" makes the square of those rows of
                                          SFC in that order (sixteen, counted from 0 at the top): for a wall the
                                          Super Famicom game has only with an edge drawn on it, where the NES
                                          game lays one square side by side and the bricks have to run on;
                                          "cols LIST" does the same with its columns (counted from the left).
                                          "over X cutting G" lays X without its own ground: X's pixels in
                                          the colors of the square G that are joined to the square's edge
                                          are clear (Dragon Quest I's tree, whose library has one layer,
                                          on Dragon Quest III's grass; the crown keeps its greens).
                                          "over @FIGS:gSET:POSE" lays the upper sixteen rows of a figure
                                          there (m before POSE mirrors it, +N after it sets it N pixels
                                          lower): the spectators the NES draws
                                          as map squares are Dragon Quest III's townspeople then.
                                          "over ~X clear 3" lays the NES square's own shapes over the ground
                                          in X's colors, all but those of its fourth color (0-3 as the NES
                                          palette orders them; "clear 0,3" for two; "0e": only where that
                                          color reaches the square's edge, so the black around a thing and
                                          not the black drawn inside it; "dots": and the specks of one or
                                          two pixels left over, the grain the NES draws on sand;
                                          "outline": and a dark line drawn around what is left): a
                                          signpost the Super Famicom games have no picture of then stands
                                          on their real grass
    fig NES SFC [dx DX] [dy DY]           the NES figure NES drawn as the figure SFC, its picture starting
                                          DX, DY pixels from the NES figure's corner (default: same feet)
    cast INV:DUMP:KIND LIB:gSET poses U,U,R,R,D,D,L,L [hue A-B>C] [colors FROM>TO[@A-B[/C-D]],...]
                                          the same for a set whose pictures are in no fixed order (Dragon Quest
                                          III's): the set's picture numbers for up, right, down and left, two
                                          steps each; "m" before a number mirrors it (m4 = picture 4 mirrored).
                                          DUMP "*": that kind in every dump of the inventory (a hero, who is
                                          the same figure wherever he goes)
    cast INV:DUMP:KIND LIB:gSET [hue A-B>C]   a whole character: the eight pictures (four facings, two steps) of
                                          kind KIND of the people inventory INV's dump DUMP drawn as set SET of
                                          the figure library LIB; "hue 200-260>320" turns the set's colors
                                          between those hues (degrees) to hue 320, for a character the Super
                                          Famicom games have only in other colors.
                                          "colors 5AB521>F0C828,CECECE>F8B050@0-10" gives every pixel of the
                                          first color (RRGGBB, as the figures' sheet shows it) the second:
                                          a figure's hair, dress and trim one by one, where a hero of this
                                          game wears other colors than the class that stands for him; "@0-10":
                                          only in those rows of the picture, counted from 0 at its top (a
                                          color the hair shares with the clothes); "@0-3/5-10": and only in
                                          those columns, from 0 at its left (a helmet's crest, not the tip
                                          of the spear beside it); a rule ending in "+": only pixels with
                                          one of the rules' old colors to their left or right (a cap's cloth,
                                          not a thin pole of the same white)

~SFC for SFC draws the NES square's own shapes in the colors of the square SFC (its darkest, its two middle
and its lightest color for the NES square's four, dark to light): for the few things the Super Famicom games
have no picture of, so that they sit in the scene's colors until they are redrawn.
A+B+C+D for SFC makes a square of four squares' quarters (the top left of A, the top right of B, the bottom
left of C, the bottom right of D), for the corners the Super Famicom games have no single square for.
=NES for SFC is the art the NES square NES is itself drawn with: the ground a thing stands on. Such a line is made
once for any kind of map and once for every kind in which that ground has art of its own ("here"), so a stool laid
over "=town:FLOOR" stands on the road in a town and on the castle's floor in a castle.
"nes" for SFC, with something laid over it, leaves the pixels the overlay does not cover to the NES: its own pixels
show there, in whatever color they have in that level ("nes over ~X clear 3": a statue's rim whose fourth color is
the sky on one level, a pool on another and black on a third).
A line of this sort written for one kind ("here") is made before those copies, wherever in the files it stands: it,
and not another line's copy for its kind, is what that kind gets (a dark castle's trees, toned for its night).
SFC.lo is a library square's ground layer alone, SFC.hi its upper layer alone (clear elsewhere); a library cut
from a game with one layer has neither.
NES is NAME:#NUMBER as the inventory's sheet numbers it or NAME:KEY (the square's 16-digit key); SFC is NAME:#NUMBER
(as the library's sheet numbers it) or NAME:HASH (the art's own 8-digit hash, which stays the same
when a library is built again from other captures: `pack.py freeze` rewrites a map file that way).

usage:
    pack.py build OUT.pak MAPFILE...
    pack.py freeze MAPFILE
The pack holds the games' art: it stays with your own copies (*.pak is git-ignored).
Each square's record also says how much of the NES square is drawn in each of its three colors: the game tints a
square when its palette holds other colors than the art was made for, and counts each color by that.
"""
import json
import os
import pickle
import struct
import sys


OVERWORLD_MIX = 0x9E3779B97F4A7C15      # into the keys of the overworld's squares (runtime/sfc.c)


def fnv(data, h=0xcbf29ce484222325):
    for b in data:
        h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
    return h


def art_hash(px):
    """8 hex digits naming a square's or figure's pixels"""
    return "%08x" % (fnv(b"".join(struct.pack("<I", p & 0xFFFFFFFF) for p in px)) & 0xFFFFFFFF)


NES_PIXEL = -1      # in a square's pixels: left to the NES (the pack holds a clear pixel there)


def solid(p):
    """a library pixel (a 15-bit Super Famicom color, or ARGB already) as ARGB; a clear one as black"""
    if p is None:
        return 0xFF000000
    return p if p > 0xFFFF else argb(p)


def argb(c):
    """a Super Famicom color (15 bits, blue high) as ARGB"""
    r, g, b = c & 31, (c >> 5) & 31, (c >> 10) & 31
    return 0xFF000000 | (((r << 3) | (r >> 2)) << 16) | (((g << 3) | (g >> 2)) << 8) | ((b << 3) | (b >> 2))


def turn_hue(p, lo, hi, to):
    """an ARGB pixel with its hue set to `to` if it lies between lo and hi (degrees) and it is not a grey"""
    import colorsys
    if not p >> 24:
        return p
    r, g, b = ((p >> 16) & 255) / 255.0, ((p >> 8) & 255) / 255.0, (p & 255) / 255.0
    h, s, v = colorsys.rgb_to_hsv(r, g, b)
    if s < 0.2 or not lo <= h * 360 <= hi:
        return p
    r, g, b = colorsys.hsv_to_rgb(to / 360.0, s, v)
    return 0xFF000000 | (int(r * 255 + 0.5) << 16) | (int(g * 255 + 0.5) << 8) | int(b * 255 + 0.5)


# how bright the NES's colors are (0-255), by color number: for ranking a square's four colors only
NES_LUMA = [102, 43, 36, 43, 52, 49, 42, 40, 44, 46, 49, 48, 48, 0, 0, 0,
            173, 98, 86, 93, 101, 97, 87, 85, 99, 102, 106, 105, 103, 0, 0, 0,
            254, 169, 158, 163, 170, 165, 160, 161, 171, 176, 181, 179, 177, 79, 0, 0,
            254, 220, 214, 216, 219, 217, 215, 216, 221, 224, 226, 225, 224, 184, 0, 0]


def recolored(nes, sfc_px, clear=()):
    """the NES square's pattern in four of the Super Famicom square's colors; the NES colors (0-3) named in `clear`
    stay clear ("0e": only its pixels joined to the square's edge), and the others take the square's colors between
    them"""
    dots, outline = "dots" in clear, "outline" in clear
    clear = [c for c in clear if c not in ("dots", "outline")]
    whole = tuple(int(c) for c in clear if not str(c).endswith("e"))
    edge = tuple(int(str(c)[:-1]) for c in clear if str(c).endswith("e"))
    def luma(c):
        if c > 0xFFFF:
            return ((c >> 16) & 255) * 3 + ((c >> 8) & 255) * 6 + (c & 255)
        return (c & 31) * 3 + ((c >> 5) & 31) * 6 + ((c >> 10) & 31)
    shades = sorted(set(p for p in sfc_px if p is not None), key=luma)
    order = sorted((v for v in range(4) if v not in whole), key=lambda v: NES_LUMA[nes["colors"][v] & 0x3F])  # dark to light
    last = max(1, len(order) - 1)
    to = {v: shades[(len(shades) - 1) * rank // last] for rank, v in enumerate(order)}
    for v in whole:
        to[v] = None
    val = [0] * 256
    for q, hexed in enumerate(nes["tiles"]):
        t = bytes.fromhex(hexed)
        for y in range(8):
            for x in range(8):
                val[((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x] = ((t[y] >> (7 - x)) & 1) | (((t[y + 8] >> (7 - x)) & 1) << 1)
    px = [to[v] for v in val]
    for c in edge:                      # from the edge inwards, through the pixels of that color
        todo = [i for i in range(256) if val[i] == c and (i < 16 or i >= 240 or i % 16 in (0, 15))]
        while todo:
            i = todo.pop()
            if px[i] is None:
                continue
            px[i] = None
            for j in (i - 16, i + 16, i - 1 if i % 16 else -1, i + 1 if i % 16 != 15 else -1):
                if 0 <= j < 256 and val[j] == c and px[j] is not None:
                    todo.append(j)
    if dots:                            # what is left in pieces of one or two pixels (touching, corners too) goes as well
        seen = set()
        for i in range(256):
            if px[i] is None or i in seen:
                continue
            piece, todo = [], [i]
            seen.add(i)
            while todo:
                k = todo.pop()
                piece.append(k)
                for dy in (-16, 0, 16):
                    for dx in (-1, 0, 1):
                        j = k + dy + dx
                        if 0 <= j < 256 and abs(j % 16 - k % 16) <= 1 and px[j] is not None and j not in seen:
                            seen.add(j)
                            todo.append(j)
            if len(piece) <= 2:
                for k in piece:
                    px[k] = None
    if outline:                         # the clear pixels beside what is left, in the darkest color
        beside = [i for i in range(256) if px[i] is None and any(
            0 <= j < 256 and abs(j % 16 - i % 16) <= 1 and px[j] is not None for j in (i - 16, i + 16, i - 1, i + 1))]
        for i in beside:
            px[i] = shades[0]
    return tuple(px)


def toned(p, extra):
    """an ARGB pixel after a line's hue / tint / gray / dark"""
    import colorsys
    if "hue" in extra:
        span, to = extra["hue"].split(">")
        lo, hi = (float(v) for v in span.split("-"))
        p = turn_hue(p, lo, hi, float(to))
    if "tint" in extra:
        hue, _, sat = extra["tint"].partition(":")
        r, g, b = ((p >> 16) & 255) / 255.0, ((p >> 8) & 255) / 255.0, (p & 255) / 255.0
        h, l, s = colorsys.rgb_to_hls(r, g, b)
        r, g, b = colorsys.hls_to_rgb(float(hue) / 360.0, l, float(sat or 50) / 100.0)
        p = 0xFF000000 | (int(r * 255 + 0.5) << 16) | (int(g * 255 + 0.5) << 8) | int(b * 255 + 0.5)
    if "gray" in extra or "dark" in extra:
        r, g, b = ((p >> 16) & 255) / 255.0, ((p >> 8) & 255) / 255.0, (p & 255) / 255.0
        h, l, s = colorsys.rgb_to_hls(r, g, b)

        def amount(key):
            """P of "P" or "P@A-B" (only the colors between hues A and B; greys always count)"""
            v, _, span = extra.get(key, "0").partition("@")
            if span and s > 0.12:
                lo, hi = (float(x) for x in span.split("-"))
                if not lo <= h * 360 <= hi:
                    return 0.0
            return float(v) / 100
        gray, dark = amount("gray"), amount("dark")
        r, g, b = colorsys.hls_to_rgb(h, min(1.0, l * (1 - dark)), s * (1 - gray))
        p = 0xFF000000 | (int(r * 255 + 0.5) << 16) | (int(g * 255 + 0.5) << 8) | int(b * 255 + 0.5)
    return p


def weights(nes):
    """how much of the NES square is drawn in its first, second and third color (0 to 15 each; 1 at least for a
    color it has at all): the game's tint counts them so (runtime/sfc.c)"""
    n = [0, 0, 0, 0]
    for hexed in nes["tiles"]:
        t = bytes.fromhex(hexed)
        for y in range(8):
            for x in range(8):
                n[((t[y] >> (7 - x)) & 1) | (((t[y + 8] >> (7 - x)) & 1) << 1)] += 1
    w = [0 if not c else max(1, (c * 15 + 128) // 256) for c in n[1:]]
    return w if any(w) else [1, 1, 1]


def recast(px, w, rules, where):
    """a figure's pixels with colors exchanged: "FROM>TO[@A-B[/C-D]],..." (RRGGBB; @A-B: only in those rows; /C-D:
    and only in those columns)"""
    def span(text):
        lo, _, hi = text.partition("-")
        return (int(lo), int(hi or lo)) if text else (0, 1 << 30)

    todo = []
    for rule in rules.split(","):
        beside = rule.endswith("+")
        pair, _, at = rule.rstrip("+").partition("@")
        old, _, new = pair.partition(">")
        rows, _, cols = at.partition("/")
        try:
            todo.append((int(old, 16), int(new, 16)) + span(rows) + span(cols) + (beside,))
        except ValueError:
            raise SystemExit("%s: colors wants FROM>TO[@A-B[/C-D]][+],... in RRGGBB, not %r" % (where, rule))
    olds = set(t[0] for t in todo)

    def is_old(j, row):
        return 0 <= j < len(px) and j // w == row and (px[j] >> 24) & 255 and px[j] & 0xFFFFFF in olds

    out = list(px)
    for i, p in enumerate(px):
        if not (p >> 24) & 255:
            continue
        for old, new, r0, r1, c0, c1, beside in todo:
            if p & 0xFFFFFF == old and r0 <= i // w <= r1 and c0 <= i % w <= c1 and (not beside or is_old(i - 1, i // w) or is_old(i + 1, i // w)):
                out[i] = (p & 0xFF000000) | new
                break
    return tuple(out)


def row_list(text, where):
    """the sixteen row numbers of "4,1-3,4,5-7,..." """
    order = []
    for part in text.split(","):
        a, _, b = part.partition("-")
        order += list(range(int(a), int(b or a) + 1))
    if len(order) != 16 or not all(0 <= r < 16 for r in order):
        raise SystemExit("%s: rows takes sixteen rows, each 0-15 (%r gives %d)" % (where, text, len(order)))
    return order


class Maps:
    def __init__(self, paths):
        self.libs, self.nes, self.figs, self.people = {}, {}, {}, {}
        self.files, self.used = {}, set()       # library file -> its entries; the entries a line has taken (supplement.py)
        self.sq, self.fig, self.cast = [], [], []
        self.lines = []
        for path in paths:
            base = os.path.dirname(os.path.abspath(path))
            with open(path, encoding="utf-8") as f:
                for n, raw in enumerate(f, 1):
                    self.lines.append((path, raw))
                    t = raw.split("#!", 1)[0]
                    t = t.split(" # ", 1)[0].split()
                    if not t or t[0].startswith("#"):
                        continue
                    where = "%s:%d" % (path, n)
                    if t[0] == "lib":
                        self.libs[t[1]] = self.library(os.path.join(base, t[2]))
                    elif t[0] == "nes":
                        self.nes[t[1]] = self.inventory(os.path.join(base, t[2]))
                    elif t[0] == "figs":
                        self.figs[t[1]] = self.library(os.path.join(base, t[2]))
                    elif t[0] == "people":
                        self.people[t[1]] = self.inventory(os.path.join(base, t[2]))
                    elif t[0] == "sq":
                        ticks, refs, i, extra = 16, [], 2, {}
                        while i < len(t):
                            if t[i] == "ticks":
                                ticks = int(t[i + 1]); i += 2
                            elif t[i] in ("over", "top", "hue", "tint", "gray", "dark", "basegray", "basetint", "basedark",
                                          "basehue", "rows", "cols", "clear", "cutting"):
                                extra[t[i]] = t[i + 1]; i += 2
                            elif t[i] == "here":
                                extra["here"] = True; i += 1
                            else:
                                refs.append(t[i]); i += 1
                        self.sq.append((where, t[1], refs, ticks, extra))
                    elif t[0] == "cast":
                        opt = {}
                        i = 3
                        while i < len(t):
                            opt[t[i]] = t[i + 1]; i += 2
                        self.cast.append((where, t[1], t[2], opt))
                    elif t[0] == "fig":
                        opt = {"dx": None, "dy": None}
                        i = 3
                        while i < len(t):
                            opt[t[i]] = int(t[i + 1]); i += 2
                        self.fig.append((where, t[1], t[2], opt))
                    else:
                        raise SystemExit("%s: what is %r?" % (where, t[0]))

    @staticmethod
    def inventory(path):
        """a NES inventory; none where the file is missing (the lines that name it are left out)"""
        return json.load(open(path)) if os.path.exists(path) else []

    def library(self, path):
        """a library file's entries (read once, whichever map files name it)"""
        path = os.path.abspath(path)
        if path not in self.files:
            self.files[path] = pickle.load(open(path, "rb"))
            self.by_hash(self.files[path])
        return self.files[path]

    @staticmethod
    def by_hash(lib):
        for e in lib:
            if "hash" not in e:
                e["hash"] = art_hash(e["px"])

    def nes_entry(self, ref, table, where):
        if ":#" in ref:
            name, num = ref.split(":#")
            return table[name][int(num)] if int(num) < len(table.get(name, [])) else None
        if ":" in ref:
            name, key = ref.split(":", 1)
            for e in table.get(name, []):
                if e["key"].lower() == key.lower():
                    return e
            return None
        for inv in table.values():
            for e in inv:
                if e["key"].lower() == ref.lower():
                    return e
        return None

    def art(self, ref, table, where):
        if "+" in ref:                  # four squares: the quarter of each that lies where it is named
            parts = [self.art(r, table, where) for r in ref.split("+")]
            if len(parts) != 4:
                raise SystemExit("%s: a square of quarters takes four (top left+top right+bottom left+bottom right)" % where)
            px = [0] * 256
            for q, p in enumerate(parts):
                for y in range(8):
                    for x in range(8):
                        i = ((q >> 1) * 8 + y) * 16 + (q & 1) * 8 + x
                        px[i] = p["px"][i]
            return {"px": tuple(px), "hash": "+".join(p["hash"] for p in parts)}
        name, _, what = ref.partition(":")
        if name not in table:
            raise SystemExit("%s: no library %r" % (where, name))
        what, _, layer = what.partition(".")
        found = None
        if what.startswith("#"):
            found = table[name][int(what[1:])]
        else:
            for e in table[name]:
                if e["hash"] == what.lower():
                    found = e
                    break
        if found is None:
            raise SystemExit("%s: library %s has no art %s" % (where, name, what))
        self.used.add(id(found))
        if layer:
            if layer not in found:
                raise SystemExit("%s: %s has no layer %r" % (where, ref, layer))
            return {"px": found[layer], "hash": found["hash"] + "." + layer}
        return found


def build(out, paths, quiet=False):
    m = Maps(paths)
    squares, figures = {}, []
    skipped = 0

    def made(e, frames, ticks, extra, where):
        """the pack's square for the NES square e: the frames after the line's rows, tones, "over" and "top" """
        tone = "hue" in extra or "tint" in extra or "gray" in extra or "dark" in extra
        if "rows" in extra:
            order = row_list(extra["rows"], where)
            frames = [tuple(px[r * 16 + x] for r in order for x in range(16)) for px in frames]
        if "cols" in extra:
            order = row_list(extra["cols"], where)
            frames = [tuple(px[y * 16 + c] for y in range(16) for c in order) for px in frames]
        base = {k[4:]: v for k, v in extra.items() if k.startswith("base")}
        if base:
            frames = [tuple(p if p == NES_PIXEL else toned(solid(p), base) for p in px) for px in frames]
        if "over" in extra:             # (with something laid over, the tones are its own: the ground keeps its colors)
            if extra["over"].startswith("@"):
                lib, _, rest = extra["over"][1:].partition(":g")
                group, _, pose = rest.partition(":")
                pose, _, lower = pose.partition("+")
                found = [x for x in m.figs.get(lib, []) if x["group"] == int(group) and x["pose"] == int(pose.lstrip("m"))]
                if not found:
                    raise SystemExit("%s: no figure %s" % (where, extra["over"]))
                m.used.add(id(found[0]))
                w, h, fp = found[0]["w"], found[0]["h"], found[0]["px"]
                over = [None] * 256
                for y in range(int(lower or 0), 16):
                    for x in range(min(16, w)):
                        p = fp[(y - int(lower or 0)) * w + (w - 1 - x if pose.startswith("m") else x)]
                        if (p >> 24) & 255:
                            over[y * 16 + x + (16 - min(16, w)) // 2] = p
                over = tuple(over)
            elif extra["over"].startswith("~"):
                over = recolored(e, m.art(extra["over"][1:], m.libs, where)["px"],
                                 tuple(v for v in extra.get("clear", "").split(",") if v))
            else:
                over = m.art(extra["over"], m.libs, where)["px"]
                if "cutting" in extra:      # its own ground goes: from the edge inwards, through that ground's colors
                    ground = set(m.art(extra["cutting"], m.libs, where)["px"])
                    over = list(over)
                    todo = [i for i in range(256) if over[i] in ground and (i < 16 or i >= 240 or i % 16 in (0, 15))]
                    while todo:
                        i = todo.pop()
                        if over[i] is None:
                            continue
                        over[i] = None
                        for j in (i - 16, i + 16, i - 1 if i % 16 else -1, i + 1 if i % 16 != 15 else -1):
                            if 0 <= j < 256 and over[j] is not None and over[j] in ground:
                                todo.append(j)
                    over = tuple(over)
            if tone:
                over = tuple(None if o is None else toned(solid(o), extra) for o in over)
            frames = [tuple(o if o is not None else p for p, o in zip(px, over)) for px in frames]
        elif tone:
            frames = [tuple(p if p == NES_PIXEL else toned(solid(p), extra) for p in px) for px in frames]
        top = m.art(extra["top"], m.libs, where)["px"] if "top" in extra else None
        if top and tone:
            top = tuple(None if p is None else toned(solid(p), extra) for p in top)
        return (bytes(e["colors"]), frames, ticks, top, weights(e))

    on_ground = []
    for where, nes, refs, ticks, extra in m.sq:
        e = m.nes_entry(nes, m.nes, where)
        if e is None:
            skipped += 1
            continue
        if refs and refs[0].startswith("="):
            on_ground.append((where, nes, refs, ticks, extra, e))
            continue
        frames = [(NES_PIXEL,) * 256 if r == "nes" else
                  recolored(e, m.art(r[1:], m.libs, where)["px"]) if r.startswith("~") else m.art(r, m.libs, where)["px"] for r in refs]
        key = int(e["key"], 16) ^ (OVERWORLD_MIX if e.get("scene") else 0) ^ (int(e["map"], 16) if extra.get("here") else 0)
        if key in squares:
            print("%s: square %s given again (the first stays)" % (where, nes))
            continue
        squares[key] = made(e, frames, ticks, extra, where)
    # a thing on another square's ground ("=NES"): for any kind of map, and for every kind that ground has art of its own for
    kinds = sorted(set(int(x["map"], 16) for inv in m.nes.values() for x in inv if "map" in x))
    # (a line written for one kind, "here", comes before the copies the other lines get for every kind: it is the
    # one meant for that kind, wherever in the files it stands)
    for where, nes, refs, ticks, extra, e in sorted(on_ground, key=lambda t: not t[4].get("here")):
        g = m.nes_entry(refs[0][1:], m.nes, where)
        if g is None:
            skipped += 1
            continue
        ground = int(g["key"], 16) ^ (OVERWORLD_MIX if g.get("scene") else 0)
        key = int(e["key"], 16) ^ (OVERWORLD_MIX if e.get("scene") else 0)
        for kind in ([int(e["map"], 16)] if extra.get("here") else [0] + kinds):
            under = squares.get(ground ^ kind) or (squares.get(ground) if extra.get("here") else None)
            if under is None:
                if not kind:
                    raise SystemExit("%s: the ground %s has no art yet" % (where, refs[0][1:]))
                continue
            if (key ^ kind) not in squares:
                squares[key ^ kind] = made(e, list(under[1]), ticks, extra, where)
    for where, nes, ref, opt in m.fig:
        e = m.nes_entry(nes, m.people, where)
        if e is None:
            skipped += 1
            continue
        a = m.art(ref, m.figs, where)
        dx = opt["dx"] if opt["dx"] is not None else (16 - a["w"]) // 2
        dy = opt["dy"] if opt["dy"] is not None else 16 - a["h"]
        figures.append((int(e["key"], 16), bytes(e["colors"]), dx, dy, a["w"], a["h"], a["px"]))
    # NES facings: up, right, down, left; the Super Famicom sets: up, down, left, right (two steps each)
    POSE = [0, 1, 6, 7, 2, 3, 4, 5]
    done = set((f[0], f[1]) for f in figures)
    for where, nes, ref, opt in m.cast:
        inv, dump, kind = nes.split(":")
        lib, _, group = ref.partition(":g")
        if inv not in m.people or lib not in m.figs:
            raise SystemExit("%s: no inventory %r or no library %r" % (where, inv, lib))
        poses = {e["pose"]: e for e in m.figs[lib] if e["group"] == int(group)}
        m.used.update(id(e) for e in poses.values())
        who = [e for e in m.people[inv] if dump in ("*", e["dump"]) and e["kind"] == int(kind)]
        if not poses:
            raise SystemExit("%s: the library has no set %s" % (where, group))
        if not who:
            skipped += 1
            continue
        named = opt["poses"].split(",") if "poses" in opt else None
        for e in who:
            if e.get("turn"):
                continue                # someone lying down, turned: made below from the upright picture
            pose = 4 if "turn" in e else e["facing"] * 2 + e["step"]       # (lying down: the upright picture is the front view)
            mirror = False
            if named:
                want = named[pose]
                mirror = want.startswith("m")
                a = poses.get(int(want.lstrip("m")))
            else:
                want = POSE[pose]
                a = poses.get(want) or poses.get(want & ~1) or poses.get(want | 1)
            if a is None:
                continue
            key, ref_colors = int(e["key"], 16), bytes(e["colors"])
            if (key, ref_colors) in done:
                continue
            done.add((key, ref_colors))
            px = a["px"]
            if mirror:
                w = a["w"]
                px = tuple(px[(i // w) * w + (w - 1 - i % w)] for i in range(len(px)))
            if "hue" in opt:
                span, to = opt["hue"].split(">")
                lo, hi = (float(v) for v in span.split("-"))
                px = tuple(turn_hue(p, lo, hi, float(to)) for p in px)
            if "colors" in opt:
                px = recast(px, a["w"], opt["colors"], where)
            figures.append((key, ref_colors, (16 - a["w"]) // 2, 16 - a["h"], a["w"], a["h"], px))
    # People lying down (a sleeper in his bed, the wounded soldier of the tower, the bones in the mine): the NES has
    # no pictures of its own for them, it turns the kind's upright picture, its front view standing, a quarter
    # clockwise, a half, three quarters (nesmap.py marks those poses: "turn", "of"). The pack does as the NES does:
    # the figure it draws for the upright picture, turned the same way about the middle of the NES figure's square
    # (a figure taller than the square reaches past it at its head's end, as it does standing).
    standing = {}
    for fig in figures:
        standing.setdefault((fig[0], fig[1]), fig)
        standing.setdefault(fig[0], fig)
    for inv in m.people.values():
        for e in inv:
            if not e.get("turn"):
                continue
            key, ref_colors = int(e["key"], 16), bytes(e["colors"])
            fig = standing.get((int(e["of"], 16), ref_colors)) or standing.get(int(e["of"], 16))
            if fig is None or (key, ref_colors) in done:
                continue
            done.add((key, ref_colors))
            _, _, dx, dy, w, h, px = fig
            for _ in range(e["turn"]):          # a quarter clockwise: the top goes to the right
                px = tuple(px[(h - 1 - x) * w + y] for y in range(w) for x in range(h))
                dx, dy, w, h = 16 - (dy + h), dx, h, w
            figures.append((key, ref_colors, dx, dy, w, h, px))
    with open(out, "wb") as f:
        f.write(b"DW4S" + struct.pack("<III", 2, len(squares), len(figures)))
        for key, (ref, frames, ticks, top, w) in sorted(squares.items()):
            f.write(struct.pack("<Q4sBBBB", key, ref, len(frames), max(1, min(255, ticks)), (1 if top else 0) | (w[2] << 4),
                                w[0] | (w[1] << 4)))
            for px in frames:
                f.write(struct.pack("<256I", *[0 if p == NES_PIXEL else solid(p) for p in px]))
            if top:
                f.write(struct.pack("<256I", *[0 if p is None else solid(p) for p in top]))
        for key, ref, dx, dy, w, h, px in figures:
            f.write(struct.pack("<Q12sbbBB", key, ref, dx, dy, w, h))
            f.write(struct.pack("<%dI" % (w * h), *px))
    if not quiet:
        print("%s: %d map squares, %d figures%s" % (out, len(squares), len(figures),
              "; %d lines left out (their NES squares or people are in no inventory given)" % skipped if skipped else ""))
    return m


def freeze(path):
    m = Maps([path])
    out = []
    for _, raw in m.lines:
        t = raw.split()
        if t and t[0] in ("sq", "fig"):
            words = raw.rstrip("\n").split(" ")
            for i, w in enumerate(words):
                if w == "#":
                    break               # the rest is a comment
                parts = w.split("+")
                for j, part in enumerate(parts):
                    if ":#" not in part:
                        continue
                    name, num = part.split(":#")
                    num, dot, layer = num.partition(".")
                    if not num.isdigit():
                        continue
                    lead = "~" if name.startswith("~") else ""
                    name = name.lstrip("~")
                    if i == 1 and name in m.nes and int(num) < len(m.nes[name]):
                        parts[j] = "%s:%s" % (name, m.nes[name][int(num)]["key"])
                    elif name in m.libs or name in m.figs:
                        parts[j] = "%s%s:%s%s%s" % (lead, name, (m.libs.get(name) or m.figs[name])[int(num)]["hash"], dot, layer)
                words[i] = "+".join(parts)
            raw = " ".join(words) + "\n"
        out.append(raw)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.writelines(out)
    print("%s: frozen" % path)


def main():
    a = sys.argv
    if len(a) >= 4 and a[1] == "build":
        build(a[2], a[3:])
    elif len(a) == 3 and a[1] == "freeze":
        freeze(a[2])
    else:
        sys.stdout.write(__doc__)
        sys.exit(2)


if __name__ == "__main__":
    main()
