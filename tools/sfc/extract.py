#!/usr/bin/env python3
"""tools/sfc/extract.py - make the SNES Remaster's art pack (sfc.pak) from your own cartridges.

    python tools/sfc/extract.py --dq12 "Dragon Quest I & II (Japan).sfc" --dq3 "Dragon Quest III (Japan).sfc"
                                [--nes "Dragon Warrior IV (USA).nes"] [--uncensor HACK.ips] [--out sfc.pak]
                                [--keep-captures]

What it does, step by step (everything it writes is under generated/sfc, which git ignores, and the pack):
 1. runs both Super Famicom games without a window (build/sfc-capture: `make sfc-capture`), pressing the
    buttons the scripts in tools/sfc/scenes name, and saves the machine's memory at the frames they list;
 2. cuts those captures into libraries: the 16x16 map squares (sfclib.py) and the people (sfcfig.py); Dragon
    Quest III's overworld squares are read straight from its ROM;
 3. runs Dragon Warrior IV (build/nes-headless-recomp) to the maps the textures cover and lists their squares
    and people (nesmap.py), also with the "Graphical Uncensoring" hack if its file is given: it changes a few.
    A short script plays to Burland; the chapter's other maps are loaded from there without playing to them
    (--poke names the place to the game as it changes maps);
 4. builds the pack from the map files in tools/sfc/maps (pack.py): which art takes whose place.
The same ROMs give the same pack every time. The art is the games': keep the pack with your own copies.
"""
import argparse
import glob
import hashlib
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
EXE = ".exe" if os.name == "nt" else ""
KNOWN = {"dq12": "1c0c6d78bf2bc29160adf48b17ebf5a5bc46230e", "dq3": "b711341e4b1022d7645c5313840874aebc01b7b7"}

# the captures each library is cut from (names from scenes/dq12.captures)
DQ1_CASTLE = ["dq1_throne1", "dq1_throne2", "dq1_castle0", "dq1_castle1", "dq1_castle2", "dq1_castle3", "dq1_castle4", "dq1_x5"] + \
             ["w1_%03d" % i for i in list(range(0, 33)) + [38]]
DQ1_OVERWORLD = ["w1_%03d" % i for i in (34, 35, 36, 40, 41, 42, 43, 44)]
# Dragon Quest III (names from scenes/dq3.captures and dq3_front.captures; the order is the libraries' order, which
# the map files' figure sets count by). Its script plays the game from power-on through the opening's questions and
# last scene to Aliahan, the king's audience and three long walks through the town: about 91,000 frames.
# scenes/dq3_cave goes on from the town: out of its south gate, over the bridge to the cape, into the cave there
# (captures v_*: its rock, its floor, its stairs and its treasure chests), up the stairs at the cave's east end into
# the passages under the tower on the island (w_*) and up into the tower's ground floor (x_*: its stone floors
# and wall tops, its statues, pillars and torches): about 69,000 frames from power-on. A hero alone at the start of
# the game does not live through that walk's battles, so its .holds file keeps his hit points up (sfc-capture
# --holds) and the battles are fought by pressing A; from the cave's east end on it also keeps the game's count of
# steps to the next battle up, and there are none.
DQ3_SKIP = {"c_000", "c_047", "h_015"}          # (pictures between two maps: black)


def dq3_sets(names):
    """the captures each Dragon Quest III library is cut from"""
    def of(prefix):
        return sorted(n for n in names if n.startswith(prefix) and n not in DQ3_SKIP)
    h = of("h_")
    town = of("t1_") + of("t2_") + of("t3_") + [n for n in h if "h_016" <= n <= "h_029"]
    castle = of("c_") + of("k_")
    front = ["f2", "f3", "f4", "c_002", "c_003"]
    house = [n for n in h if n <= "h_014"]
    figures = of("t1_") + of("t2_") + of("t3_") + h + castle + ["f2", "f3", "f4"]
    return town, castle, front, house, figures


# Dragon Warrior IV: the frame of scenes/dw4_chapter1.input at which the player stands on each map
NES_MAPS = [("castle", 6542), ("town", 6884), ("overworld", 8066)]
# and the chapter's other maps, by the game's own numbers: (name, place, level). At frame 6585 of that script the
# player is on the stairs of the castle's upper floor, Down held; 5 to 21 frames on the game reads the place ($63)
# and the level ($64) it is to load, and 115 frames on that map is there.
NES_WARPS = [("burland2", 0x02, 2), ("burland3", 0x02, 3), ("izmit0", 0x12, 0), ("izmit1", 0x12, 1), ("izmit2", 0x12, 2),
             ("tunnel0", 0x39, 0)] + [("well%d" % i, 0x36, i) for i in range(7)] + [("tower%d" % i, 0x41, i) for i in range(6)]
# their inventories, one for every kind of map (the game's number at $28: art is bound to the kind, pack.py "here")
NES_KINDS = [("castle_up", ["burland2"]), ("rooms", ["burland3", "izmit1", "izmit2"]), ("izmit", ["izmit0"]),
             ("tunnel", ["tunnel0"]), ("well", ["well%d" % i for i in range(7)]),
             ("tower", ["tower0", "tower1", "tower2", "tower3"]), ("tower_top", ["tower4", "tower5"])]


# A map is dumped at several moments: the game moves water by redrawing tiles, so a fountain is another square at
# another moment (eight steps of 16 frames). The dumps whose tiles differ are kept.
MOMENTS, APART = 17, 8
CHR_AT, CHR_LEN = 16 + 32 + 256 * 256 * 5, 8192         # where a map dump keeps the tiles


def moments(cmd, first, base):
    """dumps the map at frame `first` of the run `cmd` and MOMENTS - 1 later moments; returns the dumps kept, the
    first of them base + ".map". A moment at which the game shows nothing (every color black: it is leaving
    the map, as where the player was put on a trap door) ends the row."""
    for old in glob.glob(base + "_[0-9]*.map"):
        os.remove(old)
    kept, seen = [], set()
    for k in range(MOMENTS):
        dump = base + (".map" if k == 0 else "_%d.map" % k)
        run(cmd + ["--frames", str(first + k * APART), "--map-dump", dump, "--quiet"])
        d = open(dump, "rb").read()
        tiles = d[CHR_AT:CHR_AT + CHR_LEN]
        dark = all((c & 0x0F) >= 0x0D for c in d[16:32])       # (the background's colors)
        if k and (dark or tiles in seen):
            os.remove(dump)
            if dark:
                break
        else:
            seen.add(tiles)
            kept.append(dump)
    return kept


def run(cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode:
        sys.stdout.write(r.stdout + r.stderr)
        raise SystemExit("failed: " + " ".join(cmd[:3]))
    return r.stdout


def py(script, *args):
    out = run([sys.executable, os.path.join(HERE, script)] + list(args))
    sys.stdout.write(out)


def sha1(path):
    d = open(path, "rb").read()
    if len(d) % 0x8000 == 512:
        d = d[512:]
    return hashlib.sha1(d).hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dq12", required=True, help="Dragon Quest I & II (Japan), Super Famicom")
    ap.add_argument("--dq3", required=True, help="Dragon Quest III (Japan), Super Famicom")
    ap.add_argument("--nes", default=None, help="Dragon Warrior IV (USA); default: the one in rom/")
    ap.add_argument("--uncensor", default=None, help="the Graphical Uncensoring hack's .ips, to cover what it changes")
    ap.add_argument("--out", default=os.path.join(ROOT, "sfc.pak"))
    ap.add_argument("--keep-captures", action="store_true",
                    help="do not run the Super Famicom games again: use the captures already under generated/sfc/cap")
    a = ap.parse_args()
    nes = a.nes or (glob.glob(os.path.join(ROOT, "rom", "*.nes")) or [None])[0]
    capture = os.path.join(ROOT, "build", "sfc-capture" + EXE)
    headless = os.path.join(ROOT, "build", "nes-headless-recomp" + EXE)
    for need, how in ((capture, "make sfc-capture"), (headless, "make headless-recomp")):
        if not os.path.exists(need):
            raise SystemExit("%s is not built: %s" % (need, how))
    if not nes:
        raise SystemExit("no Dragon Warrior IV ROM: put it in rom/ or pass --nes")
    for name, path in (("dq12", a.dq12), ("dq3", a.dq3)):
        if sha1(path) != KNOWN[name]:
            print("note: %s is not the dump these scripts were made with (SHA-1 %s); the result may differ" % (path, sha1(path)))
    gen = os.path.join(ROOT, "generated", "sfc")
    cap = os.path.join(gen, "cap")
    os.makedirs(cap, exist_ok=True)
    scenes = os.path.join(HERE, "scenes")

    print("1. the Super Famicom games' scenes" + (" (kept)" if a.keep_captures else ""))
    if not a.keep_captures:
        frames = max(int(ln.split()[0]) for ln in open(os.path.join(scenes, "dq12.captures")) if ln.strip()) + 1
        run([capture, a.dq12, "--frames", str(frames), "--keys", os.path.join(scenes, "dq12.keys"),
             "--captures", os.path.join(scenes, "dq12.captures"), cap])
        run([capture, a.dq3, "--frames", "1001", "--capture", "1000", os.path.join(cap, "dq3_overworld")])   # its opening shows the overworld
    names = []
    for scene in ("dq3", "dq3_front", "dq3_cave"):
        lines = [ln.split() for ln in open(os.path.join(scenes, scene + ".captures")) if ln.strip()]
        names += [ln[1] for ln in lines]
        if a.keep_captures:
            continue
        print("   Dragon Quest III, %s: %d frames (a few minutes)" % (scene, int(lines[-1][0]) + 1), flush=True)
        holds = os.path.join(scenes, scene + ".holds")      # (the cave's script keeps the hero's hit points up)
        run([capture, a.dq3, "--frames", str(int(lines[-1][0]) + 1), "--keys", os.path.join(scenes, scene + ".keys"),
             "--captures", os.path.join(scenes, scene + ".captures"), cap] + (["--holds", holds] if os.path.exists(holds) else []))

    print("2. the libraries")
    py("sfclib.py", "build", os.path.join(gen, "dq1_castle.sfclib"), *["%s=%s" % (n, os.path.join(cap, n)) for n in DQ1_CASTLE])
    py("sfclib.py", "build", os.path.join(gen, "dq1_ow.sfclib"), *["%s=%s" % (n, os.path.join(cap, n)) for n in DQ1_OVERWORLD])
    py("sfcfig.py", "build", os.path.join(gen, "dq1.figs"), *["%s=%s" % (n, os.path.join(cap, n)) for n in DQ1_CASTLE])
    py("sfclib.py", "dq3-overworld", os.path.join(gen, "dq3_ow.sfclib"), a.dq3, os.path.join(cap, "dq3_overworld"))
    town, castle, front, house, figures = dq3_sets(names)
    cave, cellar, tower = (sorted(n for n in names if n.startswith(p)) for p in ("v_", "w_", "x_"))
    for lib, group in (("dq3_town", town), ("dq3_castle", castle), ("dq3_front", front), ("dq3_house", house), ("dq3_cave", cave),
                       ("dq3_cellar", cellar), ("dq3_tower", tower)):
        py("sfclib.py", "build", os.path.join(gen, lib + ".sfclib"), *["%s=%s" % (n, os.path.join(cap, n)) for n in group])
    py("sfcfig.py", "build3", os.path.join(gen, "dq3.figs"), a.dq3, *["%s=%s" % (n, os.path.join(cap, n)) for n in figures])

    print("3. Dragon Warrior IV's maps")
    script = os.path.join(scenes, "dw4_chapter1.input")
    state = os.path.join(gen, "extract.state")          # (the game stopped at a frame of the script, to go on from)
    game = [headless, "--rom", nes, "--recomp"]
    for hack, tag in ((None, ""), (a.uncensor, "_uncensored")):
        first, every = [], []
        for name, frame in NES_MAPS:
            if tag and (name == "overworld" or not hack):
                continue
            ips = ["--ips", hack] if tag else []
            run(game + ips + ["--frames", str(frame), "--input", script, "--state-save", state, "--quiet"])
            kept = moments(game + ips + ["--state-load", state], 1, os.path.join(gen, name + tag))
            first.append(kept[0])
            every += kept
            if not tag:
                py("nesmap.py", "squares", os.path.join(gen, "nes_" + name), *kept)
        if tag and not first:       # no hack file: nothing of its own to cover
            for f in ("nes_uncensored.json", "nes_people_uncensored.json"):
                open(os.path.join(gen, f), "w").write("[]")
        elif tag:
            py("nesmap.py", "squares", os.path.join(gen, "nes_uncensored"), *every)
            py("nesmap.py", "figures", os.path.join(gen, "nes_people_uncensored"), *first)
        else:
            py("nesmap.py", "figures", os.path.join(gen, "nes_people"), *first)
    run(game + ["--frames", "6585", "--input", script, "--state-save", state, "--quiet"])
    warped = {}
    for name, place, level in NES_WARPS:
        warped[name] = moments(game + ["--state-load", state, "--keys", "0:DOWN;55:-",
                                       "--poke", "5-21:63:%02X" % place, "--poke", "9-21:64:%02X" % level], 115, os.path.join(gen, name))
    for inv, names in NES_KINDS:
        py("nesmap.py", "squares", os.path.join(gen, "nes_" + inv), *[d for n in names for d in warped[n]])
    py("nesmap.py", "figures", os.path.join(gen, "nes_people_ch1"), *[warped[n][0] for n, _, _ in NES_WARPS])
    os.remove(state)

    print("4. the pack")
    py("pack.py", "build", a.out, *sorted(glob.glob(os.path.join(HERE, "maps", "*.map"))))


if __name__ == "__main__":
    main()
