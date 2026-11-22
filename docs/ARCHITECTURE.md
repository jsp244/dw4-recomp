# Architecture

## Goals and constraints

- A native program that plays Dragon Warrior IV (NES, USA) from the user's own dump.
- No game data in the repository: the translated game is generated locally from the ROM
  (`generated/`, ignored by git) and the program refuses a ROM whose hash it does not know.
- The translated code must behave exactly like the cartridge on the hardware. It is
  checked against an interpreter of the same machine, frame by frame.

## Components

| Part | Files | Role |
|---|---|---|
| Recompiler | `recomp/recomp.py`, `symbols.txt`, `entries.txt` | ROM -> C, one file per PRG bank |
| CPU | `runtime/nes.h`, `recomp_rt.h`, `cpu.c` | instruction semantics shared by the translated code and the interpreter |
| PPU | `runtime/ppu.c` | dot-by-dot picture, sprite evaluation, per-pixel source capture for the remaster |
| APU | `runtime/apu.c` | the five sound channels, 48 kHz output |
| 16-bit music | `runtime/sfcmusic.c`, `sfcdsp.c` | the game's notes on the art pack's Super Famicom instruments (the S-DSP is LakeSnes's) |
| Cartridge | `runtime/cart.c`, `mapper.c` | iNES loader, hashes, MMC1 (SUROM), battery RAM |
| System | `runtime/sys.c`, `mem.c` | bus, controllers, frame loop, save states |
| Remaster | `runtime/remaster.c` | the recolored, shaded picture |
| Widescreen | `runtime/widescreen.c` | the map and the people beyond the screen's edges |
| Harness | `runtime/headless.c` | scripted and random play, screenshots, traces, coverage, cosimulation |
| Frontend | `frontend/` | SDL2 program: launcher, pacing, audio, input, rewind, achievements, updater |

## The cartridge

Dragon Warrior IV is an SUROM board: MMC1, 512 KiB of PRG ROM, 8 KiB of CHR RAM, 8 KiB
of battery RAM.

- **Banks.** The MMC1 addresses 256 KiB of PRG, so the board takes the top address line
  from bit 4 of the CHR bank register. The ROM is two halves of sixteen 16 KiB banks.
  The switchable bank sits at `$8000`; the last bank of the selected half is fixed at
  `$C000`: bank 15 or bank 31. The two fixed banks are different code with the shared
  routines at the same addresses, because switching half replaces the code that is
  running.
- **Bank switching.** `$FF91` in both fixed banks takes a bank number 0-31 in A, keeps it
  at `$0507`, and writes the low four bits to the PRG register and bit 4 through the CHR
  register (five serial writes each).
- **Reset.** Every bank ends with the same stub (`SEI`, `INC $FFDF`, `JMP`), since the
  MMC1 can power up with any bank at `$C000`. `INC` on ROM writes twice on consecutive
  cycles; the MMC1 ignores the second write, and `mapper.c` does the same (both writes
  of one instruction carry the same cycle count).
- **NMI.** The vector is `$0502`, in RAM, where the game keeps a `JMP` to the handler
  (`$C15A`, the address stored at `$C0D2`).
- **Far calls.** The IRQ/BRK vector (`$C408`) is a far-call dispatcher. A routine in
  another bank is called with three bytes:

  ```
  BRK, routine, bank code
  ```

  The handler reads the two bytes after the `BRK`, works out the bank
  (`((code & 8) << 1) | (code >> 4)`), switches to it, and jumps through the table of
  routine addresses the bank starts with at `$8000` (via a `JMP` it builds at `$0023`),
  then switches back and returns after the two bytes. Variants, chosen by the low
  nibble of the bank code: `$x3` calls routines of bank 16 or 18 numbered by the high
  nibble; `$xB` calls one routine of bank 22 or 25, or with `$CB`/`$DB`/`$EB` tests,
  sets or clears a bit of the game's flags at `$627B` using a mask byte that follows;
  a routine number with bit 7 set only fetches the table entry into the zero page.
  Some routines take one more parameter byte after the call and step over it
  themselves.
- **Graphics.** There is no CHR ROM. Tiles are copied from the program ROM into CHR RAM
  as scenes need them.
- **Battery RAM.** `$6000-$7FFF` holds the adventure logs and the party's state
  (`$6000-$6DFF`) and working memory (`$6E00-$7FFF`). All 8 KiB go to the `.sav` file;
  the frontend watches the first part to decide when to write it.

## Recompiler

### Discovery

One unit per 16 KiB bank: banks 0-14 and 16-30 at `$8000`, banks 15 and 31 at `$C000`.
Instructions are found by recursive descent from:

1. the reset and IRQ vectors of both fixed banks, and the NMI handler named by the
   `codeptr` lines of `symbols.txt`;
2. far calls: each `BRK` is decoded with its two (or three) bytes, the target is looked
   up in the named bank's table, and decoding resumes after the bytes;
3. each code bank's routine table at `$8000`;
4. calls from a fixed bank into `$8000-$BFFF`: the bank is the one loaded just before
   (`LDA #bank` / `JSR $FF91`), followed in address order through the routine;
5. `entries.txt`: instructions reached only through jump tables or addresses pushed for
   `RTS`, and the far routines that take a parameter byte. The file is written by
   `recomp.py --coverage ... --write-entries` from coverage recorded by the harness
   with the interpreter; it holds addresses only.

A jump or call within a bank stays in its unit; one into `$C000-$FFFF` goes to the fixed
bank of the same half. Decoding data as code by mistake is harmless: an instruction is
only ever executed at an address the CPU really reaches, and it does what the bytes
there say.

For the USA ROM this finds about 64,000 instructions in 27 banks.

### Emission

Each unit becomes a C file. Connected instructions form a routine function (at most
1,200 instructions, so the compiler copes), and every instruction is a `case` of a
`switch (cpu.pc)`:

```c
case 0xFF91: L_FF91: INSN(0xFF91); /* 8D 07 05  STA $0507 <SwitchBank> */
    CYC(4); ram[0x507] = A; SYNC(0xFF94);
```

`CYC` adds the instruction's cycles; `SYNC` runs the PPU and APU up to the CPU's clock
and leaves the function if an interrupt or the end of the frame is due; `EXIT` leaves
for a control transfer the dispatcher resolves (a call, a return, an indirect jump, or
any write to `$8000-$FFFF`, since it may have switched banks). `recomp_run` looks up the
unit of the bank currently mapped at `cpu.pc` and the routine for that address. If there
is none (code in RAM, or an address the recompiler never found) the interpreter executes
one instruction and the dispatcher tries again. The game's two `JMP` stubs in RAM
(`$0023`, `$0502`) always run this way.

### Cosimulation

`nes-headless` runs the interpreter, `nes-headless-recomp --recomp` the translated code.
With `--frame-hash` each writes a hash per frame of the CPU registers and cycle count,
RAM, battery RAM, picture, OAM, palettes, nametables and bank numbers; `tests/cosim.py`
compares the two. `--fuzz SEED` plays at random (walking, A, B, now and then Start or
Select), the same way for the same seed, which reaches towns, the overworld, battles
and menus without a script. A mismatch is pinned down with traces:

```sh
make BUILD=build-trace RECOMP_DEFS=-DRECOMP_TRACE headless-recomp
# run both harnesses with --trace FILE --trace-start N and diff
```

## Timing

The CPU clock is the master. After every instruction the PPU is advanced to three dots
per CPU cycle and the APU to the same cycle, so register reads and writes land on the
right dot, and NMI is taken at the instruction boundary the hardware would take it.
Known gaps are the ones the public test ROMs show (`make test`: 44 of the 63 it runs
pass, and nestest's 8,991 instructions match). The failures are fine points of interrupt
and NMI timing, PPU open bus and the read buffer, the DMC channel, APU reset, running
code from I/O registers, unofficial opcodes' dummy reads, and the MMC3 tests' scanline
timing (not this game's mapper).

## Pixel Remaster graphics

With capture on, the PPU records for every pixel of each layer which tile it came from
(its slot in the CHR RAM), which pixel of the tile, the palette and the value.
`remaster.c` gives every tile 64 "inks": a color of the tile's palette and a step on
that color's ramp (seven steps and the half steps, built in OKLCH, darker steps turning
toward blue and lighter ones toward yellow). Because an ink names a palette entry and
not a color, fades and palette changes carry through.

Tiles live in RAM, so inks belong to a slot's contents: before a frame is colored, every
slot whose 16 bytes changed gets its inks again, from the pack if it has the tile (by
the 64-bit FNV-1a hash of the bytes), else from the automatic rules: a pixel inside an
area of one color is lit where the area's edge faces up or left and shaded where it
faces down or right; speckled textures and sprite outlines keep their color; lettering
(two colors, thin strokes, at most three shapes) stays flat.

`nes-headless --hd-dump` writes a frame's sources and the CHR RAM;
`tools/remaster/remaster_art.py` turns art painted over such frames into a pack.

## Widescreen

The canvas is 352x240: the PPU's picture with a 48-pixel band on each side (16:9 shows 32
of them, 16:10 shows 16).

- **The map.** Bank 22's `$8DD4` answers, for a column (`$0D`) and row (`$03E1`) counted
  around the player, the four tiles (`$00-$03`) and the palette (`$04`) of that map square,
  through whichever of the game's map readers the current map uses. `widescreen.c` runs it
  for the squares of the bands inside a sandbox (`nes_sandbox_*` in `sys.c`: CPU, RAM,
  battery RAM and mapper saved, the routine run in the interpreter with no PPU or APU time
  passing and their registers cut off, everything restored). Answers are kept by map square
  and asked again an eighth at a time, about 25 calls a frame.
- **Position.** The camera's square is `$EC`/`$ED`; the pixels within it come from the
  PPU's scroll at the start of the frame. The game's memory is one frame ahead of the
  picture, so at the end of a step the neighbouring camera squares are tried too.
- **Is it the map?** The routine's answers for four columns of the screen are compared with
  the nametables. At least a third must agree, and some of those must have something drawn
  in them (before a map is loaded the routine answers with empty squares, which agree with
  any empty screen). Otherwise the bands are black.
- **People.** The object table (30 entries from `$6FA0`: position in sixteenths of a
  pixel, kind, facing, flags, floor) gives everyone on the map; those in the bands are
  drawn with the tiles of the game's sprite definitions at `$7200`.
- **The left strip.** The game has the PPU blank the first eight pixels of every line;
  the canvas fills them from the map as well.

### The canvas, Integer fill and the ring

The canvas is the PPU's picture with 64 pixels of map at each side and 16 lines above and
below (`WS_BAND`, `WS_VBAND`); the frontend shows the part of it that the chosen shape and
scaling call for. With Integer scaling on a fixed screen that is every line and column the
output holds at the whole multiple, so the map reaches the screen's edges.

Inside the PPU's own area the outer ring of map squares is taken from the map as well
(`widescreen_inner`). The game keeps no spare row or column in its name tables: while the
screen scrolls it rewrites the row and the column that wrap around, and they appear
half-written at an edge for several frames of each step. The squares in question are
column 0 and 16 and row 0 and 15 of the camera's grid; menus and message windows stay
clear of them (`nes-headless --ws-check` counts the frames at rest in which the first
column or row differs from the map: about one in a thousand, scene changes).

## Patches

`runtime/patch.c` applies IPS files to the PRG image in memory. The dump is kept; the image
running is always "the dump plus the files that are on, in order", so the set can change
freely. Of the 16 header bytes only the mapper number is taken: a hack may move the game to
another board, and MMC1 to MMC3 (512 KiB of PRG, CHR RAM) is the move the runtime can follow.

Translated code and patches. `recomp_patch` gets a map of the PRG bytes that differ from
the dump and switches off every translated routine with one of them in it (an instruction
is at most four bytes long, so the routines whose instructions start at a changed byte or
up to three before it). Those run in the interpreter from the patched image; so does code
a patch adds, since no translated routine exists for it. Translated routines never call
each other directly (every JSR, RTS and jump out of a routine goes back to the dispatcher)
and read ROM data through the memory map, so an untouched routine behaves the same in a
patched game. The dispatcher also checks that both halves of a routine's 16 KiB bank are
mapped where they were translated; an MMC3 can map them apart.

Switching while the game runs. The new image is built on the side (`patch_prepare`) and
put in place by `patch_try` at a moment when:

- every return address on the stack leads back behind a call that is the same bytes in
  both images. The runtime notes, per stack slot, what pushed the address in it (JSR, BRK,
  interrupt, or a plain push) and the 8 KiB bank it pointed into (`stk_kind`, `stk_bank`,
  kept in save states). For a JSR or a BRK (the game's calls into other banks) the call
  instruction itself must be unchanged: then the new image makes the same call from the
  same place and has its own continuation behind it. For an interrupt the instruction to
  be resumed must be unchanged;
- the program counter stands at the start of a routine just called, behind such a call, or
  on unchanged bytes;
- when the board changes, the PRG map in force is one both boards can show (an MMC1 has a
  fixed bank per 256 KiB half, an MMC3 only the last 16 KiB), and the mapper is turned into
  the other kind with the same map, mirroring and RAM enable.

A hack on another board has to start with the game: it sets itself up in its start-up code
(the one at hand copies its own version of the game's far-call routine into the cartridge
RAM), and nothing in the image tells what a running game would be missing. `patch.c`
therefore keeps the signature of the image whose start-up code ran last, at power-on or
reset (it is part of a saved state), and lets a board-changing image in while the game runs
only if it is that image coming back; otherwise the switch waits for a power-on.

The frame loop tries after every JSR, RTS and RTI, the same points for interpreter and
translated code, so both make the switch at the same instruction and stay comparable
(`tests/cosim.py --ips-toggle`). Addresses the game pushes by hand (its jump tables) count
as data; a build with `-DSTK_AUDIT` counts returns through such an address that had a call
or return between the push and the return, and finds none.

Built-in hacks. `tools/hacks/embed_hacks.py` turns the patch files listed in
`tools/hacks/hacks.txt` (name, version, author, year, page, SHA-1) into `generated/hacks.h`;
`runtime/hacks.c` exposes them, and `patch_prepare_list` takes a patch from memory as well as
from a file. A build with hacks built in offers exactly those (a row names the hack and its
author); a build without them lists the `.ips` files of the patches folder and links to the
four hacks' pages.

The frontend orders the patches: a whole-game hack first, then the small hacks it knows,
then add-ons and the rest by name; a small hack that writes bytes a whole-game hack also writes is
taken to be part of it and left out. `patch_inspect` recognises the hacks by the places
they change, not by file hashes, so other versions are recognised too.

## Title screen

`runtime/title.c` recognises the North American title scene by the stone plaque's frame
(its tiles in the name table and a hash of their patterns) and the Japanese one by the
patterns of the logo's tiles, and draws the sky and the other picture over it in the
finished frame: the logo over the plaque, or the plaque over the logo that a hack brings
along. The PPU's pixels are saved and put back after the frame is converted, so the PPU's
picture stays its own and the option shows at once behind the paused menu. The pictures
are run-length lists of colour numbers in `generated/title_dq.h` and `title_dw.h`, made by
`tools/title/extract_title.py` from dumps of the two cartridges, which the interpreter
runs as they are (same board).

## Frontend

The SDL2 program loads the ROM, checks its hash (the translated code is used only with
the ROM it was made from), and drives `nes_run_frame`. Pacing locks to the display's
vblank where it can, and dynamic audio rate control keeps the sound queue short. Run-ahead saves the
state, runs extra frames and shows the last; the game shows a response one frame after
it reads the pad at the soonest, so the default is 1. Rewind keeps XOR-delta states in a
ring; Start + Select opens the menu by rolling back to before the first of the two
presses. `frontend/update.c` checks GitHub releases and installs a newer package after
verifying its size and SHA-256.

The same frontend builds for Linux, for the Steam Deck (`DECK_EDITION=1`, or the Deck
recognised at run time: fullscreen, the pad drives the menus, L2 + R2 opens the menu) and
for Android, where it is `libmain.so` under SDL's activity (`android/`): the ROM comes
through the system file picker, HTTP goes through the activity, pacing follows the
compositor's frame timeline, and the phone edition draws a touch controller
(`frontend/touchpad.c`).

## Where to take it next

- Hand-made tile packs for the remaster (kept with one's own copy of the game).
- More coverage for `entries.txt` from later chapters.
