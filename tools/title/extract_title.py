"""Take the title logo of the Japanese release out of your own dump of it, for the "Title screen:
Dragon Quest IV" option (runtime/title.c).

usage: python tools/title/extract_title.py "Dragon Quest IV (Japan).nes"

The Japanese cartridge is the same game on the same board, so the interpreter of this port runs
it: the tool plays its opening until the title stands still, takes the picture of the logo (the
letters and the numeral behind them, as the colour numbers the console would send to the TV) and
writes generated/title_dq.h. Like everything in generated/, that file is made from a ROM, belongs
to whoever owns the cartridge, and is not committed. A build without it simply has no such option.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = ".exe" if os.name == "nt" else ""
X0, Y0, X1, Y1 = 32, 32, 232, 112      # where a logo can be: the area of the North American plaque, and a little wider
FRAMES = 3000                          # the logo has stood still for some ten seconds by then


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    harness = os.path.join(ROOT, "build", "nes-headless" + EXE)
    if not os.path.exists(harness):
        raise SystemExit("build/nes-headless is missing: run 'make headless' first")
    dump = os.path.join(ROOT, "build", "title.ppu")
    subprocess.run([harness, "--rom", sys.argv[1], "--frames", str(FRAMES), "--ppu-dump", dump, "--quiet"], check=True)
    d = open(dump, "rb").read()
    os.remove(dump)
    if d[:4] != b"DWPP":
        raise SystemExit("unexpected dump")
    backdrop = d[16] & 0x3F
    fb = d[16 + 32 + 4096 + 8192 + 256:]
    rows = [[fb[y * 256 + x] & 0x3F for x in range(X0, X1)] for y in range(Y0, Y1)]
    ink = [(x, y) for y, r in enumerate(rows) for x, c in enumerate(r) if c != backdrop]
    if len(ink) < 2000:
        raise SystemExit("no logo on the screen after the opening: is this the Japanese release of Dragon Quest IV?")
    w = max(x for x, _ in ink) + 1
    h = max(y for _, y in ink) + 1
    # run lengths of (count, colour); 0xFF = the sky shows through
    out = []
    flat = [(c if c != backdrop else 0xFF) for r in rows[:h] for c in r[:w]]
    i = 0
    while i < len(flat):
        j = i
        while j < len(flat) and flat[j] == flat[i] and j - i < 255:
            j += 1
        out += [j - i, flat[i]]
        i = j
    path = os.path.join(ROOT, "generated", "title_dq.h")
    with open(path, "w", newline="\n") as f:
        f.write("/* The title logo of the Japanese release, from the user's dump of it (tools/title/extract_title.py).\n"
                "   Generated - do not commit. Runs of (count, colour number); 0xFF: nothing drawn. */\n")
        f.write(f"#define TITLE_DQ_X {X0}\n#define TITLE_DQ_Y {Y0}\n#define TITLE_DQ_W {w}\n#define TITLE_DQ_H {h}\n")
        f.write("static const unsigned char title_dq_runs[] = {\n")
        for k in range(0, len(out), 24):
            f.write("    " + ",".join(str(v) for v in out[k:k + 24]) + ",\n")
        f.write("};\n")
    print(f"wrote {path}: the logo is {w} x {h} pixels, {len(ink)} of them drawn, {len(out)} bytes")


if __name__ == "__main__":
    main()
