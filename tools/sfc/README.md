# The SNES Remaster's art

Graphics > Textures > **SNES Remaster** draws the game's map squares and people with the art of the
Super Famicom Dragon Quest games. That art is not in the packages: it is taken from your own
cartridge dump into a pack, `sfc.pak`, that the game loads from its folder. Without the pack the
choice is not offered. (This repository holds nineteen squares, thirteen figures, fifteen battle monsters and one painting of it, locked to the ROM
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
door). Four figures are Dragon Quest V's as well: the little ship its sea shows (at its moorings
in every port, and under sail), the sky people of the tower on the clouds, in robes and in
tunics, who wear their wings at their heads there as they do here, and Keeleon (its lion-headed
beast, the blue mane turned brown and the gold salmon, as the NES colors him). Nine figures are Dragon Quest VI's: the party's wagon, the ox (its cow, turned brown), the
fox (none of the games has one: its dog, turned fox-red), the demon at the top of chapter 1's
tower, the wild-haired brutes of chapter 5 (its ogre, turned purple), the stone guards of chapter 5
(its stone giant), the
young dragons of the tower on the clouds (its small dragon, turned salmon), the great dragon they
guard (the golden dragon of its opening) and the giant on his throne (the demon lord of that
opening, who sits there in shadow: his greys turned to the giant's flesh). The clouds one walks on
in chapter 5 are that opening's as well: the painting of cloud its dragon flies through, a night
sky there, turned to the NES cloud's white and pale blue. It is one picture laid over the whole
map (`picture`, `over %NAME` in a map file), and the NES's cloud squares give it their outline.
They come from the supplement,
`tools/sfc/supplement.bin`: just those pictures, combined with
a stream made from the Dragon Quest III ROM's contents, so that the file opens only with that very
ROM (`supplement.py`). If you would rather take them from your own cartridges, give those games
too: `--dq12 "Dragon Quest I & II (Japan).sfc"`, `--dq5 "Dragon Quest V (Japan).sfc"` (Dragon Quest V
is then played from power-on onto its ship's deck, 12,814 frames, and once to its deckhouse door,
10,766 frames, from where the game is asked to draw each wanted character in the place of a sailor
on deck), `--dq6 "Dragon Quest VI
(Japan).sfc"` (played to the cliff of its opening, 5,596 frames, where the game is asked to draw
each wanted character in place of the man who waits there, and once on from there, 12,781 frames,
past the great dragon and through the clouds to the demon lord on his throne). Whoever changes the map files
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

The windows are Dragon Quest III's too. `extract.py` takes a picture of its screen while the
opening's first message stands there (these games set their text layer up for part of the frame
only, so a capture's memory does not show a window as the screen does); `sfcfig.py buildwindow`
cuts the frame's corners and edges and the two colors from it, and one line of
`maps/chapter1.map` (`window`) puts them into the pack. The game then draws every window with
them: the frame (the same two-pixel line in the same place on both machines), the inside in
Dragon Quest III's grey, and the letters, which stay Dragon Warrior IV's own (the Super Famicom
games have capitals and digits but no lowercase), in the frame's white. The line's `frame KEY`
is the hash of the ten tiles the NES game keeps its frame in (`nesmap.py frame`): the title and
a few other screens load other pictures there, and windows are drawn this way only while those
tiles hold the frame.

`maps/chapter1.map` pairs Burland, Izmit and the places between them; `maps/chapter2.map` what
Santeem, Surene, Tempe, Frenor, the Birdsong Tower, the shrine of the travel door and Endor add;
`maps/chapter3.map` Lakanaba, Foxville, Bonmalmo and the two caves near them; `maps/chapter4.map`
Monbaraba with its theater, Kievs, the cave west of it, Aktemto and its mine, Haville with its ship
and Keeleon's castle; `maps/chapter5.map` the hero and what the towns still lacked
(a line for any kind of map serves every place that has the square). `scenes/dw4_places.txt`
lists every place of the game; `extract.py` dumps them all and writes one inventory for each kind
of map (`generated/sfc/nes_kXX.json`). (A level can be two maps side by side, the second loaded as the
player walks into it: a direction after the level's number has it dumped again from there.)

## The battles' monsters

The NES game draws its 194 monsters with 71 drawings: the rest are the same tiles in other
colours. Sixteen of those drawings are creatures Dragon Quest III or V draws too, in the same
pose and nearly the same size; `maps/monsters.map` gives the 46 monsters that use them a Super
Famicom picture (`mon NN FIGS:ART tones nes`). No Super Famicom game drew the other 55 drawings
(Dragon Quest IV never came out for it), and nothing is invented for them: those monsters stay
the NES's.

- **Each game draws any monster on request**, alone in a battle, when the numbers of an
  encounter's groups are held in memory while the battle is made up (`extract.py`, the comment
  above `NES_OVERWORLD`): Dragon Warrior IV's at `$6E45` (four monsters, then how many of each),
  Dragon Quest III's at work RAM `$2000`, Dragon Quest V's at `$1001`. Dragon Quest V is taken
  to a battle without playing its opening through: the byte that says which map is loaded
  (`$00BD`) is held at the overworld's number while the boy takes a staircase on the ship.
- **The NES inventory** (`nesmap.py monsters`, `generated/sfc/nes_monsters.json`): per monster
  the tiles of its drawing (where each lies from the first, its hash), its palettes' colours and
  the sprites laid over it. The Super Famicom games draw monsters with sprites: `sfcfig.py
  buildall` cuts one from a capture.
- **Colours.** The same creature is often another colour in another game (Esturk is brown in
  Dragon Quest V), and the NES game's own variants were never drawn at all. `tones nes` gives a
  picture the NES monster's colours: since the poses match, every colour of the picture is a
  shade of the NES colour drawn where it mostly lies; it takes that colour's hue and keeps its
  own light and dark. One picture so serves a whole family.
- **In the game** (`runtime/sfc.c`): a drawing is found in the name tables by its first tile's
  hash and confirmed by all its tiles; the variant is the one whose colours the palettes hold
  (the nearest while the game fades them). The NES tiles and the monster's own sprites are
  left out and the picture is drawn where the NES drawing stands, never over a window.

## The music

The pack also holds music: Dragon Quest III's instruments and, for each of the game's 54 tunes,
which of them plays which of the NES's three melodic channels (`runtime/sfcmusic.c` is the
player; the game is not changed, its own notes are followed frame by frame).

- **Instruments.** Dragon Quest III loads a tune's instruments into its sound memory when the
  tune starts. `extract.py` dumps that memory at a frame at which its town tune sounds and at
  one of its battle tune, which has the low brass (`sfc-capture --apu-dump FRAME PREFIX`: the
  sound CPU's 64 KiB and the sound chip's registers; `DQ3_MUSIC` names the scenes and the
  frames). `music.py samples DUMP` lists what a dump holds.
- **Arrangements.** `tools/sfc/music/dw4.arr` names the instruments (a dump, a sample number,
  the pitch the sample sounds at, a checksum of its bytes) and scores the tunes: a line per
  part (channel, instrument, envelope, volume left and right, and how much of the game's own
  volume envelope the instrument follows). A part may take only the notes below a pitch, or
  from it up: the chip plays a sample at most two octaves above its own note, so a bass line
  that climbs out of the double basses' reach passes to the next section, and a pedal note
  can be the timpani's while the rest of the line is not. The head of `music.py` describes
  every word.
  `music.py build` compiles it; `pack.py build OUT --music MUSIC.bin MAPS...` puts it at the
  pack's end.
- **Which tune is which.** The game's sound driver keeps twice the tune's number at `$BF`.
  `nes-headless --apu-trace FILE` writes it for every frame (byte 20 of each 24) beside what
  the channels do, so a tune's number and its parts can be read from a run of the place it
  plays in. Where the game decides: the routine at `$803B` of its 16K bank 25 picks the
  tune of a place (a table of places with a tune of their own at `$8124`, else by the kind of
  place at `$811C`; the overworlds by chapter at `$8116`, in chapter 5 by who leads the
  party at `$810B`; ship and balloon by `$0515`); the battle's is chosen at `$A140` of bank
  24 and `$9053` of bank 17; everything else is a `BRK n $FB` in the code (play tune or sound
  n). The fades are the driver's too: it steps a master volume (the low half of `$B0`) while
  bit 6 of `$BB` is set, and the player scales the instruments by the same steps.
- **Listening.** `nes-headless --sfc-pack PACK --sfc-music --wav OUT.wav` writes what the game
  would sound like (stereo: the instruments, and what is left of the NES's sound);
  `--sfc-music-wav FILE` writes the sound chip's output alone.

Sound effects are not re-voiced. The game makes a few of them as tunes (a rumble, a thud, a
roll of low notes), and the far world of the last chapter has a drone with wind for a tune:
those are marked `nes` in the file and left alone, as are the noise channel and whatever a
sound effect takes of the second pulse channel while it runs.

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
