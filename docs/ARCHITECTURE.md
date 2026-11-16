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
| Cartridge | `runtime/cart.c`, `mapper.c` | iNES loader, hashes, MMC1 (SUROM), battery RAM |
| System | `runtime/sys.c`, `mem.c` | bus, controllers, frame loop, save states |
| Remaster | `runtime/remaster.c` | the recolored, shaded picture |
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

## Remastered graphics

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
- Widescreen: needs the map and scroll formats decoded, to draw what lies beyond the
  screen edges.
- More coverage for `entries.txt` from later chapters.
