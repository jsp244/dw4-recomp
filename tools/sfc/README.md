# The SNES Remaster's art

Graphics > Textures > **SNES Remaster** draws the game's map squares and people with the art of the
Super Famicom Dragon Quest games. That art is not in the packages: it is taken from your own
cartridge dump into a pack, `sfc.pak`, that the game loads from its folder. Without the pack the
choice is not offered. (This repository holds nineteen squares and seven figures of it, locked to the ROM
you supply: the supplement, below.)

You need one Super Famicom game, the Japanese **Dragon Quest III**, and your Dragon Warrior IV ROM
in `rom/`.

```
make headless-recomp sfc-capture
python tools/sfc/extract.py --dq3 "Dragon Quest III - Soshite Densetsu e... (Japan).sfc"
```

Nearly all the art is Dragon Quest III's. Nine squares are Dragon Quest I & II's (its castle brick,
its single tree, its barrier floor, its throne room's tiles), and the ship in Haville's harbour is
Dragon Quest V's, the one its story opens on (ten squares: deck, rim, mast, barrel, wheel, cabin
door). Seven figures are Dragon Quest VI's: the party's wagon, the ox (its cow, turned brown), the
fox (none of the games has one: its dog, turned fox-red), the demon at the top of chapter 1's
tower, the wild-haired brutes of chapter 5 (its ogre, turned purple), Keeleon (none of the games
has a lion-beast: its troll, turned brown) and the stone guards of chapter 5 (its stone giant).
They come from the supplement,
`tools/sfc/supplement.bin`: just those pictures, combined with
a stream made from the Dragon Quest III ROM's contents, so that the file opens only with that very
ROM (`supplement.py`). If you would rather take them from your own cartridges, give those games
too: `--dq12 "Dragon Quest I & II (Japan).sfc"`, `--dq5 "Dragon Quest V (Japan).sfc"` (Dragon Quest V
is then played from power-on onto its ship's deck, 12,814 frames), `--dq6 "Dragon Quest VI
(Japan).sfc"` (played to the cliff of its opening, 5,596 frames, where the game is asked to draw
each wanted character in place of the man who waits there). Whoever changes the map files
writes the supplement again with `--dq12 ... --dq5 ... --dq6 ... --write-supplement`: it holds what
the map files use of the other games' libraries and nothing else.

That writes `sfc.pak` next to the Makefile; copy it beside the game. It takes twenty minutes or
more: most of it is Dragon Quest III being played, without a window, from power-on through its
opening to its first town and castle (where a soldier, a fighter, a priest and a mage are
registered and join its hero at the tavern, for the heroes' figures), and on through the cave on
the cape nearby to the tower on the island, for their treasure chests, statues and stonework
(about 230,000 frames in three scripts). `--keep-captures` on a later run skips that part
and uses the captures already made.
Add `--uncensor FILE.ips` if you play with the Graphical Uncensoring hack: it changes a few squares
and people, which then get their art too.

## How it works

| step | tool | what it does |
|---|---|---|
| 1 | `capture.c` (`sfc-capture`) | runs a Super Famicom game without a window (LakeSnes, `third_party/lakesnes`), pressing the buttons a script names, and saves the machine's memory at the frames a list names (`scenes/*.keys`, `scenes/*.captures`). The cave's script also keeps its hero's hit points up, and later the game's count of steps to the next battle (`scenes/dq3_cave.holds`): he is alone at the start of the game, and the battles on the way are fought by pressing A or do not come |
| 2 | `sfclib.py`, `sfcfig.py` | cut the captures into libraries of 16x16 map squares (Dragon Quest III's two layers kept apart as well, for things that stand on other ground or reach into the square above) and of people (16x16 in Dragon Quest I.II, 16x24 in III); Dragon Quest III's overworld squares are read straight from its ROM |
| 2 | `supplement.py` | reads the pictures of the games other than Dragon Quest III from the supplement into the same libraries (only the entries the map files use), or writes the supplement from them |
| 3 | `nesmap.py` | lists the squares and people of Dragon Warrior IV's maps, from `nes-headless --map-dump` (the game's own map code, asked for every square) |
| 4 | `pack.py` | builds the pack from the map files in `maps/`: which Super Famicom art takes the place of which NES square or person. People lying down need no lines: the NES draws a sleeper or a wounded man by turning his standing front view a quarter, a half, three quarters (`nesmap.py` finds those kinds), and the pack turns the figure it has for that view the same way |

The same ROMs and scripts give the same pack every time.

## Which game supplies what

A place keeps the character the NES game gave it: its materials and its colors come first, and the
Super Famicom art is chosen to fit them. In chapter 1 that means Dragon Quest I's grey castle brick
for every wall (the castle inside and out, the towns' buildings), and Dragon Quest III for the
ground, the plants, the water, the furniture, the people and the overworld (single trees, the
throne rooms' white and blue tiles and the barrier floor are Dragon Quest I's too: it has the very
things). The heroes are Dragon Quest III's classes as they walk in its party: Ragnar its soldier, Alena
its fighter, Cristo its priest, Brey its mage, Taloon its merchant. The scenes pass only some thirty
of Dragon Quest III's characters, so the others are drawn by the game itself where `extract.py` asks
for them (`DQ3_PEOPLE`: the dog, the cat, the horse, the slime, the nun, the wizard, the man in the
striped shirt, a flame, a boulder, ...): in a copy of the ROM, deleted again afterwards, the people
on the ground floor of Luisa's Place are given a character's number and the four facings, and the
floor is loaded from the first scene's state upstairs. `sfcfig.py buildn` cuts those captures into
`dq3_people.figs`, a set per number with all eight pictures, whatever size the game draws it
(a map file names the set by the number in decimal: `dq3n:g90` is the dog). The caves' rock is Dragon
Quest III's boulder wall in each cave's own color, and the tower's walls its dressed stone. Where the right thing
exists only in another place's colors, the map file tones it (`gray`, `dark`, `hue`): Dragon Quest
III's round towers, gate and braziers stand in the brick's grey on Burland's castle front.

A thing that stands on a floor (a stool, a table, a pot, a statue) is its upper layer alone, laid over
the ground the place itself has (`=town:KEY over ...`): the NES game draws one square for a town's
road, a castle's floor and a room's floor, and the stool stands on each. A thing the Super Famicom
games have no picture of keeps its NES shape in their colors, laid over their real ground
(`over ~... clear ...`), until it is redrawn: signposts, the stepped rim of Tempe's altar, the great
doors, the casino's slot machines.

`maps/chapter1.map` pairs Burland, Izmit and the places between them; `maps/chapter2.map` what
Santeem, Surene, Tempe, Frenor, the Birdsong Tower, the shrine of the travel door and Endor add;
`maps/chapter3.map` Lakanaba, Foxville, Bonmalmo and the two caves near them; `maps/chapter4.map`
Monbaraba with its theater, Kievs, the cave west of it, Aktemto and its mine, Haville with its ship
and Keeleon's castle; `maps/chapter5.map` the hero and what the towns still lacked
(a line for any kind of map serves every place that has the square). `scenes/dw4_places.txt`
lists every place of the game; `extract.py` dumps them all and writes one inventory for each kind
of map (`generated/sfc/nes_kXX.json`). (A level can be two maps side by side, the second loaded as the
player walks into it: a direction after the level's number has it dumped again from there.)

## Adding a map

1. Get the game to the map and dump it: `nes-headless --rom ... --input SCRIPT --frames N --map-dump x.map`,
   then `nesmap.py squares` / `figures` for numbered sheets of what it holds. The game need not be played to
   every map: it keeps the place it is to load at `$63` and that place's level at `$64`, and `--poke` puts
   other numbers there while it changes maps (`extract.py` lists chapter 1's that way: `NES_WARPS`).
   Another chapter's people need that chapter's game: it starts by itself when its number (`$615A`, counted
   from 0) is held while a new quest's adventure log is continued (`--sav`), title card, opening and party
   included. `extract.py` does that for chapters 2 to 5 (`CH2_STEPS` ... `CH5_STEPS`) and walks their parties
   to the same door, so their places are dumped with their own people: `generated/sfc/nes_people_ch2.json`,
   `nes_people_ch3.json`. (Chapter 3's opening ends with Taloon in bed and waits for a direction to be pressed.)
2. `sfclib.py sheet` / `sfcfig.py sheet` show the Super Famicom libraries the same way.
3. Add lines to a map file (see the head of `pack.py` for the forms) and build the pack.
4. `nes-headless --sfc-check --sfc-pack sfc.pak` plays a script or random input and reports every
   square and person the pack lacks; `--sfc FILE` / `--sfc-wide FILE` write pictures.
5. `pack.py freeze FILE` rewrites sheet numbers as keys, which stay the same when the libraries
   are built again.

Everything these tools write (`generated/sfc`, `*.pak`) is ignored by git: it holds the games' art.
