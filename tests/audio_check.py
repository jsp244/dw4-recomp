"""Is every sound channel in what a build hands to the audio device?

usage: python tests/audio_check.py CAPTURE.wav [CAPTURE.wav ...]

A capture is made by the game program itself:
    dw4 rom.nes --play --audio-dump capture.wav --quit-after 2400
(the first 40 seconds from power-on: the title music, which uses both pulse channels and the
triangle). The references come from the test harness for the same stretch: the full mix, and the
mix with pulse 1 left out. The channels add up in energy, so a capture passes when its energy is
nearer the full mix's than halfway to the mix without pulse 1, when it is as long as the stretch
(within 3 %), and when its two stereo channels are the same.
"""
import array
import glob
import math
import os
import subprocess
import sys
import wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = ".exe" if os.name == "nt" else ""
FRAMES = 2400


def load(path):
    w = wave.open(path)
    ch, rate, n = w.getnchannels(), w.getframerate(), w.getnframes()
    a = array.array("h", w.readframes(n))
    w.close()
    if sys.byteorder == "big":
        a.byteswap()
    return ch, rate, a


def rms(a):
    return math.sqrt(sum(x * x for x in a) / max(1, len(a)))


def reference(rom, mask):
    out = os.path.join(ROOT, "build", "audio_ref_%s.wav" % (mask or "all"))
    cmd = [os.path.join(ROOT, "build", "nes-headless" + EXE), "--rom", rom, "--frames", str(FRAMES), "--wav", out, "--quiet"]
    if mask:
        cmd += ["--mute", mask]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)
    return rms(load(out)[2])


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    rom = (glob.glob(os.path.join(ROOT, "rom", "*.nes")) or [None])[0]
    if not rom:
        raise SystemExit("no ROM: put your .nes file in rom/")
    full, no_p1 = reference(rom, None), reference(rom, "1")
    seconds = FRAMES * 655171.0 / 39375000.0      # the NES's frame: 60.0988 a second
    print("reference: full mix RMS %.0f, without pulse 1 RMS %.0f, %.1f s" % (full, no_p1, seconds))
    bad = 0
    for path in sys.argv[1:]:
        ch, rate, a = load(path)
        left, right = (a[0::2], a[1::2]) if ch == 2 else (a, a)
        r = rms(left)
        same = left == right
        length_ok = abs(len(left) / rate - seconds) / seconds < 0.03
        ok = abs(r * r - full * full) < 0.5 * (full * full - no_p1 * no_p1) and length_ok and same
        bad += not ok
        print("%-30s %d ch %d Hz %5.1f s  RMS %6.0f  peak %5d  L=R %s  -> %s" % (
            os.path.basename(path), ch, rate, len(left) / rate, r, max(abs(x) for x in left) if left else 0, same,
            "all channels present" if ok else "NOT RIGHT" if length_ok else "NOT RIGHT (not the %d-frame stretch)" % FRAMES))
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
