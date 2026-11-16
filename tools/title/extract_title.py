"""Take the two faces of the title screen out of your own dumps, for the "Title screen" option
(runtime/title.c): the logo of the Japanese release, and the engraved plaque of the North
American one.

usage: python tools/title/extract_title.py "Dragon Quest IV (Japan).nes" ["Dragon Warrior IV (USA).nes"]

The Japanese cartridge is the same game on the same board, so the interpreter of this port runs
it: the tool plays each opening until the title stands still, takes the picture (as the colour
numbers the console would send to the TV) and writes generated/title_dq.h (the logo: the letters
and the numeral behind them) and, with the second ROM, generated/title_dw.h (the finished
plaque). Like everything in generated/, those files are made from ROMs, belong to whoever owns
the cartridges, and are not committed. A build without them simply has no such option.
"""
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = ".exe" if os.name == "nt" else ""


def picture(rom, frames):
    """the frame buffer (colour numbers) and the backdrop colour after so many frames without input"""
    harness = os.path.join(ROOT, "build", "nes-headless" + EXE)
    if not os.path.exists(harness):
        raise SystemExit("build/nes-headless is missing: run 'make headless' first")
    dump = os.path.join(ROOT, "build", "title.ppu")
    subprocess.run([harness, "--rom", rom, "--frames", str(frames), "--ppu-dump", dump, "--quiet"], check=True)
    d = open(dump, "rb").read()
    os.remove(dump)
    if d[:4] != b"DWPP":
        raise SystemExit("unexpected dump")
    return d[16 + 32 + 4096 + 8192 + 256:], d[16] & 0x3F


def write(name, prefix, comment, x0, y0, w, h, flat):
    """runs of (count, colour); 0xFF = nothing drawn"""
    out = []
    i = 0
    while i < len(flat):
        j = i
        while j < len(flat) and flat[j] == flat[i] and j - i < 255:
            j += 1
        out += [j - i, flat[i]]
        i = j
    path = os.path.join(ROOT, "generated", name)
    with open(path, "w", newline="\n") as f:
        f.write(f"/* {comment} (tools/title/extract_title.py).\n"
                "   Generated - do not commit. Runs of (count, colour number); 0xFF: nothing drawn. */\n")
        f.write(f"#define {prefix.upper()}_X {x0}\n#define {prefix.upper()}_Y {y0}\n#define {prefix.upper()}_W {w}\n#define {prefix.upper()}_H {h}\n")
        f.write(f"static const unsigned char {prefix}_runs[] = {{\n")
        for k in range(0, len(out), 24):
            f.write("    " + ",".join(str(v) for v in out[k:k + 24]) + ",\n")
        f.write("};\n")
    print(f"wrote {path}: {w} x {h} pixels, {len(out)} bytes")


def main():
    if len(sys.argv) not in (2, 3):
        raise SystemExit(__doc__)
    # the logo: where one can be (the plaque's area, and a little wider), what is not sky
    x0, y0, x1, y1 = 32, 32, 232, 112
    fb, sky = picture(sys.argv[1], 3000)            # the logo has stood still for some ten seconds by then
    rows = [[fb[y * 256 + x] & 0x3F for x in range(x0, x1)] for y in range(y0, y1)]
    ink = [(x, y) for y, r in enumerate(rows) for x, c in enumerate(r) if c != sky]
    if len(ink) < 2000:
        raise SystemExit("no logo on the screen after the opening: is this the Japanese release of Dragon Quest IV?")
    w = max(x for x, _ in ink) + 1
    h = max(y for _, y in ink) + 1
    write("title_dq.h", "title_dq", "The title logo of the Japanese release, from the user's dump of it", x0, y0, w, h,
          [(c if c != sky else 0xFF) for r in rows[:h] for c in r[:w]])
    if len(sys.argv) == 3:
        # the plaque: 24 x 10 tiles at column 4, row 4, once the engraving is finished
        x0, y0, w, h = 32, 32, 192, 80
        fb, sky = picture(sys.argv[2], 6000)
        flat = [fb[(y0 + y) * 256 + x0 + x] & 0x3F for y in range(h) for x in range(w)]
        if sum(c != sky for c in flat) < w * h * 9 // 10:
            raise SystemExit("no plaque on the screen after the opening: is this Dragon Warrior IV (USA)?")
        write("title_dw.h", "title_dw", "The finished title plaque of the North American release, from the user's dump of it", x0, y0, w, h, flat)


if __name__ == "__main__":
    main()
