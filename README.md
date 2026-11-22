# Dragon Quest IV (Dragon Warrior IV) native PC port (static recompilation)

A native program for Windows, Linux, the Steam Deck and Android that plays Dragon Quest IV,
released in North America as Dragon Warrior IV (NES, USA), from your own cartridge dump. The game's 6502 code is translated ahead of time into C by a recompiler and linked
against a small hardware runtime (CPU helpers, a dot-accurate PPU, the APU and the
cartridge's MMC1 board), with a launcher, save states, rewind, low-latency video and
optional Pixel Remaster graphics, ROM hacks switched on and off as you play, and widescreen.

**No game data is in this repository.** Nothing here contains Dragon Warrior IV code,
graphics, music or text. The recompiler reads your `.nes` file and writes the translated
game into `generated/`, which git ignores; the program checks the ROM's hash before it
runs. Do not share your `.nes` file or anything built from it.

## What works

- The whole game runs as native code. The recompiled code is checked against the
  built-in interpreter frame by frame: identical CPU state, RAM, save RAM, picture,
  sprites, palettes and bank registers over scripted and random play (title, name entry,
  chapter 1's castle and town, the overworld, battles, menus).
- Battery saves (the three adventure logs) in `saves/<rom name>.sav`.
- Launcher with tabs for Play, Graphics, Audio, Controls, Achievements, ROM, Patches and Updates;
  the game's own title sequence plays behind it.
- Nine save-state slots, rewind, fast-forward, run-ahead, vblank-locked pacing, shaders
  (scanlines, CRT, glow), integer / fit / stretch scaling, keyboard and controller
  rebinding, RetroAchievements (through the vendored rcheevos library).
- Textures: Pixel Remaster, the game's tiles recolored and shaded as they are drawn, and a
  tool for hand-made tile packs (below); SNES Remaster, the map and the people on it drawn
  with the Super Famicom Dragon Quest games' art, from a pack built out of your own copies
  of those games (below).
- Widescreen: 16:9 and 16:10 pictures with more of the map, and the people on it, at both
  sides wherever the game is on a map (below).
- ROM hacks as IPS patches, switched on and off while the game runs (below).
- The title screen under either name: the Dragon Quest IV logo or the Dragon Warrior IV
  plaque (below).

Not there yet: a hand-made tile pack.

## Building (Windows, MSYS2 MinGW64)

Needs gcc, GNU make, Python 3 and SDL2 (`pacman -S mingw-w64-x86_64-gcc make
mingw-w64-x86_64-SDL2 python`). Put your dump in `rom/` (see `rom/README.md`), then from
the MSYS2 MinGW64 shell:

```sh
make recomp          # your ROM -> generated/*.c
make -j8 dw4         # build/dw4.exe: the game
make dist            # dist/dw4-win64.zip: the executable and the player notes
```

`build/dw4.exe` is a single file that needs only Windows' own DLLs. Start it, pick your
ROM on the ROM tab (or pass the file as an argument, or drop it on the window) and choose
Play > Start game. `docs/PLAYERS.md` is the player's guide.

Other targets: `make headless` (the test harness with the interpreter only),
`make headless-recomp` (the harness with the recompiled code), `make cosim`, `make test`.

### Linux and Steam Deck packages

`tools/linux/Dockerfile` is the build environment (Ubuntu 22.04, SDL2 built as a static
library that loads its video, audio and input backends at run time), so the binary needs
only glibc 2.34 or newer and no SDL installed. With `generated/` made and your ROM in
`rom/` for the tests:

```sh
docker build -t dw4-linux tools/linux
docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e DW4_VERSION -e DW4_REPO \
    -v "$PWD":/src -w /src dw4-linux sh tools/linux/build.sh
```

It builds both editions, runs the cosimulation and the frontend's self-tests offscreen,
and writes `dist/dw4-linux-x86_64.tar.gz` and `dist/dw4-steamdeck.tar.gz`. The Deck edition
is the same program with the Deck's defaults (fullscreen, the pad drives the menus,
L2 + R2 opens the menu) plus `add-to-steam.py`, which adds it to the Steam library with
artwork from SteamGridDB; `docs/STEAMDECK.md` is its guide.

### Android

`python android/build_apk.py` builds two APKs straight from the SDK tools, without Gradle
(needs the Android SDK with a platform, build-tools, an NDK and CMake, and a JDK):
`dist/dw4-android-handheld.apk` for handhelds with built-in controls and
`dist/dw4-android-phone.apk`, the same app with a translucent on-screen controller,
installed as "Dragon Warrior IV Touch". Both carry arm64 and x86_64 code. They are signed
with a key the script makes on first use in `~/.android/dw4-release.keystore`; keep it,
because an installed app only updates from the same key. `docs/ANDROID.md` is the guide.

## Pixel Remaster textures

Graphics > Textures > Pixel Remaster colors every pixel from an "ink" its tile gives it: one
of the tile's four palette colors, a few steps darker or lighter on a hue-shifted ramp.
Inks follow the palette, so fades, palette swaps and flashes still work. By default the
inks are worked out from each tile's own pattern (light from the upper left, flat
lettering, natural tones for the colors the game uses most).

The cartridge has no CHR ROM: the game copies tiles into 8 KiB of CHR RAM as scenes need
them. So a tile is known by its contents, and a hand-made pack (`remaster.pak` beside the
program) lists inks by the hash of a tile's 16 bytes. `tools/remaster/remaster_art.py`
makes packs from art painted over dumped frames:

```sh
build/nes-headless --rom rom/your.nes --frames 6000 --keys "..." --hd-dump frame.hd
python tools/remaster/remaster_art.py template frame.hd bg 96 64 32 32 tree.art
# edit tree.art: one color + step per pixel
python tools/remaster/remaster_art.py pack remaster.pak tree.art
```

A pack is drawn over the cartridge's tiles: keep it with your own copy of the game.

## SNES Remaster textures

Graphics > Textures > SNES Remaster draws the map's squares and the people on it with the
art of the Super Famicom Dragon Quest games: each 16x16 square of the NES map is replaced
whole by a square of that art, chosen to fit the material and the colors the NES game gave
the place. The art is not in the packages. `tools/sfc` takes it from your own dump of
Dragon Quest III (Japan, Super Famicom) into a pack, `sfc.pak`; the choice is offered when
that file lies beside the program (`tools/sfc/README.md` has the two commands). Nineteen
squares, thirteen figures and one painting are other games' (nine squares of Dragon Quest I & II, ten of
Dragon Quest V's ship, with that game's little ship for the sea, its sky people and its lion beast; Dragon Quest VI's wagon, its cow, its dog, four of its monsters, its great dragon, its demon lord and the clouds its dragon flies through): this
repository holds them in a supplement that opens only with the Dragon Quest III ROM, and
nothing else of the games' art. The pack covers every place of the game.
The windows (the commands, the messages, the battles, the shops) are Dragon Quest III's as
well: its grey inside and its frame, with the game's own English letters in that frame's
white. The few things those games have no picture of (the balloon, a white hen, a stone idol)
are drawn as Pixel Remaster draws them, and so are the monsters of the battles and the title.

## Widescreen

Graphics > Aspect ratio > 16:9 or 16:10 keeps the pixels' shape and adds picture at the
sides: more of the map, with the townspeople standing there. With Scaling: Integer on a
fixed screen (fullscreen, a handheld) the picture is shown at the largest whole multiple
and the map fills the rest of the screen on all four sides (1920x1080: four times, with 64
more pixels of map at each side and 23 more lines above and below). Battles, the title and
other screens that are not the map keep the normal picture between black bars.

The same data repairs the picture's edges. A four-way scrolling game on this hardware has
to rewrite a row and a column of its name tables while the screen moves, and for a few
frames of every step they show half-written at an edge of the picture (a TV's overscan
hid most of it). In a widescreen mode that outer ring of map squares is drawn from the
map instead of from the PPU, along with the eight pixels the PPU blanks at the left.

Nothing about the maps is stored or decoded by the port. The game has a routine that names
the tiles of any map square around the player (it repaints the map with it when a window
closes); the port runs that routine on the side for the squares beyond the screen's edges,
with the machine saved before and restored after, so the game itself is untouched (the
cosimulation hashes are the same with widescreen drawing every frame). The people come
from the game's own table of who stands where. `nes-headless --ws-check` asks the routine
for the squares that are on the screen and compares them with what the PPU was given.

## How it works

`recomp/recomp.py` finds the code in the 32 PRG banks and writes one C file per bank,
every instruction a `case` label inside a routine function, so execution can enter at
any instruction (after an interrupt, a bank switch, or a step of the interpreter). The
runtime keeps the PPU and APU in step with the CPU's cycle count after every
instruction, which is what makes the translated code interchangeable with the
interpreter.

What the recompiler knows about this game (`docs/ARCHITECTURE.md` has the details):

- an MMC1 SUROM board: 512 KiB of PRG in two halves, each with its own fixed bank
  (15 and 31) at `$C000`, selected through a bit of the CHR register;
- far calls: `BRK, routine, bank` goes through the interrupt handler and the table of
  routine addresses at the start of each code bank, so cross-bank calls are followed
  statically;
- `recomp/symbols.txt`: a handful of labels and the one pointer the code does not spell
  out (the NMI vector points into RAM);
- `recomp/entries.txt`: entry points reached only through jump tables, learnt from
  execution coverage. Addresses only.

Code the recompiler misses still runs correctly, through the interpreter.

## Testing

```sh
make headless headless-recomp
python tests/cosim.py                 # 12,000 frames, scripted then random play
python tests/cosim.py --frames 120000 --fuzz 7
python tests/cosim.py --ips hack.ips                  # the same check on a patched game
python tests/cosim.py --ips hack.ips --ips-toggle 7   # and with the patch switched off/on as it runs
sh tests/get_test_roms.sh && make test   # public CPU/PPU/APU test ROMs
build/dw4 rom/your.nes --play --audio-dump capture.wav --quit-after 2400
python tests/audio_check.py capture.wav   # every sound channel in what the program plays
```

`nes-headless --help` lists the harness options: scripted or random input, PNG and WAV
output, traces, coverage files, save states, the remastered picture.

To widen the recompiler's coverage after reaching new parts of the game, record coverage
with the interpreter and fold it into the entries file:

```sh
build/nes-headless --rom rom/your.nes --frames 200000 --keys "..." --fuzz 3 --coverage cov.bin
python recomp/recomp.py --rom rom/your.nes --out generated --coverage cov.bin --write-entries recomp/entries.txt
```

## Patches (ROM hacks)

The Patches tab lists ROM hacks, each On or Off. The ROM on disk is never changed: the
image the machine runs is the dump plus the patches that are on, rebuilt from the dump
whenever the set changes, so any of them can be taken out again.

Four hacks are built into the packages on the releases page, unchanged and under their
authors' names (a row reads "Delocalized by Translation Quest"), and that build offers exactly
these, the set that was tested together:

| Hack | Version | Author | Year | Page |
|---|---|---|---|---|
| Delocalized: a new translation from the Japanese, with fixes | 1.08 | Translation Quest (Chicken Knife, nejimakipiyo, AWildJuneAppeared, dattebayo) | 2026 | romhacking.net/hacks/9523 |
| Graphical Uncensoring | 1.0 | MetHy | 2024 | romhacking.net/hacks/8426 |
| Manual Tactics: command every party member yourself in chapter 5 | 1.1 | karmic | 2025 | romhacking.net/hacks/9221 |
| Seamonsters fix | 1.0 | Jegriva | 2017 | romhacking.net/hacks/3760 |

They are their authors' work and are not in this repository: `make hacks` builds them in
from the files named in `tools/hacks/hacks.txt` (checked against the hashes there), which
you download from the pages above into `rom/hacks/`. A build without them works from files
instead: it lists the `.ips` files of the `patches` folder next to the program (drop a file on
the window, or use "Add a patch file"), knows the four hacks by what they change, and links
to their pages.

The three small ones touch different bytes and combine freely, in any order. Delocalized
already contains all three (in its own adapted form), so with it on they are not applied a
second time. Delocalized also moves the game from
its MMC1 board to an MMC3, which the runtime follows.

How a patched game runs: the translated code was made from the dump, so every routine of
it with a patched byte is switched off and the interpreter runs that routine from the
patched image, along with any code a patch adds. `tests/cosim.py --ips FILE` checks a
patched game the same way as the plain one (interpreter against translated code, frame by
frame).

Switching while the game runs (the tab's switches, or its Compare row / F9: the original game
for the moment, and back) puts the new
image in place at a moment when the CPU cannot be caught in code that is no longer there.
The runtime notes what pushed each return address on the stack and from which bank; a
switch waits until the instruction the CPU stands on, and every call it still has to
return from, are the same bytes in both images, and, when the board changes, until the
memory map in force is one both boards can show. It looks for that moment after every call
and return inside the frame, so it usually takes a fraction of a frame.
`tests/cosim.py --ips FILE --ips-toggle 7` switches every 7 frames and still requires
interpreter and translated code to agree on every frame. What a patch changes is seen the
next time the game reads it: text when it is printed again, graphics when a scene loads.

One kind of hack cannot join a game that is already running: one that moves the game to
another board is a different program from the first instruction on and sets itself up when
the game starts. The game calls into its other half through a routine that its start-up
code copies into the cartridge RAM; the cartridge's copy drives the MMC1's serial port,
which on an MMC3 switches the cartridge RAM off, and Delocalized installs one of its own.
Such a hack goes in at a power-on: switched on while a game runs, it waits for the next
reset. In a game that was started with it, it can be switched out and back in while the
game runs like any other patch (its routine goes through the game's own bank switch and
serves the original code as well); the runtime keeps the image whose start-up code ran
last, at power-on or reset (`patch_needs_restart`), also in saved states.
`nes-headless --ips FILE --ips-late N` starts a game without its patches and switches them
on at frame N.

The adventure logs have the same layout under every selection, so the one `.sav` file
serves them all: a log saved under any selection continues under any other. The routine
in the cartridge RAM is part of that file and is written again by the start-up code of
whichever image starts the game.

RetroAchievements are off while a patch is on. A saved state holds the machine in the
middle of its code, so it belongs to the image it was saved from: loading one that was
saved under another set of patches puts that set's image back for the load (it is found
by the state's signature), and the set chosen now takes over through the same live switch
(or, if that set has to start with the game, the state keeps the patches it was saved
with). `dw4 --state-patch-test` saves and loads states across every pair of selections.

## Title screen

Graphics > Title screen chooses the name the game goes by: the Dragon Quest IV logo, as on
the Japanese cartridge, or the stone plaque being engraved with DRAGON WARRIOR IV. The
scene around it is the same in both releases, and the game runs the same either way: the
picture is changed after the PPU has drawn it (`runtime/title.c`), and the PPU's own
picture is put back afterwards, so the choice shows at once. It works both ways: with a
hack that brings the Japanese title along (Delocalized), choosing Dragon Warrior IV draws
the plaque over that logo.

The two pictures are not in this repository. `make title TITLE_ROM="<your dump of the
Japanese release>"` takes the logo from that cartridge and the finished plaque from the ROM
in `rom/` into `generated/` (197 x 65 and 192 x 80 pixels); a build without them has no
such option. The packages on the releases page are built with them.

## Layout

```
recomp/      the recompiler, symbols.txt, entries.txt
runtime/     CPU helpers and interpreter, PPU, APU, MMC1 and MMC3, save states, remaster, widescreen,
             patches, the title logo, test harness
frontend/    SDL2 program: launcher, video, audio, input, touch controls, rewind, achievements, updater
android/     the Android app: manifest, activity, CMake file, build_apk.py
tools/       icon generator, remaster pack tool, title logo tool, the list of built-in hacks, the Linux/Deck
             build, Deck and Android helpers
tests/       cosimulation, test-ROM runner, frontend tests
docs/        ARCHITECTURE.md, PLAYERS.md, STEAMDECK.md, ANDROID.md
rom/         your cartridge dump (ignored by git)
generated/   the translated game (ignored by git)
```

## Legal

Dragon Warrior and Dragon Quest belong to Square Enix. This is an unofficial fan
project; it runs only with a cartridge dump you provide. The MIT licence in `LICENSE`
covers the tooling and runtime in this repository only. The ROM hacks belong to their
authors (named above); the released packages carry them unchanged, the repository does not.
