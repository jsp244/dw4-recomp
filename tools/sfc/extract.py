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
# last scene to Aliahan, the king's audience and three long walks through the town: about 91,000 frames; then
# to Luisa's Place there, where a soldier, a fighter, a priest and a mage are entered in the register upstairs and
# called to the party at the counter (a ready-made soldier and priest first, the soldier later left for the
# mage), and the party walks in the four directions for its pictures (captures y_*): 16,700 frames more.
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
# their inventories, one for every kind of map (the game's number at $28: art is bound to the kind, pack.py "here").
# (Chapter 1's keep their names; scenes/dw4_places.txt lists every place of the game for the inventories nes_kXX.)
NES_KINDS = [("castle_up", ["burland2"]), ("rooms", ["burland3", "izmit1", "izmit2"]), ("izmit", ["izmit0"]),
             ("tunnel", ["tunnel0"]), ("well", ["well%d" % i for i in range(7)]),
             ("tower", ["tower0", "tower1", "tower2", "tower3"]), ("tower_top", ["tower4", "tower5"])]


# Chapter 2's people. The game starts a chapter by itself when the chapter's number (at $615A, counted from 0) is
# held while a new quest's adventure log is continued: it shows the chapter's title, tells its opening and builds
# its party (Alena, before her father's throne). The steps from there, each (keys, pokes, frames) from the state the
# step before left:
#  1. power-on with the new quest's battery file: CONTINUE A QUEST, log 1, the chapter's number held, the opening;
#  2. Alena takes the throne room's stairs while Burland castle's upper floor is held as the place she leaves (the
#     game looks the stairs up there and puts her at the foot of that castle's stairs), is set before its door and
#     stops on it as Ragnar does at frame 6585 of chapter 1's script: from there any place can be loaded;
#  3. Santeem's gate is loaded, she is set past its guards and walks out: on the overworld Cristo and Brey join her
#     (the game's own scene);
#  4. back in through the gate, up to the throne room, and by the same stairs to Burland castle's door, the three.
A_EVERY = lambda first, last: ";".join("%d:A;%d:-" % (t, t + 6) for t in range(first, last + 1, 60))
CH2_STEPS = [
    ("1600:START;1606:-;1910:A;1916:-;2120:A;2126:-;2330:A;2336:-;2600:A;2606:-;2800:A;2806:-;3000:A;3006:-;"
     "3200:A;3206:-;3400:A;3406:-;3600:A;3606:-;3800:A;3806:-", ["2130-3990:615A:01"], 3990),
    ("60:DOWN;110:-;215:DOWN;330:-",
     ["40-44:44:0B", "40-44:45:10", "90-95:63:02", "90-95:64:02", "200-204:44:10", "200-204:45:23"], 231),
    ("0:DOWN;55:-;150:A;156:-;210:A;216:-;270:A;276:-;330:A;336:-;400:DOWN;600:-;" + A_EVERY(640, 1700),
     ["5-21:63:01", "9-21:64:00", "12-21:44:0F", "12-21:45:1A"], 1760),
    ("12:UP;44:-;150:A;156:-;210:A;216:-;270:A;276:-;330:A;336:-;390:A;396:-;470:UP;520:-;640:UP;670:-;700:DOWN;760:-;"
     "815:DOWN;940:-",
     ["0-3:42:27", "0-3:43:4A", "450-454:44:0F", "450-454:45:0F", "726-731:63:02", "726-731:64:02",
      "800-804:44:10", "800-804:45:23"], 834),
]
# chapter 2's places and how many levels each has (the game's numbers, as in scenes/dw4_places.txt): Santeem castle,
# Surene, Tempe, Frenor, the desert Bazaar, the shrines of the travel doors, the Birdsong Tower, Endor
CH2_PLACES = [(0x01, 3), (0x13, 5), (0x0E, 1), (0x19, 3), (0x0C, 1), (0x1D, 1), (0x1E, 1), (0x1F, 1), (0x3F, 6), (0x04, 15)]

# A map is dumped at several moments: the game moves water by redrawing tiles, so a fountain is another square at
# another moment (eight steps of 16 frames). The dumps whose tiles differ are kept.
MOMENTS, APART = 17, 8
KIND_AT = 16 + 32 + 256 * 256 * 5 + 8192 + 0x28         # where a map dump keeps the map's kind (the game's $28)


def moments(cmd, first, base):
    """dumps the map at frame `first` of the run `cmd` and at the later moments whose tiles differ (nes-headless
    --map-moments); returns the dumps, the first of them base + ".map" """
    for old in glob.glob(base + "_[0-9]*.map"):
        os.remove(old)
    run(cmd + ["--frames", str(first), "--map-dump", base + ".map", "--map-moments", "%d:%d" % (MOMENTS, APART), "--quiet"])
    return [base + ".map"] + sorted(glob.glob(base + "_[0-9]*.map"), key=lambda p: int(p[len(base) + 1:-4]))


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
    # the hero and his companions (the end of the first scene: Luisa's Place), a set for each class
    py("sfcfig.py", "build3p", os.path.join(gen, "dq3_party.figs"), a.dq3,
       *["%s=%s" % (n, os.path.join(cap, n)) for n in sorted(n for n in names if n.startswith("y_"))])

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
    # every place of the game and its levels (scenes/dw4_places.txt), one inventory for each kind of map: nes_kXX
    kinds = {}
    for ln in open(os.path.join(scenes, "dw4_places.txt")):
        t = ln.split("#")[0].split()
        if len(t) < 2:
            continue
        place, level = int(t[0], 16), int(t[1], 16)
        dumps = moments(game + ["--state-load", state, "--keys", "0:DOWN;55:-",
                                "--poke", "5-21:63:%02X" % place, "--poke", "9-21:64:%02X" % level], 115,
                        os.path.join(gen, "p%02X_%02X" % (place, level)))
        kinds.setdefault(open(dumps[0], "rb").read()[KIND_AT], []).extend(dumps)
    for kind in sorted(kinds):
        py("nesmap.py", "squares", os.path.join(gen, "nes_k%02X" % kind), *kinds[kind])
    # chapter 2's people (CH2_STEPS): the maps of its places with its own party and townsfolk
    sav = os.path.join(gen, "newquest.sav")
    if os.path.exists(sav):
        os.remove(sav)
    run(game + ["--frames", "6600", "--input", script, "--sav", sav, "--quiet"])
    for n, (keys, pokes, frames) in enumerate(CH2_STEPS):
        cmd = game + (["--state-load", state] if n else ["--sav", sav]) + ["--keys", keys, "--frames", str(frames),
                                                                         "--state-save", state, "--quiet"]
        for p in pokes:
            cmd += ["--poke", p]
        run(cmd)
    dumps = []
    for place, levels in CH2_PLACES:
        for level in range(levels):
            dumps.append(os.path.join(gen, "c2_%02X_%02X.map" % (place, level)))
            run(game + ["--state-load", state, "--keys", "0:DOWN;55:-", "--poke", "5-21:63:%02X" % place,
                        "--poke", "9-21:64:%02X" % level, "--frames", "115", "--map-dump", dumps[-1], "--quiet"])
    py("nesmap.py", "figures", os.path.join(gen, "nes_people_ch2"), *dumps)
    os.remove(sav)
    os.remove(state)

    print("4. the pack")
    py("pack.py", "build", a.out, *sorted(glob.glob(os.path.join(HERE, "maps", "*.map"))))


if __name__ == "__main__":
    main()
