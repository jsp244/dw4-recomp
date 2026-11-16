# Dragon Warrior IV native PC port (static recompilation)

A native program for Windows, Linux, the Steam Deck and Android that plays Dragon Warrior IV
(NES, USA) from your own cartridge dump. The game's 6502 code is translated ahead of time into C by a recompiler and linked
against a small hardware runtime (CPU helpers, a dot-accurate PPU, the APU and the
cartridge's MMC1 board), with a launcher, save states, rewind, low-latency video and
optional remastered graphics.

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
- Launcher with tabs for Play, Graphics, Audio, Controls, Achievements, ROM and Updates;
  the game's own title sequence plays behind it.
- Nine save-state slots, rewind, fast-forward, run-ahead, vblank-locked pacing, shaders
  (scanlines, CRT, glow), integer / fit / stretch scaling, keyboard and controller
  rebinding, RetroAchievements (through the vendored rcheevos library).
- Textures: Remastered, the game's tiles recolored and shaded as they are drawn, and a
  tool for hand-made tile packs (below).
- Widescreen: 16:9 and 16:10 pictures with more of the map, and the people on it, at both
  sides wherever the game is on a map (below).

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

## Remastered textures

Graphics > Textures > Remastered colors every pixel from an "ink" its tile gives it: one
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

## Widescreen

Graphics > Aspect ratio > 16:9 or 16:10 keeps the pixels' shape and adds picture at the
sides: 32 or 16 more pixels of the map on each side, with the townspeople standing there.
Battles, the title and other screens that are not the map keep the normal picture between
black bars.

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

## Layout

```
recomp/      the recompiler, symbols.txt, entries.txt
runtime/     CPU helpers and interpreter, PPU, APU, MMC1, save states, remaster, widescreen, test harness
frontend/    SDL2 program: launcher, video, audio, input, touch controls, rewind, achievements, updater
android/     the Android app: manifest, activity, CMake file, build_apk.py
tools/       icon generator, remaster pack tool, the Linux/Deck build, Deck and Android helpers
tests/       cosimulation, test-ROM runner, frontend tests
docs/        ARCHITECTURE.md, PLAYERS.md, STEAMDECK.md, ANDROID.md
rom/         your cartridge dump (ignored by git)
generated/   the translated game (ignored by git)
```

## Legal

Dragon Warrior and Dragon Quest belong to Square Enix. This is an unofficial fan
project; it runs only with a cartridge dump you provide. The MIT licence in `LICENSE`
covers the tooling and runtime in this repository only.
