#!/usr/bin/env python3
"""tools/sfc/extract.py - make the SNES Remaster's art pack (sfc.pak) from your own cartridge.

    python tools/sfc/extract.py --dq3 "Dragon Quest III (Japan).sfc"
                                [--nes "Dragon Warrior IV (USA).nes"] [--uncensor HACK.ips] [--out sfc.pak]
                                [--keep-captures] [--dq12 "Dragon Quest I & II (Japan).sfc"]
                                [--dq5 "Dragon Quest V (Japan).sfc"] [--dq6 "Dragon Quest VI (Japan).sfc"]
                                [--write-supplement]

One Super Famicom game is needed: Dragon Quest III, where nearly all the art comes from. The few pictures of other
games of the series (Dragon Quest I.II's castle brick and a handful more, the ship of Dragon Quest V, the wagon and
a few animals and creatures of Dragon Quest VI) are read from the supplement (tools/sfc/supplement.bin, locked to the Dragon
Quest III ROM: supplement.py). With --dq12, --dq5 and --dq6 those games are run as well and their pictures are cut
from them instead; with all three, --write-supplement writes the supplement again from what the map files use:
that is for whoever changes the map files.

What it does, step by step (everything it writes is under generated/sfc, which git ignores, and the pack):
 1. runs both Super Famicom games without a window (build/sfc-capture: `make sfc-capture`), pressing the
    buttons the scripts in tools/sfc/scenes name, and saves the machine's memory at the frames they list;
 2. cuts those captures into libraries: the 16x16 map squares (sfclib.py) and the people (sfcfig.py); Dragon
    Quest III's overworld squares are read straight from its ROM. Characters of Dragon Quest III that the scenes
    do not pass (its dog, its cat, its horse, ...) the game is asked to draw by their numbers: see DQ3_PEOPLE;
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
KNOWN = {"dq12": "1c0c6d78bf2bc29160adf48b17ebf5a5bc46230e", "dq3": "b711341e4b1022d7645c5313840874aebc01b7b7",
         "dq5": "1c47ed62c561d7965fe5dc2a03f4c37feb4a46b5", "dq6": "3e699dc7e064d6ac84b1981aa150fdf1672b5456"}

# the libraries cut from games other than Dragon Quest III: what the map files use of them goes into the supplement
SUPPLEMENT_LIBRARIES = ("dq1_castle.sfclib", "dq1_ow.sfclib", "dq1.figs", "dq5_ship.sfclib", "dq6_people.figs",
                        "dq6_dragon.figs", "dq6_lord.figs", "dq6_sky.figs")
# Dragon Quest V opens on a ship: its script (scenes/dq5.keys) starts a new game, lets the opening's words pass, walks
# the boy out of his cabin, through the storeroom and up the stairs, opens the deckhouse's door and walks the deck
# from bow to stern; scenes/dq5.captures names the 27 pictures the deck is cut from. That game keeps its map's
# squares at odd tile columns and rows of its tile map (sfclib.py: parity=1,1).
# the captures each library is cut from (names from scenes/dq12.captures)
DQ1_CASTLE = ["dq1_throne1", "dq1_throne2", "dq1_castle0", "dq1_castle1", "dq1_castle2", "dq1_castle3", "dq1_castle4", "dq1_x5"] + \
             ["w1_%03d" % i for i in list(range(0, 33)) + [38]]
DQ1_OVERWORLD = ["w1_%03d" % i for i in (34, 35, 36, 40, 41, 42, 43, 44)]
# Dragon Quest III (names from scenes/dq3.captures and dq3_front.captures; the order is the libraries' order, which
# the map files' figure sets count by). Its script plays the game from power-on through the opening's questions and
# last scene to Aliahan, the king's audience and three long walks through the town: about 91,000 frames; then
# to Luisa's Place there, where a soldier, a fighter, a priest and a mage are entered in the register upstairs and
# called to the party at the counter (a ready-made soldier and priest first, the soldier later left for the
# mage), and the party walks in the four directions for its pictures (captures y_*): 16,700 frames more; then a
# merchant and a female priest are registered, the three companions left, and those two called with the ready-made
# female mage, for the same walk: 7,600 frames more.
# scenes/dq3_cave goes on from the town: out of its south gate, over the bridge to the cape, into the cave there
# (captures v_*: its rock, its floor, its stairs and its treasure chests), up the stairs at the cave's east end into
# the passages under the tower on the island (w_*) and up into the tower's ground floor (x_*: its stone floors
# and wall tops, its statues, pillars and torches): about 69,000 frames from power-on. A hero alone at the start of
# the game does not live through that walk's battles, so its .holds file keeps his hit points up (sfc-capture
# --holds) and the battles are fought by pressing A; from the cave's east end on it also keeps the game's count of
# steps to the next battle up, and there are none.
DQ3_SKIP = {"c_000", "c_047", "h_015"}          # (pictures between two maps: black)
# Dragon Quest III's people by their numbers. The towns the scripts walk through show only some thirty of the game's
# characters. But the game has a record of twelve bytes for every person of every map (in the ROM from $0808D4, the
# records numbered through all maps): the character is bits 6-13 of the word at +6, the way it faces bits 14-15
# (0 up, 1 right, 2 down, 3 left). So the game is asked to draw the others: in a copy of the ROM the four people on
# the ground floor of Luisa's Place (DQ3_RECORDS) are given a wanted character's number and the four facings, the
# first scene's state upstairs there is loaded (DQ3_UPSTAIRS: the frame of scenes/dq3.keys at which the hero stands
# in the registry, the last companion entered) and he walks down the stairs (scenes/dq3_people.keys), which
# loads the floor: the character stands there in its own colors, walking on the spot, and two captures sixteen
# frames apart have both steps. Three of the four are on the screen then: the first record's behind the counter,
# the third's on the carpet, the fourth's in the middle of the room. So the first run shows the character from
# behind, from the front and facing left, and a second run turns the one on the carpet right. The copy is deleted
# again.
DQ3_UPSTAIRS = 102762
DQ3_RECORDS = (0x082AD0, 0x082ADC, 0x082AE8, 0x082B0C)
DQ3_FACINGS = (("", (0, 1, 2, 3)), ("R", (3, 0, 1, 2)))
# number: who (the map files name a set by the number in decimal: dq3n:g90 is the dog). The game has more; add a
# number here and its captures are made on the next run, also with --keep-captures.
DQ3_PEOPLE = {
    0x01: "the hero", 0x1B: "a sage (the woman)", 0x1C: "the striped cat", 0x22: "a dwarf", 0x23: "a winged demon", 0x24: "a slime", 0x2A: "a woman in blue",
    0x2C: "a girl", 0x2D: "a woman in red", 0x2E: "a woman in green", 0x31: "a hooded man", 0x32: "a young man", 0x33: "a woman in lilac",
    0x34: "a bald old man in blue", 0x36: "an old woman in brown", 0x37: "a king", 0x3B: "a man in a turban",
    0x3F: "a boy", 0x40: "a soldier", 0x43: "a swordsman", 0x46: "a merchant in green",
    0x45: "a stout merchant", 0x47: "an old woman in blue", 0x48: "a townsman", 0x49: "a man in a striped shirt",
    0x4A: "a bandit in green", 0x4B: "a bearded ruffian", 0x4C: "a bald old man with a beard", 0x4D: "a king in red",
    0x4E: "a nun", 0x4F: "a woman in a white bonnet", 0x50: "a dancer", 0x52: "a bunny girl", 0x54: "a wizard with a staff",
    0x55: "an elf", 0x56: "a dwarf in a pointed cap", 0x59: "a cat", 0x5A: "a dog", 0x5B: "a horse", 0x5C: "a skeleton",
    0x5D: "a gargoyle", 0x5E: "a slime", 0x5F: "a sailor", 0x70: "an elf in red", 0x73: "a white horse", 0x74: "a king", 0x75: "a warrior in a horned helmet", 0x76: "a pirate",
    0x78: "a flame", 0x79: "a green flame", 0x7A: "a boulder", 0x95: "a priest in a mitre", 0x96: "a bald old man in white",
}

# Dragon Quest VI's people by their numbers. That game runs on the engine Dragon Quest III's remake was built on,
# and keeps a record of ten bytes for every person of a map: the character is bits 6-13 of its first word, the way
# it faces bits 14-15 (0 up, 1 right, 2 down, 3 left). Its opening has such a person a minute and a half from
# power-on: the man who waits at the cliff's edge north of the campfire (the record at DQ6_RECORD). scenes/dq6.keys
# starts a new game, lets the opening's words pass and walks the hero to the top of the campfire's map (DQ6_CLIFF
# frames; the state is kept). In a copy of the ROM the man is given a wanted character's number and a facing, the
# state is loaded and the hero walks up into the cliff's map (scenes/dq6_people.keys) and stops below him: the
# character stands at the cliff in its own colors, walking on the spot, and two captures twenty frames apart (a step
# lasts twenty) have both steps. Four runs for a character, one for each facing. The copy is deleted again.
# The game has 106 characters (numbers $00-$69), its monsters that walk the maps among them.
DQ6_CLIFF = 5596
DQ6_RECORD = 0x3F0A10
DQ6_FACINGS = (("U", 0), ("R", 1), ("D", 2), ("L", 3))
# The same opening has a great golden dragon: when the hero walks up to the companion who stands at the cliff's edge
# (scenes/dq6_dragon.keys, from the cliff's state) she calls it, and it hovers over the sea alone, beating its
# wings, before it comes for them. Two captures there are two beats (frames after the state): dq6_dragon.figs.
DQ6_DRAGON = (2444, 2468)
# The script goes on: the dragon carries them to the demon lord's castle, they walk up through it (the left carpet
# of the hall, the arch, the room above to the stairs at its left end, the great hall, his door) into his room,
# and he shows himself on his throne, a great seated shape in shadow with lit eyes. One capture there, and of its
# sprites only his (the three stand before him): dq6_lord.figs.
DQ6_LORD = 12780
# Between the two, the flight: the dragon carries them through a night of cloud, a painting that scrolls by
# behind it, 256 pixels wide where it repeats and taller than the screen. Eighteen captures of quiet moments
# (lightning lights the wisps up in between) show all of the part between two rows where it is plain dark
# right across, so that it also repeats downwards without a seam: dq6_sky.figs, 256 x 238.
DQ6_SKY = (4420, 4500, 4548, 4596, 4644, 4692, 4740, 4788, 4852, 4900, 4948, 5004, 5052, 5100, 5148, 5196, 5252, 5300)
DQ6_SKY_ROWS = "291-528"              # counted from the first capture's scroll
DQ6_LORD_BOX = (100, 36, 156, 90)
# number: who (the map files name a set by the number in decimal: dq6n:g11 is the wagon)
DQ6_PEOPLE = {0x0B: "the covered wagon", 0x12: "a clawed demon", 0x14: "a wild-haired ogre", 0x18: "a small dragon",
              0x1D: "a troll", 0x47: "a dog", 0x4F: "a cow", 0x5D: "a stone giant"}


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

# Chapter 3's people. Its opening is run the same way (the number 2 held). When its words are over Taloon lies in his
# bed in Lakanaba and the game waits for a direction to be pressed: then Neta hands him his lunch and he is the
# player's. His house's stairs take him, as the throne room's took Alena, to the foot of Burland castle's stairs,
# and he stops on its door.
CH3_STEPS = [
    ("1600:START;1606:-;1910:A;1916:-;2120:A;2126:-;2330:A;2336:-;"
     + ";".join("%d:A;%d:-" % (t, t + 6) for t in range(2600, 4001, 200)), ["2130-4300:615A:02"], 4300),
    ("20:DOWN;60:-;150:A;156:-;270:A;276:-;420:DOWN;470:-;575:DOWN;700:-",
     ["400-404:44:21", "400-404:45:06", "450-455:63:02", "450-455:64:02", "560-564:44:10", "560-564:45:23"], 594),
]
# Lakanaba, Foxville, Bonmalmo, the castle on the way to Endor, two villages, Endor's castle and casino
CH3_PLACES = [(0x16, 3), (0x0F, 2), (0x06, 4), (0x05, 2), (0x18, 2), (0x14, 3), (0x04, 2)]
# Chapter 4's people (the number 3 held). Its opening is Nara and Mara's talk in the dressing room of Monbaraba's
# theater; the last A answers Nara's "Shall we leave now?", and they stand in the town. One of the town's stairs
# (at 7,18) takes them, as before, to the foot of Burland castle's stairs, and they stop on its door.
CH4_STEPS = [
    ("1600:START;1606:-;1910:A;1916:-;2120:A;2126:-;2330:A;2336:-;"
     + ";".join("%d:A;%d:-" % (t, t + 6) for t in range(2600, 6601, 200)), ["2130-6790:615A:03"], 6790),
    ("40:DOWN;90:-;195:DOWN;320:-",
     ["20-24:44:07", "20-24:45:11", "70-75:63:02", "70-75:64:02", "180-184:44:10", "180-184:45:23"], 214),
]
# Monbaraba, Kievs, the cave west of it, Aktemto, Haville with its ship, Keeleon's castle, Aktemto's mine (the upper
# five levels of place $2D, which no icon of the overworld leads to: it is entered from the town)
CH4_PLACES = [(0x15, 4), (0x17, 2), (0x31, 5), (0x0A, 1), (0x11, 6), (0x00, 5), (0x2D, 5)]
# Chapter 5's people (the number 4 held). The opening's words end with the hero's mother sending him to the pond with
# his father's lunch, and he stands in his house in the hidden village. The stairs down to the village's cellar
# (at 21,2) take him to Burland castle's ground floor, and he stops on its door. Chapter 5 goes everywhere: every
# town, castle and shrine of the game is dumped with its people as that chapter has them.
CH5_STEPS = [
    ("1600:START;1606:-;1910:A;1916:-;2120:A;2126:-;2330:A;2336:-;"
     + ";".join("%d:A;%d:-" % (t, t + 6) for t in range(2600, 3601, 200)), ["2130-3790:615A:04"], 3790),
    ("40:DOWN;90:-;195:DOWN;320:-",
     ["20-24:44:15", "20-24:45:01", "70-75:63:02", "70-75:64:02", "180-184:44:10", "180-184:45:23"], 214),
]
CH5_PLACES = [(0x00, 5), (0x01, 3), (0x02, 4), (0x03, 4), (0x04, 15), (0x05, 2), (0x06, 4), (0x07, 2), (0x08, 5), (0x09, 8),
              (0x0A, 1), (0x0B, 2), (0x0C, 1), (0x0D, 3), (0x0E, 1), (0x0F, 2), (0x10, 4), (0x11, 6), (0x12, 3), (0x13, 5),
              (0x14, 3), (0x15, 4), (0x16, 3), (0x17, 2), (0x18, 2), (0x19, 3), (0x1A, 2), (0x1B, 5), (0x1C, 2), (0x1D, 1),
              (0x1E, 1), (0x1F, 1), (0x20, 1), (0x21, 1), (0x22, 2), (0x23, 1), (0x24, 1), (0x25, 2), (0x26, 1), (0x27, 3),
              (0x28, 3), (0x29, 2), (0x2A, 1), (0x2B, 1), (0x2C, 1), (0x43, 11)]

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
    ap.add_argument("--dq12", default=None, help="Dragon Quest I & II (Japan), Super Famicom: cut its pictures from the "
                    "game itself instead of reading them from the supplement")
    ap.add_argument("--supplement", default=os.path.join(HERE, "supplement.bin"),
                    help="the pictures of the games other than Dragon Quest III (default: tools/sfc/supplement.bin)")
    ap.add_argument("--dq5", default=None, help="Dragon Quest V (Japan), Super Famicom: cut its ship from the game itself "
                    "instead of reading it from the supplement")
    ap.add_argument("--dq6", default=None, help="Dragon Quest VI (Japan), Super Famicom: have the game itself draw its "
                    "people instead of reading them from the supplement")
    ap.add_argument("--write-supplement", action="store_true",
                    help="with --dq12, --dq5 and --dq6: write the supplement from what the map files use")
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
    others = a.dq12 and a.dq5 and a.dq6
    if a.write_supplement and not others:
        raise SystemExit("--write-supplement needs the other games too: --dq12, --dq5 and --dq6")
    if not others and not os.path.exists(a.supplement):
        raise SystemExit("%s is not there: the pictures of the other games come from it, or from those games "
                         "with --dq12, --dq5 and --dq6" % a.supplement)
    for name, path in (("dq12", a.dq12), ("dq3", a.dq3), ("dq5", a.dq5), ("dq6", a.dq6)):
        if path and sha1(path) != KNOWN[name]:
            print("note: %s is not the dump these scripts were made with (SHA-1 %s); the result may differ" % (path, sha1(path)))
    gen = os.path.join(ROOT, "generated", "sfc")
    cap = os.path.join(gen, "cap")
    os.makedirs(cap, exist_ok=True)
    scenes = os.path.join(HERE, "scenes")

    print("1. the Super Famicom games' scenes" + (" (kept)" if a.keep_captures else ""))
    if not a.keep_captures:
        if a.dq12:
            frames = max(int(ln.split()[0]) for ln in open(os.path.join(scenes, "dq12.captures")) if ln.strip()) + 1
            run([capture, a.dq12, "--frames", str(frames), "--keys", os.path.join(scenes, "dq12.keys"),
                 "--captures", os.path.join(scenes, "dq12.captures"), cap])
        run([capture, a.dq3, "--frames", "1001", "--capture", "1000", os.path.join(cap, "dq3_overworld")])   # its opening shows the overworld
    if a.dq5:                           # (a short scene: it is also played when its captures are not there yet)
        lines = [ln.split() for ln in open(os.path.join(scenes, "dq5.captures")) if ln.strip()]
        if not a.keep_captures or not all(os.path.exists(os.path.join(cap, ln[1] + ".regs")) for ln in lines):
            print("   Dragon Quest V: %d frames" % (int(lines[-1][0]) + 1), flush=True)
            run([capture, a.dq5, "--frames", str(int(lines[-1][0]) + 1), "--keys", os.path.join(scenes, "dq5.keys"),
                 "--captures", os.path.join(scenes, "dq5.captures"), cap])
    names = []
    upstairs = os.path.join(cap, "dq3_upstairs.state")
    for scene in ("dq3", "dq3_front", "dq3_cave"):
        lines = [ln.split() for ln in open(os.path.join(scenes, scene + ".captures")) if ln.strip()]
        names += [ln[1] for ln in lines]
        if a.keep_captures:
            continue
        print("   Dragon Quest III, %s: %d frames (a few minutes)" % (scene, int(lines[-1][0]) + 1), flush=True)
        holds = os.path.join(scenes, scene + ".holds")      # (the cave's script keeps the hero's hit points up)
        cmd = [capture, a.dq3, "--keys", os.path.join(scenes, scene + ".keys"),
               "--captures", os.path.join(scenes, scene + ".captures"), cap] + (["--holds", holds] if os.path.exists(holds) else [])
        if scene == "dq3":          # (in two parts: the state between them is where the people by number are drawn from)
            run(cmd + ["--frames", str(DQ3_UPSTAIRS), "--state-save", upstairs])
            cmd += ["--state-load", upstairs, "--start", str(DQ3_UPSTAIRS)]
        run(cmd + ["--frames", str(int(lines[-1][0]) + 1)])
    # Dragon Quest III's people by number (DQ3_PEOPLE): whoever has no captures yet is drawn by the game now
    people = ["n%02X%s_%s" % (n, tag, step) for n in sorted(DQ3_PEOPLE) for tag, _ in DQ3_FACINGS for step in "ab"]
    todo = [n for n in sorted(DQ3_PEOPLE) if not all(os.path.exists(os.path.join(cap, "n%02X%s_%s.regs" % (n, tag, step)))
                                                     for tag, _ in DQ3_FACINGS for step in "ab")]
    if todo:
        print("   Dragon Quest III, %d of its people by number" % len(todo), flush=True)
        if not os.path.exists(upstairs):            # (captures kept from before there was this step)
            run([capture, a.dq3, "--keys", os.path.join(scenes, "dq3.keys"), "--frames", str(DQ3_UPSTAIRS), "--state-save", upstairs])
        rom3 = open(a.dq3, "rb").read()
        head = 512 if len(rom3) % 0x8000 == 512 else 0
        copy = os.path.join(cap, "people.sfc")
        for n in todo:
            for tag, facings in DQ3_FACINGS:
                changed = bytearray(rom3)
                for at, facing in zip(DQ3_RECORDS, facings):
                    w = changed[head + at + 6] | (changed[head + at + 7] << 8)
                    w = (w & 0x003F) | (n << 6) | (facing << 14)
                    changed[head + at + 6], changed[head + at + 7] = w & 255, w >> 8
                open(copy, "wb").write(changed)
                try:
                    run([capture, copy, "--keys", os.path.join(scenes, "dq3_people.keys"), "--state-load", upstairs, "--frames", "340",
                         "--capture", "323", os.path.join(cap, "n%02X%s_a" % (n, tag)),
                         "--capture", "339", os.path.join(cap, "n%02X%s_b" % (n, tag))])
                finally:
                    os.remove(copy)
    # Dragon Quest VI's people by number (DQ6_PEOPLE), the same way
    people6 = ["v%02X%s_%s" % (n, tag, step) for n in sorted(DQ6_PEOPLE) for tag, _ in DQ6_FACINGS for step in "ab"]
    todo = [n for n in sorted(DQ6_PEOPLE) if not all(os.path.exists(os.path.join(cap, "v%02X%s_%s.regs" % (n, tag, step)))
                                                     for tag, _ in DQ6_FACINGS for step in "ab")] if a.dq6 else []
    if todo:
        print("   Dragon Quest VI, %d of its people by number" % len(todo), flush=True)
        cliff = os.path.join(cap, "dq6_cliff.state")
        run([capture, a.dq6, "--keys", os.path.join(scenes, "dq6.keys"), "--frames", str(DQ6_CLIFF), "--state-save", cliff])
        rom6 = open(a.dq6, "rb").read()
        head = 512 if len(rom6) % 0x8000 == 512 else 0
        copy = os.path.join(cap, "people.sfc")
        for n in todo:
            for tag, facing in DQ6_FACINGS:
                changed = bytearray(rom6)
                at = head + DQ6_RECORD
                w = changed[at] | (changed[at + 1] << 8)
                w = (w & 0x003F) | (n << 6) | (facing << 14)
                changed[at], changed[at + 1] = w & 255, w >> 8
                open(copy, "wb").write(changed)
                try:
                    run([capture, copy, "--keys", os.path.join(scenes, "dq6_people.keys"), "--state-load", cliff, "--frames", "491",
                         "--capture", "470", os.path.join(cap, "v%02X%s_a" % (n, tag)),
                         "--capture", "490", os.path.join(cap, "v%02X%s_b" % (n, tag))])
                finally:
                    os.remove(copy)

    dragon6 = [os.path.join(cap, "dragon_%s" % step) for step in "ab"]
    lord6 = os.path.join(cap, "lord")
    sky6 = [os.path.join(cap, "sky_%d" % f) for f in DQ6_SKY]
    if a.dq6 and not all(os.path.exists(p + ".regs") for p in dragon6 + [lord6] + sky6):
        print("   Dragon Quest VI, its great dragon and its demon lord", flush=True)
        cliff = os.path.join(cap, "dq6_cliff.state")
        if not os.path.exists(cliff):
            run([capture, a.dq6, "--keys", os.path.join(scenes, "dq6.keys"), "--frames", str(DQ6_CLIFF), "--state-save", cliff])
        run([capture, a.dq6, "--keys", os.path.join(scenes, "dq6_dragon.keys"), "--state-load", cliff,
             "--frames", str(DQ6_LORD + 1), "--capture", str(DQ6_DRAGON[0]), dragon6[0],
             "--capture", str(DQ6_DRAGON[1]), dragon6[1], "--capture", str(DQ6_LORD), lord6] +
            [x for f, p in zip(DQ6_SKY, sky6) for x in ("--capture", str(f), p)])

    print("2. the libraries")
    if not others:                  # (the other games' libraries with only the pictures the map files use)
        py("supplement.py", "unpack", a.supplement, a.dq3, gen)
    if a.dq12:
        py("sfclib.py", "build", os.path.join(gen, "dq1_castle.sfclib"), *["%s=%s" % (n, os.path.join(cap, n)) for n in DQ1_CASTLE])
        py("sfclib.py", "build", os.path.join(gen, "dq1_ow.sfclib"), *["%s=%s" % (n, os.path.join(cap, n)) for n in DQ1_OVERWORLD])
        py("sfcfig.py", "build", os.path.join(gen, "dq1.figs"), *["%s=%s" % (n, os.path.join(cap, n)) for n in DQ1_CASTLE])
    if a.dq5:
        ship = [ln.split()[1] for ln in open(os.path.join(scenes, "dq5.captures")) if ln.strip()]
        py("sfclib.py", "build", os.path.join(gen, "dq5_ship.sfclib"), "parity=1,1", *["%s=%s" % (n, os.path.join(cap, n)) for n in ship])
    if a.dq6:                       # a set for each number, as Dragon Quest III's
        py("sfcfig.py", "buildn6", os.path.join(gen, "dq6_people.figs"), *["%s=%s" % (n, os.path.join(cap, n)) for n in people6])
        py("sfcfig.py", "buildall", os.path.join(gen, "dq6_dragon.figs"), *dragon6)
        py("sfcfig.py", "buildall", os.path.join(gen, "dq6_lord.figs"), "%s@%d,%d,%d,%d" % ((lord6,) + DQ6_LORD_BOX))
        py("sfcfig.py", "buildscroll", os.path.join(gen, "dq6_sky.figs"), "2", "256", DQ6_SKY_ROWS, *sky6)
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
    # and the people the game drew by their numbers, a set for each number
    py("sfcfig.py", "buildn", os.path.join(gen, "dq3_people.figs"), *["%s=%s" % (n, os.path.join(cap, n)) for n in people])

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
    # every place of the game and its levels (scenes/dw4_places.txt), one inventory for each kind of map: nes_kXX;
    # and the people of the places no chapter's own inventory has (the dungeons: the towns, castles and shrines are
    # all among chapter 5's places): nes_people_dng
    kinds, towns, dungeons = {}, set(place for place, _ in CH5_PLACES), []
    for ln in open(os.path.join(scenes, "dw4_places.txt")):
        t = ln.split("#")[0].split()
        if len(t) < 2:
            continue
        place, level = int(t[0], 16), int(t[1], 16)
        walk = t[2] if len(t) > 2 else ""       # a level in two parts: the game loads the second on the way there
        dumps = moments(game + ["--state-load", state, "--keys", "0:DOWN;55:-" + (";130:%s;230:-" % walk if walk else ""),
                                "--poke", "5-21:63:%02X" % place, "--poke", "9-21:64:%02X" % level], 260 if walk else 115,
                        os.path.join(gen, "p%02X_%02X%s" % (place, level, "b" if walk else "")))
        kinds.setdefault(open(dumps[0], "rb").read()[KIND_AT], []).extend(dumps)
        if place not in towns and not walk:
            dungeons.append(dumps[0])
    for kind in sorted(kinds):
        py("nesmap.py", "squares", os.path.join(gen, "nes_k%02X" % kind), *kinds[kind])
    py("nesmap.py", "figures", os.path.join(gen, "nes_people_dng"), *dungeons)
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
    # chapter 3's people (CH3_STEPS, CH3_PLACES): the same with Taloon; chapter 4's with Nara and Mara; chapter 5's with the hero
    for chapter, steps, places in ((3, CH3_STEPS, CH3_PLACES), (4, CH4_STEPS, CH4_PLACES), (5, CH5_STEPS, CH5_PLACES)):
        for n, (keys, pokes, frames) in enumerate(steps):
            cmd = game + (["--state-load", state] if n else ["--sav", sav]) + ["--keys", keys, "--frames", str(frames),
                                                                             "--state-save", state, "--quiet"]
            for p in pokes:
                cmd += ["--poke", p]
            run(cmd)
        dumps = []
        for place, levels in places:
            for level in range(levels):
                dumps.append(os.path.join(gen, "c%d_%02X_%02X.map" % (chapter, place, level)))
                run(game + ["--state-load", state, "--keys", "0:DOWN;55:-", "--poke", "5-21:63:%02X" % place,
                            "--poke", "9-21:64:%02X" % level, "--frames", "115", "--map-dump", dumps[-1], "--quiet"])
        py("nesmap.py", "figures", os.path.join(gen, "nes_people_ch%d" % chapter), *dumps)
    os.remove(sav)
    os.remove(state)

    print("4. the pack")
    maps = sorted(glob.glob(os.path.join(HERE, "maps", "*.map")))
    py("pack.py", "build", a.out, *maps)
    if a.write_supplement:
        py("supplement.py", "pack", a.supplement, a.dq3, *[os.path.join(gen, n) for n in SUPPLEMENT_LIBRARIES], "--", *maps)


if __name__ == "__main__":
    main()
