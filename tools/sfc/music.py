#!/usr/bin/env python3
"""tools/sfc/music.py - the SNES Remaster's music: Dragon Quest III's instruments and, for every tune of Dragon
Warrior IV, which of them plays which of the NES's channels (runtime/sfcmusic.c plays it).

    music.py build OUT.bin ARRANGEMENTS.txt DUMPDIR
    music.py samples DUMP [FROM]          lists a dump's samples (number, length, loop, the name of its bytes)

DUMPDIR holds dumps of Dragon Quest III's sound memory, made while a tune plays (sfc-capture --apu-dump FRAME
DUMPDIR/NAME writes NAME.spcram, the sound CPU's 64 KiB, and NAME.dsp, the sound chip's registers). The game
loads a tune's instruments as numbered samples from $17 up; the same instrument has another number in another
tune, so an instrument is named here by a dump and a number, and checked by the first eight hex digits of the
SHA-1 of its sample bytes.

The arrangements file, line by line ("#" starts a comment):
    chip gain PERCENT delay HEX fir HEX*8 master HEX
        the sound chip's settings for all tunes: how loud its output is mixed (100 = as it comes), the echo's
        delay (in 16 ms steps), its eight filter taps and the master volume, as Dragon Quest III sets them
    inst NAME DUMP NUMBER PITCH SHA
        an instrument: sample NUMBER (hex) of DUMP; PITCH = the note it sounds at the chip's pitch $1000, in Hz
        (measured from the sample's own sound; 0 for a drum)
    tune N NAME echo FEEDBACK VOLUME          (hex, as the chip's registers take them)
        the arrangement of tune N (the game's own number; "tune any" = every tune that has none); NAME is only
        for the reader. The lines after it are its parts:
    CHANNEL INSTRUMENT ADSR LEFT RIGHT [octave N] [follow PERCENT] [top N] [ring] [detune CENTS]
        CHANNEL p1, p2 (the pulses) or tri (the triangle); ADSR as four hex digits (the chip's two envelope
        registers); LEFT RIGHT the voice's volume at the channel's loudest; "follow": how much of the game's
        own volume envelope the instrument follows (0 = it only starts and stops with the note, 100 = all of
        it); "top": the NES volume that counts as loudest (default 12); "ring": a plucked string, which rings
        on when the channel falls silent; "octave": octaves above the NES note; "detune": cents above it
    tune N [NAME] nes
        the tune is left to the NES's own sound: the game makes a few sound effects as tunes
"""
import hashlib
import os
import struct
import sys


def sample(ram, dirp, n):
    """sample n of the directory at dirp: its BRR bytes, loop start (bytes), or None"""
    st, lp = struct.unpack_from("<HH", ram, dirp + n * 4)
    if st < 0x200 or st >= 0xFFF7:
        return None
    a = st
    for _ in range(8000):
        h = ram[a]
        if h & 1:
            data = bytes(ram[st:a + 9])
            return data, (lp - st if st <= lp <= a else 0)
        a += 9
        if a + 9 > len(ram):
            break
    return None


def dump(dumpdir, name):
    ram = open(os.path.join(dumpdir, name + ".spcram"), "rb").read()
    dsp = open(os.path.join(dumpdir, name + ".dsp"), "rb").read()
    return ram, dsp[0x5D] << 8


def samples(path, first=0x17):
    ram, dirp = dump(os.path.dirname(path) or ".", os.path.basename(path))
    for n in range(first, 0x40):
        s = sample(ram, dirp, n)
        if s:
            print("%02X  %5d bytes, loop from %5d, %s" % (n, len(s[0]), s[1], hashlib.sha1(s[0]).hexdigest()[:8]))


def build(out, arrangements, dumpdir):
    chip = {"gain": 100, "delay": 0x0B, "fir": [0x7F, 0, 0, 0, 0, 0, 0, 0], "master": 0x78}
    insts, names, tunes, dumps = [], {}, [], {}
    cur = None
    for k, raw in enumerate(open(arrangements, encoding="utf-8"), 1):
        t = raw.split("#")[0].split()
        if not t:
            continue
        where = "%s:%d" % (arrangements, k)
        if t[0] == "chip":
            i = 1
            while i < len(t):
                if t[i] == "gain":
                    chip["gain"] = int(t[i + 1]); i += 2
                elif t[i] == "delay":
                    chip["delay"] = int(t[i + 1], 16); i += 2
                elif t[i] == "master":
                    chip["master"] = int(t[i + 1], 16); i += 2
                elif t[i] == "fir":
                    chip["fir"] = [int(x, 16) for x in t[i + 1:i + 9]]; i += 9
                else:
                    raise SystemExit("%s: what is %r?" % (where, t[i]))
        elif t[0] == "inst":
            name, dname, num, pitch, sha = t[1], t[2], int(t[3], 16), float(t[4]), t[5].lower()
            if dname not in dumps:
                dumps[dname] = dump(dumpdir, dname)
            s = sample(*dumps[dname], num)
            if s is None:
                raise SystemExit("%s: dump %s has no sample %02X" % (where, dname, num))
            got = hashlib.sha1(s[0]).hexdigest()[:8]
            if got != sha:
                raise SystemExit("%s: sample %02X of %s is %s, not %s: another moment of the game, or another ROM" % (where, num, dname, got, sha))
            names[name] = len(insts)
            insts.append((s[0], s[1], pitch))
        elif t[0] == "tune":
            cur = {"tune": 255 if t[1] == "any" else int(t[1]), "nes": "nes" in t[2:], "efb": 0, "evol": 0, "parts": [], "where": where}
            if "echo" in t:
                i = t.index("echo")
                cur["efb"], cur["evol"] = int(t[i + 1], 16), int(t[i + 2], 16)
            if any(x["tune"] == cur["tune"] for x in tunes):
                raise SystemExit("%s: tune %s has an arrangement already" % (where, t[1]))
            tunes.append(cur)
        elif t[0] in ("p1", "p2", "tri"):
            if cur is None or cur["nes"]:
                raise SystemExit("%s: a part of which tune?" % where)
            if t[1] not in names:
                raise SystemExit("%s: no instrument %r" % (where, t[1]))
            part = {"chan": ("p1", "p2", "tri").index(t[0]), "inst": names[t[1]], "adsr": int(t[2], 16), "vl": int(t[3]), "vr": int(t[4]),
                    "octave": 0, "follow": 0, "top": 12, "ring": 0, "detune": 0}
            if not insts[part["inst"]][2] > 0:
                raise SystemExit("%s: %s has no pitch (a drum)" % (where, t[1]))
            i = 5
            while i < len(t):
                if t[i] == "ring":
                    part["ring"] = 1; i += 1
                elif t[i] in ("octave", "follow", "top", "detune"):
                    part[t[i]] = int(t[i + 1]); i += 2
                else:
                    raise SystemExit("%s: what is %r?" % (where, t[i]))
            cur["parts"].append(part)
            if len(cur["parts"]) > 8:
                raise SystemExit("%s: the chip has eight voices" % where)
        else:
            raise SystemExit("%s: what is %r?" % (where, t[0]))
    if sum(len(i[0]) for i in insts) > 0x8000 - 0x0400:
        raise SystemExit("the instruments do not fit the chip's memory beside its echo")
    b = bytearray(b"DW4M" + struct.pack("<HHHHB8BBH", 1, len(insts), len(tunes), chip["gain"], chip["delay"], *chip["fir"], chip["master"], 0))
    for data, loop, pitch in insts:
        b += struct.pack("<HHf", len(data), loop, pitch) + data
    for x in tunes:
        b += struct.pack("<BBBbb3x", x["tune"], 1 if x["nes"] else 0, len(x["parts"]), x["efb"] - 256 if x["efb"] > 127 else x["efb"],
                         x["evol"] - 256 if x["evol"] > 127 else x["evol"])
        for p in x["parts"]:
            b += struct.pack("<BBBBbbbBBBbx", p["chan"], p["inst"], p["adsr"] >> 8, p["adsr"] & 255, p["vl"], p["vr"], p["octave"], p["follow"],
                             p["top"], p["ring"], p["detune"])
    with open(out, "wb") as f:
        f.write(b)
    print("%s: %d instruments (%d bytes of samples), %d arrangements" % (out, len(insts), sum(len(i[0]) for i in insts), len(tunes)))


def main():
    a = sys.argv
    if len(a) == 5 and a[1] == "build":
        build(a[2], a[3], a[4])
    elif len(a) in (3, 4) and a[1] == "samples":
        samples(a[2], int(a[3], 16) if len(a) == 4 else 0x17)
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
