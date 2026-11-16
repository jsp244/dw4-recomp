"""Cosimulation check: run the same session through the interpreter and through
the recompiled code and compare per-frame state hashes.

usage: python tests/cosim.py [--frames N] [--keys SPEC] [--fuzz SEED] [--fuzz-start N] [--rom FILE]

The default session is scripted up to the start of chapter 1 (title, a new
adventure log, a name, the message speed, the King's speech), then played at
random from frame 5000 on: walking, talking, menus (nes-headless --fuzz).

Exit code 0 means every frame matched (CPU registers and cycle count, RAM,
save RAM, framebuffer, OAM, palettes, nametables, bank registers).
"""
import argparse
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = ".exe" if os.name == "nt" else ""


def taps(start, button, count, step):
    return ";".join(f"{start + i * step}:{button};{start + i * step + 6}:-" for i in range(count))


# Title -> BEGIN A NEW QUEST -> adventure log 1 -> the name "AA" (two letters, across to K, down to
# END) -> Male -> message speed -> the chapter's opening text and the King's speech.
DEFAULT_KEYS = ";".join([
    "1600:START;1606:-;1910:A;1916:-;2120:A;2126:-;2330:A;2336:-;2360:A;2366:-",
    taps(2390, "RIGHT", 10, 20), taps(2590, "DOWN", 5, 20),
    "2690:A;2696:-;2840:A;2846:-;2990:A;2996:-;3140:A;3146:-",
    taps(3370, "A", 12, 66),
])


def run(exe, rom, frames, keys, out, extra):
    cmd = [exe, "--rom", rom, "--frames", str(frames), "--keys", keys, "--frame-hash", out, "--quiet"] + extra
    p = subprocess.run(cmd, capture_output=True, text=True)
    if p.returncode != 0:
        print(p.stdout, p.stderr)
        raise SystemExit(f"{exe} failed")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=12000)
    ap.add_argument("--keys", default=DEFAULT_KEYS)
    ap.add_argument("--fuzz", type=int, default=1, help="seed of the random play (0 = none)")
    ap.add_argument("--fuzz-start", type=int, default=5000)
    ap.add_argument("--rom", default=None)
    args = ap.parse_args()
    rom = args.rom or (glob.glob(os.path.join(ROOT, "rom", "*.nes")) or [None])[0]
    if not rom:
        raise SystemExit("no ROM: put your .nes file in rom/ or pass --rom")
    interp = os.path.join(ROOT, "build", "nes-headless" + EXE)
    native = os.path.join(ROOT, "build", "nes-headless-recomp" + EXE)
    for exe in (interp, native):
        if not os.path.exists(exe):
            raise SystemExit(f"missing {exe}: run 'make headless headless-recomp' first")
    extra = ["--fuzz", str(args.fuzz), "--fuzz-start", str(args.fuzz_start)] if args.fuzz else []
    h1 = os.path.join(ROOT, "build", "cosim_interp.txt")
    h2 = os.path.join(ROOT, "build", "cosim_native.txt")
    run(interp, rom, args.frames, args.keys, h1, extra)
    run(native, rom, args.frames, args.keys, h2, extra + ["--recomp"])
    a = open(h1).read().splitlines()
    b = open(h2).read().splitlines()
    bad = [i for i, (x, y) in enumerate(zip(a, b)) if x != y]
    n = min(len(a), len(b))
    if bad:
        print(f"MISMATCH: first divergence at frame {bad[0]} ({len(bad)} of {n} frames differ)")
        print("Build a tracing harness to pinpoint it:")
        print("  make BUILD=build-trace RECOMP_DEFS=-DRECOMP_TRACE headless-recomp")
        print("  then run both harnesses with --trace FILE --trace-start <frame> and diff the traces")
        sys.exit(1)
    print(f"OK: {n} frames bit-identical between interpreter and recompiled code")


if __name__ == "__main__":
    main()
